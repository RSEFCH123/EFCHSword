#include "ProcessBehaviorPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QGuiApplication>
#include <QApplication>
#include <QClipboard>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QDebug>
#include <QKeySequence>
#include <memory>

// 网络轮询 (iphlpapi) —— winsock2 必须在 windows.h 前
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <shellapi.h>
#include <userenv.h>
#pragma comment(lib, "userenv.lib")

#include <windows.h>
#include <tlhelp32.h>

namespace {

QString behaviorKindName(const ArkBehaviorEvent& ev)
{
    switch (ev.kind) {
    case BH_ProcCreate:   return QStringLiteral("进程创建");
    case BH_ProcExit:     return QStringLiteral("进程退出");
    case BH_ThreadCreate: return QStringLiteral("线程创建");
    case BH_ThreadExit:   return QStringLiteral("线程退出");
    case BH_ImageLoad:    return QStringLiteral("镜像加载");
    case BH_DiskWrite:    return QStringLiteral("磁盘写入");
    case BH_DiskRead:     return QStringLiteral("磁盘读取");
    case BH_HandleOpen:
        switch (ev.op) {
        case BOP_HandleCreate:        return QStringLiteral("句柄·打开进程");
        case BOP_HandleDuplicate:     return QStringLiteral("句柄·复制进程句柄");
        case BOP_HandleThreadCreate:  return QStringLiteral("句柄·打开线程");
        case BOP_HandleThreadDup:     return QStringLiteral("句柄·复制线程句柄");
        default:                      return QStringLiteral("跨进程句柄");
        }
    case BH_Registry:
        switch (ev.op) {
        case BOP_RegCreateKey:   return QStringLiteral("注册表·建键");
        case BOP_RegDeleteKey:   return QStringLiteral("注册表·删键");
        case BOP_RegRenameKey:   return QStringLiteral("注册表·改键名");
        case BOP_RegSetValue:    return QStringLiteral("注册表·写值");
        case BOP_RegDeleteValue: return QStringLiteral("注册表·删值");
        case BOP_RegLoadKey:     return QStringLiteral("注册表·加载键");
        case BOP_RegReplaceKey:  return QStringLiteral("注册表·替换键");
        case BOP_RegRestoreKey:  return QStringLiteral("注册表·恢复键");
        case BOP_RegOpenKey:     return QStringLiteral("注册表·打开键");
        case BOP_RegQueryValue:  return QStringLiteral("注册表·读取值");
        case BOP_RegEnumValue:   return QStringLiteral("注册表·枚举值");
        default:                 return QStringLiteral("注册表");
        }
    case BH_File:
        switch (ev.op) {
        case BOP_FileCreate: return QStringLiteral("文件·打开/创建");
        case BOP_FileDelete: return QStringLiteral("文件·删除");
        case BOP_FileRename: return QStringLiteral("文件·改名");
        case BOP_FileWrite:  return QStringLiteral("文件·写入");
        case BOP_FileRead:   return QStringLiteral("文件·读取");
        case BOP_FileLink:   return QStringLiteral("文件·创建硬链接");
        default:             return QStringLiteral("文件");
        }
    default:              return QStringLiteral("未知");
    }
}

QString formatTimestamp(quint64 t100ns)
{
    quint64 ms = t100ns / 10000ULL;
    quint64 secs = ms / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg((secs / 3600) % 100, 2, 10, QChar('0'))
        .arg((secs / 60) % 60, 2, 10, QChar('0'))
        .arg(secs % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

QString addrToString(quint32 addrNetOrder, quint16 port)
{
    IN_ADDR a;
    a.S_un.S_addr = addrNetOrder;
    wchar_t buf[46] = {0};
    InetNtopW(AF_INET, &a, buf, 46);
    return QString::fromWCharArray(buf) + QStringLiteral(":%1").arg(port);
}

} // namespace

ProcessBehaviorPage::ProcessBehaviorPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    if (qApp) qApp->installNativeEventFilter(this);
}

ProcessBehaviorPage::~ProcessBehaviorPage()
{
    if (qApp) qApp->removeNativeEventFilter(this);
    if (m_hotkeyHwnd) {
        HWND hwnd = reinterpret_cast<HWND>(m_hotkeyHwnd);
        if (m_killHotkeyId) UnregisterHotKey(hwnd, (int)m_killHotkeyId);
        if (m_suspendHotkeyId) UnregisterHotKey(hwnd, (int)m_suspendHotkeyId);
    }
}

// ==================== 过滤树 ====================

void ProcessBehaviorPage::setupFilterTree()
{
    m_filterTree = new QTreeWidget(this);
    m_filterTree->setHeaderLabel(QStringLiteral("监视行为"));
    m_filterTree->setUniformRowHeights(true);

    auto addKind = [this](int kind, const QString& title, bool checked) {
        auto* item = new QTreeWidgetItem(m_filterTree);
        item->setText(0, title);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        m_kindItems.insert(kind, item);
        return item;
    };
    auto addSub = [this](QTreeWidgetItem* parent, int kind, int op, const QString& title) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, title);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, parent->checkState(0));
        m_subItems.insert(subKey(kind, op), item);
        return item;
    };

    // 根：全选/全不选
    auto* root = new QTreeWidgetItem(m_filterTree);
    root->setText(0, QStringLiteral("全部行为"));
    root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
    root->setCheckState(0, Qt::Checked);
    root->setExpanded(true);

    // ---- 进程/线程 ----
    {
        auto* it = addKind(BH_ProcCreate, QStringLiteral("进程 / 线程"), true);
        it->setText(0, QStringLiteral("进程 / 线程"));
        addSub(it, BH_ProcCreate, BH_ProcCreate, QStringLiteral("进程创建"));
        addSub(it, BH_ProcCreate, BH_ProcExit, QStringLiteral("进程退出"));
        addSub(it, BH_ProcCreate, BH_ThreadCreate, QStringLiteral("线程创建"));
        addSub(it, BH_ProcCreate, BH_ThreadExit, QStringLiteral("线程退出"));
        m_kindItems.insert(BH_ProcCreate, it);
        m_kindItems.insert(BH_ProcExit, it);
        m_kindItems.insert(BH_ThreadCreate, it);
        m_kindItems.insert(BH_ThreadExit, it);
        it->setExpanded(true);
    }
    // ---- 镜像加载 ----
    addKind(BH_ImageLoad, QStringLiteral("镜像加载 (EXE/DLL/驱动)"), true);
    // ---- 注册表 ----
    {
        auto* it = addKind(BH_Registry, QStringLiteral("注册表操作"), false);
        addSub(it, BH_Registry, BOP_RegCreateKey, QStringLiteral("建键"));
        addSub(it, BH_Registry, BOP_RegDeleteKey, QStringLiteral("删键"));
        addSub(it, BH_Registry, BOP_RegRenameKey, QStringLiteral("改键名"));
        addSub(it, BH_Registry, BOP_RegSetValue, QStringLiteral("写值"));
        addSub(it, BH_Registry, BOP_RegDeleteValue, QStringLiteral("删值"));
        addSub(it, BH_Registry, BOP_RegLoadKey, QStringLiteral("加载键 (LoadKey)"));
        addSub(it, BH_Registry, BOP_RegReplaceKey, QStringLiteral("替换键 (ReplaceKey)"));
        addSub(it, BH_Registry, BOP_RegRestoreKey, QStringLiteral("恢复键 (RestoreKey)"));
        addSub(it, BH_Registry, BOP_RegOpenKey, QStringLiteral("打开键 (高频)"));
        addSub(it, BH_Registry, BOP_RegQueryValue, QStringLiteral("读取值 (高频)"));
        addSub(it, BH_Registry, BOP_RegEnumValue, QStringLiteral("枚举值 (高频)"));
        it->setExpanded(false);
    }
    // ---- 文件 ----
    {
        auto* it = addKind(BH_File, QStringLiteral("文件操作"), false);
        addSub(it, BH_File, BOP_FileCreate, QStringLiteral("打开 / 创建"));
        addSub(it, BH_File, BOP_FileDelete, QStringLiteral("删除"));
        addSub(it, BH_File, BOP_FileRename, QStringLiteral("改名"));
        addSub(it, BH_File, BOP_FileWrite, QStringLiteral("写入 (高频)"));
        addSub(it, BH_File, BOP_FileRead, QStringLiteral("读取 (高频)"));
        addSub(it, BH_File, BOP_FileLink, QStringLiteral("创建硬链接"));
        it->setExpanded(false);
    }
    // ---- 磁盘 ----
    {
        auto* it = addKind(BH_DiskWrite, QStringLiteral("磁盘写入"), true);
        auto* big = new QTreeWidgetItem(it);
        big->setText(0, QStringLiteral("仅显示 ≥1MB 的写入"));
        big->setFlags(big->flags() | Qt::ItemIsUserCheckable);
        big->setCheckState(0, Qt::Unchecked);
        m_diskBigOnlyItem = big;
        it->setExpanded(true);
    }
    addKind(BH_DiskRead, QStringLiteral("磁盘读取"), false);
    // ---- 跨进程句柄 ----
    {
        auto* it = addKind(BH_HandleOpen, QStringLiteral("跨进程句柄 / 注入信号"), true);
        addSub(it, BH_HandleOpen, BOP_HandleCreate, QStringLiteral("打开进程句柄"));
        addSub(it, BH_HandleOpen, BOP_HandleDuplicate, QStringLiteral("复制进程句柄"));
        addSub(it, BH_HandleOpen, BOP_HandleThreadCreate, QStringLiteral("打开线程句柄"));
        addSub(it, BH_HandleOpen, BOP_HandleThreadDup, QStringLiteral("复制线程句柄"));
        it->setExpanded(true);
    }
    // ---- 网络 (R3) ----
    {
        auto* it = addKind(BH_NetR3, QStringLiteral("网络连接 (R3)"), true);
        addSub(it, BH_NetR3, 0, QStringLiteral("建立连接"));
        addSub(it, BH_NetR3, 1, QStringLiteral("断开连接"));
        addSub(it, BH_NetR3, 2, QStringLiteral("新监听端口"));
        it->setExpanded(true);
    }

    // 初始混合状态（部分类别默认关）下根节点应为半选。
    // 注意：必须放在 connect 之前——放后面会立刻触发 onFilterItemChanged
    // → rebuildTable()，而此时 m_table 还没创建 → 启动必崩（重大事故已修）。
    if (m_filterTree->topLevelItemCount() > 0) {
        m_filterTree->topLevelItem(0)->setCheckState(0, Qt::PartiallyChecked);
    }

    connect(m_filterTree, &QTreeWidget::itemChanged,
            this, &ProcessBehaviorPage::onFilterItemChanged);
}

void ProcessBehaviorPage::onFilterItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_treeUpdating || column != 0 || item == nullptr) return;
    m_treeUpdating = true;

    // 用户改动的节点：把状态【递归】传播给全部子孙
    // （根→类别→子项 一次勾选全部生效）
    if (item->childCount() > 0) {
        Qt::CheckState st = item->checkState(0);
        QList<QTreeWidgetItem*> stack;
        for (int i = 0; i < item->childCount(); ++i) stack.append(item->child(i));
        while (!stack.isEmpty()) {
            QTreeWidgetItem* cur = stack.takeFirst();
            // “仅显示 ≥1MB”是独立筛选条件，不能被磁盘类别的总开关
            // 强制改写；否则取消后再次勾选类别会偷偷恢复该条件。
            if (cur != m_diskBigOnlyItem) cur->setCheckState(0, st);
            for (int i = 0; i < cur->childCount(); ++i) stack.append(cur->child(i));
        }
    }

    // 从改动节点向上重算每个祖先的三态（由子节点多数派决定）
    for (QTreeWidgetItem* parent = item->parent(); parent != nullptr; parent = parent->parent()) {
        if (parent->childCount() == 0) continue;
        int checked = 0;
        for (int i = 0; i < parent->childCount(); ++i) {
            if (parent->child(i)->checkState(0) == Qt::Checked) ++checked;
        }
        parent->setCheckState(0,
            checked == 0 ? Qt::Unchecked :
            checked == parent->childCount() ? Qt::Checked : Qt::PartiallyChecked);
    }

    m_treeUpdating = false;

    // 树 = 【临时显示过滤】: 内核始终全量记录, 勾选变化只重建表格
    // (想只看注册表就取消其他勾选, 表里立刻只剩注册表事件)
    rebuildTable();
    applyKernelFilterMask();
}

// 按当前树状态把 m_allEvents 里的全部事件重新渲染到表格
void ProcessBehaviorPage::rebuildTable()
{
    if (!m_table) return;   // 构造期防护（树先于表格创建）
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(0);
    for (const ArkBehaviorEvent& ev : m_allEvents) {
        if (!eventMatchesFilters(ev)) continue;
        appendEventRow(ev);
    }
    m_table->setUpdatesEnabled(true);
    m_table->scrollToBottom();
}

// 查询辅助
static bool itemChecked(QTreeWidgetItem* it)
{
    return it != nullptr && it->checkState(0) != Qt::Unchecked;
}

bool ProcessBehaviorPage::netSubChecked(int sub) const
{
    return itemChecked(m_subItems.value(subKey(BH_NetR3, sub)));
}

// ==================== 界面 ====================

void ProcessBehaviorPage::setupUi()
{
    auto* layout = new QVBoxLayout(this);

    // ---- 工具栏 ----
    auto* filterRow = new QHBoxLayout();

    m_globalWatch = new QCheckBox(QStringLiteral("全局监视 (所有进程)"), this);
    m_globalWatch->setChecked(true);
    connect(m_globalWatch, &QCheckBox::toggled, this, [this](bool on) {
        m_pickBtn->setEnabled(!on);
    });
    filterRow->addWidget(m_globalWatch);

    m_pickBtn = new QPushButton(QStringLiteral("选择监视进程 (可多选)"), this);
    m_pickBtn->setEnabled(false);
    connect(m_pickBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onPickTargets);
    filterRow->addWidget(m_pickBtn);

    m_launchBtn = new QPushButton(QStringLiteral("启动并监视..."), this);
    connect(m_launchBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onLaunchAndMonitor);
    filterRow->addWidget(m_launchBtn);

    auto* killBtn = new QPushButton(QStringLiteral("终止目标 (Ctrl+Alt+K)"), this);
    killBtn->setToolTip(QStringLiteral(
        "强制终止当前监视的全部目标进程及其已跟踪的子进程 (驱动强杀, 可终止受保护进程)。\n"
        "当被监视程序被判定为恶意时, 用它立刻止损。"));
    killBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+K")));
    connect(killBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onKillTargets);
    filterRow->addWidget(killBtn);

    auto* suspendBtn = new QPushButton(QStringLiteral("挂起目标 (Ctrl+Alt+P)"), this);
    suspendBtn->setToolTip(QStringLiteral(
        "挂起当前监视的全部目标进程及其已跟踪的子进程 (冻结, 可随时恢复或终止)。"));
    suspendBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+P")));
    connect(suspendBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onSuspendTargets);
    filterRow->addWidget(suspendBtn);

    m_toggleBtn = new QPushButton(QStringLiteral("开始监视"), this);
    connect(m_toggleBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onToggleMonitor);
    filterRow->addWidget(m_toggleBtn);

    m_exportBtn = new QPushButton(QStringLiteral("导出 CSV"), this);
    connect(m_exportBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onExportCsv);
    filterRow->addWidget(m_exportBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("清空列表"), this);
    connect(clearBtn, &QPushButton::clicked, this, &ProcessBehaviorPage::onClearEvents);
    filterRow->addWidget(clearBtn);

    filterRow->addWidget(new QLabel(QStringLiteral("文本过滤:"), this));
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("映像名 / 路径 / PID"));
    m_filterEdit->setMaximumWidth(220);
    filterRow->addWidget(m_filterEdit);
    filterRow->addStretch();
    layout->addLayout(filterRow);

    // ---- 左树 + 右表 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);
    setupFilterTree();
    m_splitter->addWidget(m_filterTree);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("事件"), QStringLiteral("进程名"),
        QStringLiteral("PID"), QStringLiteral("父PID/目标"), QStringLiteral("数值"),
        QStringLiteral("映像/路径"), QStringLiteral("序号")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 生命周期事件使用整行底色标识；交替行背景会覆盖单元格背景，因此关闭它。
    m_table->setAlternatingRowColors(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 110);
    m_table->setColumnWidth(2, 120);
    m_table->setColumnWidth(3, 70);
    m_table->setColumnWidth(4, 80);
    m_table->setColumnWidth(5, 90);
    m_table->setColumnWidth(6, 340);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &ProcessBehaviorPage::onContextMenu);
    m_splitter->addWidget(m_table);
    m_splitter->setSizes({210, 700});
    layout->addWidget(m_splitter, 1);

    m_statusLabel = new QLabel(QStringLiteral("就绪 (未监视)"), this);
    m_statusLabel->setToolTip(
        QStringLiteral("内核环丢弃: 事件到达快于界面拉取时, 环形缓冲(8192条)中最旧事件被覆盖的数量。"
                       "如数值持续增长, 可在左侧树收窄监视范围。"));
    layout->addWidget(m_statusLabel);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &ProcessBehaviorPage::onPollTimeout);

    setupGlobalHotkeys();
}

// 全局热键: 注册到系统级 (RegisterHotKey), 无论窗口是否有焦点都生效。
// Ctrl+Alt+K = 终止监视目标, Ctrl+Alt+P = 挂起监视目标。
// 普通按钮/快捷键在恶意程序抢焦点时会失效, 全局热键不受影响。
void ProcessBehaviorPage::setupGlobalHotkeys()
{
    HWND hwnd = reinterpret_cast<HWND>(window()->winId());
    if (!hwnd) return;
    m_hotkeyHwnd = hwnd;
    m_killHotkeyId = 0x4151;
    m_suspendHotkeyId = 0x4152;
    struct Combo { UINT mods; UINT killVk; UINT suspendVk; };
    const Combo combos[] = {
        { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K', 'P' },
        { MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'K', 'P' },
        { MOD_WIN | MOD_ALT | MOD_NOREPEAT, VK_F11, VK_F12 }
    };
    for (const Combo& c : combos) {
        if (!RegisterHotKey(hwnd, (int)m_killHotkeyId, c.mods, c.killVk)) continue;
        if (!RegisterHotKey(hwnd, (int)m_suspendHotkeyId, c.mods, c.suspendVk)) {
            UnregisterHotKey(hwnd, (int)m_killHotkeyId);
            continue;
        }
        m_hotkeyMods = c.mods;
        m_killHotkeyVk = c.killVk;
        m_suspendHotkeyVk = c.suspendVk;
        return;
    }
    m_killHotkeyId = m_suspendHotkeyId = 0;
    qWarning() << "[Behavior] unable to register global hotkeys:" << GetLastError();
}

bool ProcessBehaviorPage::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    MSG* msg = static_cast<MSG*>(message);
    if (msg != nullptr && msg->message == WM_HOTKEY) {
        if (msg->wParam == 0x4151) {          // Ctrl+Alt+K
            onKillTargets();
            return true;
        }
        if (msg->wParam == 0x4152) {          // Ctrl+Alt+P
            onSuspendTargets();
            return true;
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

bool ProcessBehaviorPage::nativeEventFilter(const QByteArray& eventType,
                                             void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);
    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_HOTKEY) return false;
    if (msg->wParam == 0x4151) { onKillTargets(); return true; }
    if (msg->wParam == 0x4152) { onSuspendTargets(); return true; }
    return false;
}

// ==================== 过滤匹配 ====================

bool ProcessBehaviorPage::eventMatchesFilters(const ArkBehaviorEvent& ev) const
{
    // 类别节点（进程/线程类共享一个节点）
    QTreeWidgetItem* kindItem = m_kindItems.value(ev.kind, nullptr);
    if (kindItem != nullptr && !itemChecked(kindItem)) {
        return false;
    }

    // 子项
    switch (ev.kind) {
    case BH_ProcCreate:
    case BH_ProcExit:
    case BH_ThreadCreate:
    case BH_ThreadExit:
        if (!itemChecked(m_subItems.value(subKey(BH_ProcCreate, ev.kind)))) return false;
        break;
    case BH_Registry:
    case BH_File:
    case BH_HandleOpen:
        if (!itemChecked(m_subItems.value(subKey(ev.kind, ev.op)))) return false;
        break;
    case BH_DiskWrite:
        if (m_diskBigOnlyItem != nullptr &&
            m_diskBigOnlyItem->checkState(0) == Qt::Checked &&
            ev.detail < 1024 * 1024) {
            return false;
        }
        break;
    case BH_DiskRead:
        break;
    default:
        break;
    }

    // 目标进程（全局监视时不过滤）
    // 注意: 跨进程句柄事件里 ev.pid 是【发起者】, ev.parentPid 是【目标】;
    // 监视"某程序的行为"只看发起者, 否则本程序/安全软件探测目标进程的
    // 句柄全会混进来, 表现得像全局监视。
    if (!m_globalWatch->isChecked() && !m_watchPids.isEmpty()) {
        if (ev.kind == BH_ProcCreate) {
            // 进程创建: PID 是新进程自己, 父进程在集合内也放行
            // (子进程跟踪依赖这条)
            if (!m_watchPids.contains(ev.pid) &&
                !(m_watchChildren && m_watchPids.contains(ev.parentPid))) {
                return false;
            }
        } else if (!m_watchPids.contains(ev.pid)) {
            return false;
        }
    }

    // 文本过滤
    QString filter = m_filterEdit ? m_filterEdit->text().trimmed() : QString();
    if (!filter.isEmpty()) {
        QString image = QString::fromWCharArray(ev.image);
        if (!image.contains(filter, Qt::CaseInsensitive) &&
            !QString::number(ev.pid).contains(filter)) {
            return false;
        }
    }
    return true;
}

// ==================== 事件显示 ====================

// PID -> 进程名（缓存 3 秒）
QString ProcessBehaviorPage::pidNameCached(quint32 pid)
{
    if (m_pidNameCacheTime.isValid() &&
        m_pidNameCacheTime.msecsTo(QDateTime::currentDateTime()) < 3000 &&
        m_pidNameCache.contains(pid)) {
        return m_pidNameCache.value(pid);
    }

    m_pidNameCache.clear();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                m_pidNameCache.insert((quint32)pe.th32ProcessID,
                    QString::fromWCharArray(pe.szExeFile));
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    m_pidNameCacheTime = QDateTime::currentDateTime();

    if (m_pidNameCache.contains(pid)) {
        return m_pidNameCache.value(pid);
    }
    return QStringLiteral("(PID %1, 已退出?)").arg(pid);
}

void ProcessBehaviorPage::appendEvent(const ArkBehaviorEvent& ev)
{
    // 存入全量事件列表（临时显示过滤的数据源），再按过滤渲染
    if (m_allEvents.size() < 20000) {
        m_allEvents.append(ev);
    } else {
        m_allEvents.removeFirst();
    }
    if (!eventMatchesFilters(ev)) return;
    appendEventRow(ev);
}

// 渲染单条事件到表格末尾（不做过滤, 过滤由调用方完成）
void ProcessBehaviorPage::appendEventRow(const ArkBehaviorEvent& ev)
{
    int r = m_table->rowCount();
    if (r > 20000) {
        m_table->removeRow(0);
        r--;
    }
    m_table->insertRow(r);

    QString image = QString::fromWCharArray(ev.image);
    QColor rowColor = Qt::black;
    if (ev.kind == BH_ProcCreate) rowColor = QColor(0, 100, 0);
    if (ev.kind == BH_ProcExit)   rowColor = QColor(120, 120, 120);
    if (ev.kind == BH_ImageLoad)  rowColor = QColor(0, 0, 160);
    if (ev.kind == BH_Registry)   rowColor = QColor(140, 60, 160);
    if (ev.kind == BH_File)       rowColor = QColor(0, 110, 110);
    if (ev.kind == BH_DiskWrite)  rowColor = QColor(100, 100, 0);
    if (ev.kind == BH_DiskRead)   rowColor = QColor(0, 90, 140);

    // 数值列
    QString value = QStringLiteral("-");
    if (ev.kind == BH_ProcExit) {
        value = QStringLiteral("0x%1").arg(ev.detail, 8, 16, QChar('0'));
    } else if (ev.kind == BH_DiskWrite || ev.kind == BH_DiskRead) {
        if (ev.detail >= 1024 * 1024)
            value = QStringLiteral("%1 MB").arg(ev.detail / (1024 * 1024));
        else if (ev.detail >= 1024)
            value = QStringLiteral("%1 KB").arg(ev.detail / 1024);
        else
            value = QStringLiteral("%1 B").arg(ev.detail);
    } else if (ev.kind == BH_Registry && ev.op == BOP_RegSetValue) {
        value = QStringLiteral("%1 字节").arg(ev.detail);
    } else if (ev.kind == BH_HandleOpen) {
        value = QStringLiteral("0x%1").arg(ev.detail, 8, 16, QChar('0'));
    }

    QString kindText = behaviorKindName(ev);
    if (ev.kind == BH_ProcCreate) kindText.prepend(QStringLiteral("[CREATE] "));
    if (ev.kind == BH_ProcExit) kindText.prepend(QStringLiteral("[EXIT] "));
    auto* kindItem = new QTableWidgetItem(kindText);
    kindItem->setForeground(QBrush(rowColor));
    if (ev.kind == BH_ProcCreate || ev.kind == BH_ImageLoad) {
        QFont bold = kindItem->font();
        bold.setBold(true);
        kindItem->setFont(bold);
    }
    // 高风险跨进程句柄标红：
    //   进程: CREATE_THREAD(0x2)/VM_OP(0x8)/VM_WRITE(0x20)/DUP(0x40)/SUSPEND(0x800)
    //   线程: TERMINATE(0x1)/SUSPEND_RESUME(0x2)/SET_CONTEXT(0x10)/IMPERSONATE(0x100)
    if (ev.kind == BH_HandleOpen) {
        ULONG risky = (ev.op == BOP_HandleThreadCreate || ev.op == BOP_HandleThreadDup)
            ? 0x00000001UL | 0x00000002UL | 0x00000010UL | 0x00000100UL | 0x00000200UL
            : 0x00000002UL | 0x00000008UL | 0x00000020UL | 0x00000040UL | 0x00000800UL;
        if ((ev.detail & risky) != 0) {
            kindItem->setForeground(QBrush(QColor(200, 0, 0)));
            QFont bold = kindItem->font();
            bold.setBold(true);
            kindItem->setFont(bold);
        }
    }

    m_table->setItem(r, 0, new QTableWidgetItem(formatTimestamp(ev.timestamp)));
    m_table->setItem(r, 1, kindItem);
    m_table->setItem(r, 2, new QTableWidgetItem(pidNameCached(ev.pid)));
    m_table->setItem(r, 3, new QTableWidgetItem(QString::number(ev.pid)));

    // 父PID/目标列：进程创建=父PID，线程=TID，句柄=目标PID
    QString third = QStringLiteral("-");
    if (ev.kind == BH_ProcCreate) third = QString::number(ev.parentPid);
    if (ev.kind == BH_ThreadCreate || ev.kind == BH_ThreadExit) third = QString::number(ev.tid);
    if (ev.kind == BH_HandleOpen) third = QStringLiteral("目标:%1").arg(ev.parentPid);
    m_table->setItem(r, 4, new QTableWidgetItem(third));
    m_table->setItem(r, 5, new QTableWidgetItem(value));

    auto* imgItem = new QTableWidgetItem(image);
    if (ev.kind == BH_ProcExit || ev.kind == BH_ThreadExit) {
        imgItem->setForeground(QBrush(Qt::gray));
    }
    m_table->setItem(r, 6, imgItem);
    m_table->setItem(r, 7, new QTableWidgetItem(QString::number(ev.sequence)));
    // 进程创建/退出使用整行短期可见的语义颜色；退出事件保留灰红色，
    // 便于在高频事件中确认进程生命周期变化。
    if (ev.kind == BH_ProcCreate || ev.kind == BH_ProcExit) {
        const QColor lifecycleColor = ev.kind == BH_ProcCreate
            ? QColor(0, 150, 0) : QColor(210, 70, 70);
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (auto* cell = m_table->item(r, c)) {
                const QColor bg = ev.kind == BH_ProcCreate
                    ? QColor(225, 250, 225) : QColor(255, 225, 225);
                cell->setData(Qt::ForegroundRole, QBrush(lifecycleColor));
                cell->setData(Qt::BackgroundRole, QBrush(bg));
            }
        }
    }
}

void ProcessBehaviorPage::appendNetEvent(const QString& eventName, quint32 pid,
                                         const QString& image, const QString& local,
                                         const QString& remote)
{
    int r = m_table->rowCount();
    if (r > 20000) {
        m_table->removeRow(0);
        r--;
    }
    m_table->insertRow(r);

    quint64 now = GetTickCount64() * 10000ULL;
    m_table->setItem(r, 0, new QTableWidgetItem(formatTimestamp(now)));
    m_table->setItem(r, 1, new QTableWidgetItem(eventName));
    m_table->setItem(r, 2, new QTableWidgetItem(pidNameCached(pid)));
    m_table->setItem(r, 3, new QTableWidgetItem(QString::number(pid)));
    m_table->setItem(r, 4, new QTableWidgetItem(QStringLiteral("-")));
    m_table->setItem(r, 5, new QTableWidgetItem(remote));
    m_table->setItem(r, 6, new QTableWidgetItem(
        image + QStringLiteral("  [%1 → %2]").arg(local, remote)));
    m_table->setItem(r, 7, new QTableWidgetItem(QStringLiteral("R3")));
}

// ==================== 监视控制 ====================

void ProcessBehaviorPage::applyKernelFilterMask()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    // 内核始终全量记录 (BM_All), 勾选树只影响【显示】——
    // 用户可以随时改勾选重新过滤表格而不丢事件
    quint32 mask = BM_All;
    if (!ctx.driver().behaviorSetFilter(mask)) {
        m_statusLabel->setText(QStringLiteral("设置记录类别失败 (错误码: %1)")
            .arg(ctx.driver().lastIoError()));
    }

    // 文件/磁盘监视需要卷过滤设备在位。
    // 判定用【子项】而不是类别节点的三态（PartiallyChecked 会被
    // itemChecked 视为真, 导致构造期间 root 半选就误挂卷过滤）。
    bool fileActive = false;
    static const int fileOps[] = { BOP_FileCreate, BOP_FileDelete, BOP_FileRename,
                                   BOP_FileWrite, BOP_FileRead, BOP_FileLink };
    for (int op : fileOps) {
        if (itemChecked(m_subItems.value(subKey(BH_File, op)))) { fileActive = true; break; }
    }
    bool diskActive = itemChecked(m_kindItems.value(BH_DiskWrite));
    bool needMon = (itemChecked(m_kindItems.value(BH_File)) && fileActive) || diskActive;
    if (needMon) {
        if (!ctx.driver().filemonStart()) {
            m_statusLabel->setText(QStringLiteral("挂载卷过滤设备失败 (错误码: %1), 文件/磁盘监视不可用")
                .arg(ctx.driver().lastIoError()));
        }
    } else {
        ctx.driver().filemonStop();
    }
}

// 按 m_blockMalware 开关同步 Harden 名单
void ProcessBehaviorPage::syncHardenState()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (m_blockMalware) {
        for (quint32 pid : m_watchPids) {
            if (!m_hardenedPids.contains(pid)) {
                quint32 flags = 0;
                if (ctx.driver().hardenAddPid(pid, (quint64)(ULONG_PTR)GetModuleHandleW(L"ntdll.dll"), &flags)) {
                    m_hardenedPids.insert(pid);
                }
            }
        }
    } else {
        for (quint32 pid : m_hardenedPids) {
            ctx.driver().hardenRemovePid(pid);
        }
        m_hardenedPids.clear();
    }
}

void ProcessBehaviorPage::onToggleMonitor()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("行为监视"),
            QStringLiteral("驱动未加载, 仅可使用网络连接(R3)监视"));
        m_monitoring = !m_monitoring;
        if (m_monitoring) {
            m_netFirstPoll = true;
            m_pollTimer->start();
            m_toggleBtn->setText(QStringLiteral("暂停监视"));
            m_statusLabel->setText(QStringLiteral("监视中 (仅网络R3, 驱动未加载)"));
        } else {
            m_pollTimer->stop();
            m_toggleBtn->setText(QStringLiteral("开始监视"));
            m_statusLabel->setText(QStringLiteral("已暂停"));
        }
        return;
    }

    m_monitoring = !m_monitoring;
    if (m_monitoring) {
        applyKernelFilterMask();

        auto log = std::make_unique<ArkBehaviorLog>();
        quint64 current = 0;
        if (ctx.driver().behaviorReadLog(m_lastSeq, *log)) {
            current = log->nextSeq;
        }
        m_lastSeq = current;
        m_netFirstPoll = true;
        m_pollTimer->start();
        m_toggleBtn->setText(QStringLiteral("暂停监视"));
        if (m_globalWatch->isChecked()) {
            m_statusLabel->setText(QStringLiteral("监视中 (全局) - 事件: %1")
                .arg(m_table->rowCount()));
        } else {
            m_statusLabel->setText(QStringLiteral("监视中 %1 - 事件: %2")
                .arg(m_watchSummary).arg(m_table->rowCount()));
        }
    } else {
        m_pollTimer->stop();
        m_toggleBtn->setText(QStringLiteral("开始监视"));
        m_statusLabel->setText(QStringLiteral("已暂停 (事件: %1)").arg(m_table->rowCount()));
        // 暂停监视不解除 Harden 名单（恶意行为阻止应持续到程序关闭或手动取消勾选）
    }
}

// ==================== R3 网络轮询 ====================

void ProcessBehaviorPage::pollNetworkR3()
{
    if (!itemChecked(m_kindItems.value(BH_NetR3))) return;

    QVector<NetConn> current;
    QVector<NetListen> currentListens;
    ULONG size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                            TCP_TABLE_OWNER_MODULE_ALL, 0) != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }
    QByteArray buf(size, Qt::Uninitialized);
    if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                            TCP_TABLE_OWNER_MODULE_ALL, 0) != NO_ERROR) {
        return;
    }
    auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_MODULE>(buf.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_MODULE& row = table->table[i];
        if (row.dwState == MIB_TCP_STATE_LISTEN) {
            NetListen l;
            l.pid = row.dwOwningPid;
            l.localAddr = row.dwLocalAddr;
            l.localPort = ntohs((u_short)(row.dwLocalPort & 0xFFFF));
            currentListens.append(l);
            continue;
        }
        if (row.dwState != MIB_TCP_STATE_ESTAB && row.dwState != MIB_TCP_STATE_SYN_SENT &&
            row.dwState != MIB_TCP_STATE_TIME_WAIT) {
            continue;
        }
        NetConn c;
        c.pid = row.dwOwningPid;
        c.localAddr = row.dwLocalAddr;
        c.localPort = ntohs((u_short)(row.dwLocalPort & 0xFFFF));
        c.remoteAddr = row.dwRemoteAddr;
        c.remotePort = ntohs((u_short)(row.dwRemotePort & 0xFFFF));
        if (c.remoteAddr == 0) continue;
        current.append(c);
    }

    if (!m_netFirstPoll) {
        for (const NetConn& c : current) {
            if (m_prevNetConns.contains(c)) continue;
            if (m_globalWatch->isChecked() || m_watchPids.isEmpty() ||
                m_watchPids.contains(c.pid)) {
                if (netSubChecked(0)) {
                    appendNetEvent(QStringLiteral("网络·建立连接"), c.pid, pidNameCached(c.pid),
                                   addrToString(c.localAddr, c.localPort),
                                   addrToString(c.remoteAddr, c.remotePort));
                }
            }
        }
        for (const NetConn& c : m_prevNetConns) {
            if (current.contains(c)) continue;
            if (m_globalWatch->isChecked() || m_watchPids.isEmpty() ||
                m_watchPids.contains(c.pid)) {
                if (netSubChecked(1)) {
                    appendNetEvent(QStringLiteral("网络·断开"), c.pid, QStringLiteral("(已结束)"),
                                   addrToString(c.localAddr, c.localPort),
                                   addrToString(c.remoteAddr, c.remotePort));
                }
            }
        }
        // 新监听端口
        for (const NetListen& l : currentListens) {
            if (m_prevNetListens.contains(l)) continue;
            if (m_globalWatch->isChecked() || m_watchPids.isEmpty() ||
                m_watchPids.contains(l.pid)) {
                if (netSubChecked(2)) {
                    appendNetEvent(QStringLiteral("网络·新监听端口"), l.pid,
                                   pidNameCached(l.pid),
                                   addrToString(l.localAddr, l.localPort),
                                   QStringLiteral("(本机监听)"));
                }
            }
        }
    }
    m_prevNetConns = current;
    m_prevNetListens = currentListens;
    m_netFirstPoll = false;
}

void ProcessBehaviorPage::onPollTimeout()
{
    auto& ctx = AppContext::instance();

    pollNetworkR3();

    if (!ctx.driverLoaded() || !m_monitoring) return;

    auto log = std::make_unique<ArkBehaviorLog>();
    if (!ctx.driver().behaviorReadLog(m_lastSeq, *log)) return;

    m_lastSeq = log->nextSeq;
    int appended = 0;
    if (m_watchChildren && !m_globalWatch->isChecked()) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (quint32 j = 0; j < log->count; ++j) {
                const ArkBehaviorEvent& p = log->events[j];
                if (p.kind == BH_ProcCreate && m_watchPids.contains(p.parentPid) &&
                    !m_watchPids.contains(p.pid)) {
                    m_watchPids.insert(p.pid);
                    changed = true;
                    if (m_blockMalware && ctx.driverLoaded() &&
                        !m_hardenedPids.contains(p.pid) &&
                        ctx.driver().hardenAddPid(p.pid, 0)) {
                        m_hardenedPids.insert(p.pid);
                    }
                }
            }
        }
    }
    for (quint32 i = 0; i < log->count; ++i) {
        const ArkBehaviorEvent& ev = log->events[i];

        // 子进程跟踪：父进程在监视集合内的新建进程自动加入集合
        if (m_watchChildren && ev.kind == BH_ProcCreate &&
            !m_globalWatch->isChecked() &&
            m_watchPids.contains(ev.parentPid)) {
            m_watchPids.insert(ev.pid);
            // 阻止恶意行为开启时，子进程同样进入 Harden 名单
            if (m_blockMalware && ctx.driverLoaded() &&
                !m_hardenedPids.contains(ev.pid)) {
                if (ctx.driver().hardenAddPid(ev.pid, (quint64)(ULONG_PTR)GetModuleHandleW(L"ntdll.dll"))) {
                    m_hardenedPids.insert(ev.pid);
                }
            }
        }

        if (!eventMatchesFilters(ev)) continue;
        appendEvent(ev);
        ++appended;
    }

    if (appended > 0) {
        m_table->scrollToBottom();
    }
    QString base = m_globalWatch->isChecked()
        ? QStringLiteral("监视中 (全局)")
        : QStringLiteral("监视中 %1").arg(m_watchSummary);
    // 卷过滤挂载状态直接可见（0 卷 = 文件/磁盘监视未生效, 一眼可辨）
    m_statusLabel->setText(QStringLiteral("%1 - 事件: %2 (内核环丢弃: %3)")
        .arg(base).arg(m_table->rowCount()).arg(log->dropped));
}

// ==================== 目标选择 / 启动并监视 ====================

void ProcessBehaviorPage::onPickTargets()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("选择监视目标进程 (可多选)"));
    dlg.resize(520, 560);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(QStringLiteral("勾选要监视的进程 (含其创建的子进程):"), &dlg));

    auto* list = new QListWidget(&dlg);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == 0) continue;
                auto* item = new QListWidgetItem(
                    QStringLiteral("[%1] %2")
                        .arg(pe.th32ProcessID)
                        .arg(QString::fromWCharArray(pe.szExeFile)), list);
                item->setData(Qt::UserRole, (quint32)pe.th32ProcessID);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(m_watchPids.contains((quint32)pe.th32ProcessID)
                    ? Qt::Checked : Qt::Unchecked);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    lay->addWidget(list, 1);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    m_watchPids.clear();
    QStringList names;
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        if (item->checkState() == Qt::Checked) {
            quint32 pid = item->data(Qt::UserRole).value<quint32>();
            m_watchPids.insert(pid);
            names << item->text();
        }
    }
    if (m_watchPids.isEmpty()) {
        m_watchSummary = QStringLiteral("(未选择目标)");
    } else if (m_watchPids.size() <= 3) {
        m_watchSummary = names.join(QStringLiteral(", "));
    } else {
        m_watchSummary = QStringLiteral("%1 个目标进程").arg(m_watchPids.size());
    }
    m_watchChildren = false;
    m_statusLabel->setText(QStringLiteral("已选择 %1 个目标进程").arg(m_watchPids.size()));
}

// 启动一个进程。
//   mode 0 = 继承当前权限；1 = 管理员 (UAC)；2 = 标准用户 (降权)；
//   3 = 防护模式 (受限令牌: DISABLE_MAX_PRIVILEGE + 禁用管理员组)
// mode 2/3 以 CREATE_SUSPENDED 挂起态创建，由调用方完成"是否继续运行"
// 的门控后决定 Resume 还是 Terminate —— 绝不让未经确认的进程先跑起来。
// bypass740: 附加 __COMPAT_LAYER=RUNASINVOKER 环境变量绕过清单提权要求。
bool ProcessBehaviorPage::launchProcess(const QString& exePath, int mode, bool bypass740,
                                        quint32* outPid, HANDLE* outProcess, HANDLE* outThread)
{
    *outPid = 0;
    *outProcess = NULL;
    *outThread = NULL;
    DWORD suspended = (mode == 2 || mode == 3) ? CREATE_SUSPENDED : 0;

    QByteArray envBytes;
    LPVOID envPtr = NULL;
    if (bypass740) {
        envBytes = envBlockWithCompatLayer(L"RUNASINVOKER");
        envPtr = envBytes.data();
    }

    if (mode == 1) {
        SHELLEXECUTEINFOW sei;
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE | SEE_MASK_UNICODE;
        sei.lpVerb = L"runas";
        sei.lpFile = (LPCWSTR)QDir::toNativeSeparators(exePath).utf16();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei) || sei.hProcess == NULL) {
            return false;
        }
        *outPid = GetProcessId(sei.hProcess);
        *outProcess = sei.hProcess;      // 调用方负责关闭
        return *outPid != 0;
    }

    // mode 2 (标准用户) / mode 3 (防护模式) 都走 explorer 过滤令牌
    if (mode == 2 || mode == 3) {
        if (mode == 2 && isUacDisabled()) {
            return false;    // EnableLUA=0 无可降级令牌（调用方已预拦截）
        }
        DWORD explorerPid = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                        explorerPid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        if (explorerPid == 0) {
            return false;
        }

        HANDLE hExplorer = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, explorerPid);
        if (!hExplorer) return false;
        HANDLE hToken = NULL;
        bool ok = false;
        if (OpenProcessToken(hExplorer, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken) && hToken) {
            HANDLE hNewToken = NULL;
            if (DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL,
                                 SecurityImpersonation, TokenPrimary, &hNewToken) && hNewToken) {
                // 防护模式：CreateRestrictedToken 移除全部特权并禁用管理员组
                if (mode == 3) {
                    DWORD retLen = 0;
                    if (GetTokenInformation(hNewToken, TokenPrivileges, NULL, 0, &retLen) ||
                        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                        PTOKEN_PRIVILEGES privs = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, retLen);
                        if (privs && GetTokenInformation(hNewToken, TokenPrivileges,
                                                         privs, retLen, &retLen)) {
                            SID_AND_ATTRIBUTES disableSids[1];
                            SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
                            PSID adminSid = NULL;
                            if (AllocateAndInitializeSid(&ntAuthority, 2,
                                    SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                    0, 0, 0, 0, 0, 0, &adminSid)) {
                                disableSids[0].Sid = adminSid;
                                disableSids[0].Attributes = 0;
                                HANDLE hRestricted = NULL;
                                if (CreateRestrictedToken(hNewToken, DISABLE_MAX_PRIVILEGE,
                                        1, disableSids,
                                        privs->PrivilegeCount, privs->Privileges,
                                        0, NULL, &hRestricted) && hRestricted) {
                                    CloseHandle(hNewToken);
                                    hNewToken = hRestricted;
                                }
                                FreeSid(adminSid);
                            }
                        }
                        if (privs) LocalFree(privs);
                    }
                }

                STARTUPINFOW si;
                PROCESS_INFORMATION pi;
                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));
                QString cmd = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(exePath));
                QByteArray cmdBytes((const char*)cmd.utf16(), (cmd.size() + 1) * (int)sizeof(wchar_t));
                if (CreateProcessWithTokenW(hNewToken, LOGON_WITH_PROFILE, NULL,
                                            (LPWSTR)cmdBytes.data(),
                                            suspended | CREATE_UNICODE_ENVIRONMENT,
                                            envPtr, NULL, &si, &pi)) {
                    *outPid = pi.dwProcessId;
                    *outProcess = pi.hProcess;
                    *outThread = pi.hThread;
                    ok = (*outPid != 0);
                }
            }
            CloseHandle(hToken);
        }
        CloseHandle(hExplorer);
        return ok;
    }

    // mode 0: 继承当前权限
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    QString cmd = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(exePath));
    QByteArray cmdBytes((const char*)cmd.utf16(), (cmd.size() + 1) * (int)sizeof(wchar_t));
    if (!CreateProcessW(NULL, (LPWSTR)cmdBytes.data(), NULL, NULL, FALSE,
                        suspended, envPtr, NULL, &si, &pi)) {
        return false;
    }
    *outPid = pi.dwProcessId;
    *outProcess = pi.hProcess;
    *outThread = pi.hThread;
    return *outPid != 0;
}

// 构造 "当前环境 + __COMPAT_LAYER=<layer>" 的环境块（双 NUL 结尾）
QByteArray ProcessBehaviorPage::envBlockWithCompatLayer(const wchar_t* compatLayer)
{
    QByteArray block;
    QStringList lines;
    bool hasCompat = false;

    LPWCH env = GetEnvironmentStringsW();
    if (env) {
        LPWCH p = env;
        while (*p) {
            QString line = QString::fromWCharArray(p);
            if (line.startsWith(QStringLiteral("__COMPAT_LAYER="), Qt::CaseInsensitive)) {
                hasCompat = true;
                lines << QStringLiteral("__COMPAT_LAYER=%1").arg(QString::fromWCharArray(compatLayer));
            } else {
                lines << line;
            }
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(env);
    }
    if (!hasCompat) {
        lines << QStringLiteral("__COMPAT_LAYER=%1").arg(QString::fromWCharArray(compatLayer));
    }
    for (const QString& l : lines) {
        block.append((const char*)l.utf16(), (l.size() + 1) * (int)sizeof(wchar_t));
    }
    block.append('\0');   // 结尾双 NUL
    block.append('\0');
    return block;
}

bool ProcessBehaviorPage::isUacDisabled()
{
    QSettings s(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"),
                QSettings::NativeFormat);
    return s.value(QStringLiteral("EnableLUA"), 1).toInt() == 0;
}

// 启动后实际查一次子进程令牌，防止"假降权"
bool ProcessBehaviorPage::isChildElevated(quint32 pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    HANDLE hToken = NULL;
    bool elevated = false;
    if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken) && hToken) {
        DWORD elev = 0, retLen = 0;
        if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &retLen)) {
            elevated = (elev != 0);
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProc);
    return elevated;
}

void ProcessBehaviorPage::onLaunchAndMonitor()
{
    QString exePath = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择要启动并监视的可执行文件"), QString(),
        QStringLiteral("可执行程序 (*.exe *.cmd *.bat);;所有文件 (*.*)"));
    if (exePath.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("启动并监视 - 选项"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QStringLiteral("将启动: %1").arg(exePath), &dlg));

    auto* watchChildren = new QCheckBox(QStringLiteral("自动跟踪并监视其创建的子进程"), &dlg);
    watchChildren->setChecked(true);
    lay->addWidget(watchChildren);

    auto* permRow = new QHBoxLayout();
    permRow->addWidget(new QLabel(QStringLiteral("启动权限:"), &dlg));
    auto* permCombo = new QComboBox(&dlg);
    permCombo->addItem(QStringLiteral("继承当前程序权限"), 0);
    permCombo->addItem(QStringLiteral("以管理员身份运行 (弹 UAC, 给予权限)"), 1);
    permCombo->addItem(QStringLiteral("以标准用户身份运行 (剥夺管理员权限)"), 2);
    permCombo->addItem(QStringLiteral("防护模式 (剥夺几乎全部权限: 无特权+无管理员组)"), 3);
    permRow->addWidget(permCombo);
    permRow->addStretch();
    lay->addLayout(permRow);

    auto* blockCheck = new QCheckBox(
        QStringLiteral("阻止可能的恶意行为"), &dlg);
    blockCheck->setChecked(m_blockMalware);
    lay->addWidget(blockCheck);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    int mode = permCombo->currentData().toInt();
    bool block = blockCheck->isChecked();
    m_blockMalware = block;

    // UAC 关闭时降权必然失败（所有令牌都是未过滤的管理员令牌），提前拦截
    if (mode == 2 && isUacDisabled()) {
        QMessageBox::warning(this, QStringLiteral("启动并监视"),
            QStringLiteral("无法剥夺管理员权限: 系统 UAC 已完全关闭 (EnableLUA=0)。\n"
                           "此时所有进程都直接以管理员令牌运行, 不存在可降级的标准用户令牌。\n"
                           "如需降权, 请在 [控制面板-用户账户] 或组策略中重新开启 UAC 后重启系统。"));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("启动确认"),
        QStringLiteral("即将启动并监视:\n%1\n\n"
                       "注意: 本程序只能监视该程序的运行行为, "
                       "【不能】保护你的计算机安全! 请确认你了解将要运行的程序。\n\n"
                       "是否继续?").arg(exePath),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    quint32 newPid = 0;
    HANDLE hProcess = NULL, hThread = NULL;
    bool bypassed = false;
    if (!launchProcess(exePath, mode, false, &newPid, &hProcess, &hThread) || newPid == 0) {
        DWORD err = GetLastError();
        if (err == 740 && mode == 2) {
            // 目标清单要求管理员权限 → 用 RUNASINVOKER 兼容标记绕过再试一次
            if (launchProcess(exePath, mode, true, &newPid, &hProcess, &hThread) && newPid != 0) {
                bypassed = true;
            } else {
                err = GetLastError();
            }
        }
        if (newPid == 0) {
            QString why;
            if (err == 740) {
                why = QStringLiteral("\n\n原因: 该程序的清单要求管理员权限运行 "
                                     "(ERROR_ELEVATION_REQUIRED, 740), "
                                     "即使使用 RUNASINVOKER 兼容标记也未能绕过。请改用管理员模式。");
            } else {
                why = QStringLiteral("\n\n错误码: %1").arg(err);
            }
            QMessageBox::warning(this, QStringLiteral("启动并监视"),
                QStringLiteral("启动失败。") + why);
            return;
        }
    }

    // ===== 挂起门控：mode 2/3 时进程此刻是挂起态，尚未运行任何代码 =====
    if (mode == 2 || mode == 3) {
        QString modeWarn;
        if (isChildElevated(newPid)) {
            modeWarn = QStringLiteral(
                "警告: 令牌降级未生效, 目标进程仍持有管理员令牌!\n"
                "可能原因: 系统的 UAC 过滤被禁用 (EnableLUA=0 或注册表策略), "
                "或安全软件干预了令牌复制。\n\n");
        }
        if (bypassed) {
            modeWarn += QStringLiteral("(已通过 __COMPAT_LAYER=RUNASINVOKER "
                                       "绕过该程序的清单提权要求)\n\n");
        }
        auto gate = QMessageBox::warning(this, QStringLiteral("启动确认 - 进程已挂起"),
            QStringLiteral("目标进程 (PID=%1) 已创建但处于【挂起】状态, 尚未运行任何代码。\n\n")
                .arg(newPid) + modeWarn +
            QStringLiteral("是否继续运行?\n\n"
                           "是 = 恢复运行并开始监视\n"
                           "否 = 立即终止该进程"),
            QMessageBox::Yes | QMessageBox::No);
        if (gate != QMessageBox::Yes) {
            if (hProcess != NULL) {
                TerminateProcess(hProcess, 1);
            }
            if (hThread) CloseHandle(hThread);
            if (hProcess) CloseHandle(hProcess);
            m_statusLabel->setText(
                QStringLiteral("已终止挂起进程 (PID=%1), 未运行任何代码").arg(newPid));
            return;
        }
        if (hThread != NULL) {
            ResumeThread(hThread);
        }
    }

    // 阻止恶意行为：把目标进程加入内核 Harden 名单。
    // 驱动侧会轮询等待目标的加载器初始化（24H2 每进程 ntdll 基址随机,
    // 必须从目标自己的 PEB->Ldr 解析）, 补丁落在目标入口代码执行之前
    // （Ldr 初始化先于 main）。
    //注意，多次测试后这个Harden补丁一直出现问题，这个不弄了，直接注释掉
    //注意，多次测试后这个Harden补丁一直出现问题，这个不弄了，直接注释掉
    //注意，多次测试后这个Harden补丁一直出现问题，这个不弄了，直接注释掉
    /*
    auto& ctx0 = AppContext::instance();
    if (block && ctx0.driverLoaded() && !m_hardenedPids.contains(newPid)) {
        quint32 flags = 0;
        if (ctx0.driver().hardenAddPid(newPid, 0, &flags)) {
            m_hardenedPids.insert(newPid);
            if ((flags & 0x01) == 0) {
                QString stage;
                if (!(flags & 0x10))      stage = QStringLiteral("目标加载器初始化等待超时 (进程可能立即退出)");
                else if (!(flags & 0x20)) stage = QStringLiteral("PEB->Ldr 模块链不可读 (PEBOffset 与系统不匹配)");
                else if (!(flags & 0x40)) stage = QStringLiteral("加载器列表中未找到 ntdll");
                else if (!(flags & 0x80)) stage = QStringLiteral("ntdll 导出表中未找到 NtRaiseHardError");
                else                      stage = QStringLiteral("内存写入被拒绝");
                QMessageBox::warning(this, QStringLiteral("启动并监视"),
                    QStringLiteral("注意: 防蓝屏拦截未能应用。\n失败阶段: %1\n"
                                   "其余拦截(磁盘/驱动加载)仍然生效。").arg(stage));
            }
        } else {
            QMessageBox::warning(this, QStringLiteral("启动并监视"),
                QStringLiteral("Harden 名单加入失败 (错误码: %1), "
                               "恶意行为阻止对该进程不生效!").arg(ctx0.driver().lastIoError()));
        }
    }
    */
    if (hThread) CloseHandle(hThread);
    if (hProcess) CloseHandle(hProcess);
    

    // 降权后核验真实令牌，杜绝"假降权"
    if (mode == 2 && isChildElevated(newPid)) {
        QMessageBox::warning(this, QStringLiteral("启动并监视"),
            QStringLiteral("警告: 降权未生效, 子进程 (PID=%1) 仍持有管理员令牌。\n"
                           "可能原因: 系统的 UAC 过滤被禁用 (EnableLUA=0 或注册表策略), "
                           "或安全软件干预了令牌复制。").arg(newPid));
    }

    m_watchChildren = watchChildren->isChecked();
    m_globalWatch->setChecked(false);
    m_watchPids.clear();
    m_watchPids.insert(newPid);
    m_watchSummary = QStringLiteral("[%1] %2").arg(newPid)
        .arg(QFileInfo(exePath).fileName());

    if (m_monitoring) {
        applyKernelFilterMask();
    } else {
        onToggleMonitor();
    }
    m_statusLabel->setText(
        QStringLiteral("已启动并监视: %1%2")
            .arg(m_watchSummary)
            .arg(m_watchChildren ? QStringLiteral(", 含子进程") : QString()));
}

// ==================== 杂项 ====================

// 强制终止监视目标及其子进程（驱动 APC 终止, 可终止受保护进程）
void ProcessBehaviorPage::onKillTargets()
{
    if (m_watchPids.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("终止目标"),
            QStringLiteral("当前未选择任何监视目标 (全局模式下请先切换到目标模式)"));
        return;
    }
    auto& ctx = AppContext::instance();

    auto ret = QMessageBox::warning(this, QStringLiteral("终止目标"),
        QStringLiteral("确定强制终止全部监视目标 (含子进程) 吗?\n共 %1 个进程。")
            .arg(m_watchPids.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    int killed = 0;
    for (quint32 pid : m_watchPids) {
        if (ctx.driverLoaded()) {
            if (ctx.driver().zwKillProcess(pid)) ++killed;
        } else {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) {
                if (TerminateProcess(h, 1)) ++killed;
                CloseHandle(h);
            }
        }
        if (m_hardenedPids.contains(pid)) {
            if (ctx.driverLoaded()) ctx.driver().hardenRemovePid(pid);
            m_hardenedPids.remove(pid);
        }
    }
    m_statusLabel->setText(
        QStringLiteral("终止指令已发送: %1/%2 个进程").arg(killed).arg(m_watchPids.size()));
}

// 挂起监视目标及其子进程（冻结状态, 可用进程页恢复）
void ProcessBehaviorPage::onSuspendTargets()
{
    if (m_watchPids.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("挂起目标"),
            QStringLiteral("当前未选择任何监视目标 (全局模式下请先切换到目标模式)"));
        return;
    }
    auto& ctx = AppContext::instance();

    int suspended = 0;
    for (quint32 pid : m_watchPids) {
        if (ctx.driverLoaded()) {
            if (ctx.driver().suspendProcess(pid)) ++suspended;
        } else {
            // R3 回退: 挂起进程全部线程
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                THREADENTRY32 te;
                te.dwSize = sizeof(te);
                if (Thread32First(hSnap, &te)) {
                    do {
                        if (te.th32OwnerProcessID != pid) continue;
                        HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                        if (hThread) {
                            if (SuspendThread(hThread) != (DWORD)-1) ++suspended;
                            CloseHandle(hThread);
                        }
                    } while (Thread32Next(hSnap, &te));
                }
                CloseHandle(hSnap);
            }
        }
    }
    m_statusLabel->setText(
        QStringLiteral("挂起指令已发送: %1 个目标, 共 %2 线程级操作").arg(m_watchPids.size()).arg(suspended));
}

void ProcessBehaviorPage::onClearEvents()
{
    m_allEvents.clear();
    m_table->setRowCount(0);
    m_statusLabel->setText(QStringLiteral("列表已清空"));
}

void ProcessBehaviorPage::onExportCsv()
{
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出行为事件"), QStringLiteral("behavior_log.csv"),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("导出"),
            QStringLiteral("无法创建文件: %1").arg(f.errorString()));
        return;
    }

    f.write("\xEF\xBB\xBF");
    QTextStream ts(&f);
    ts << QStringLiteral("时间,事件,进程名,PID,父PID/目标,数值,映像/路径,序号\r\n");

    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto* it = m_table->item(r, c);
            QString text = it ? it->text() : QString();
            text.replace(QChar('"'), QStringLiteral("\"\""));
            cells << QStringLiteral("\"%1\"").arg(text);
        }
        ts << cells.join(QChar(',')) << QStringLiteral("\r\n");
    }
    ts.flush();
    f.close();

    m_statusLabel->setText(
        QStringLiteral("已导出 %1 条事件: %2").arg(m_table->rowCount()).arg(path));
}

void ProcessBehaviorPage::onContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    m_table->selectRow(idx.row());

    QMenu menu(this);
    QAction* copyImageAction = menu.addAction(QStringLiteral("复制映像/路径"));
    connect(copyImageAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_table->item(m_table->currentRow(), 6))
            QGuiApplication::clipboard()->setText(it->text());
    });
    QAction* copyRowAction = menu.addAction(QStringLiteral("复制整行"));
    connect(copyRowAction, &QAction::triggered, this, [this]() {
        int r = m_table->currentRow();
        if (r < 0) return;
        QStringList cells;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto* it = m_table->item(r, c);
            cells << (it ? it->text() : QString());
        }
        QGuiApplication::clipboard()->setText(cells.join(QStringLiteral("\t")));
    });
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
