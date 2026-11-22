#include "ProcessPage.h"
#include "AppContext.h"
#include "ProcessPropertiesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QTimer>
#include <QScrollBar>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QSet>
#include <QStack>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QInputDialog>
#include <QStyle>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "psapi.lib")

// 进程信息列（树形框列索引）
enum ProcessColumn {
    Col_Name = 0,
    Col_PID,
    Col_PPID,
    Col_Cpu,
    Col_Memory,
    Col_Path,
    Col_Description,
    Col_Count
};

// 单个进程的临时信息
struct ProcInfo {
    QString name;
    QString path;
    quint32 pid = 0;
    quint32 ppid = 0;
    quint64 workingSet = 0;
    QString description;
    double cpuUsage = 0.0;
    bool hidden = false;  // 驱动可见但用户层不可见
};

ProcessPage::ProcessPage(QWidget* parent) : QWidget(parent)
{
    // 获取 CPU 核心数
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_cpuCoreCount = si.dwNumberOfProcessors;
    if (m_cpuCoreCount < 1) m_cpuCoreCount = 1;

    setupUi();
    setupContextMenu();

    // 自动刷新定时器
    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(2000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &ProcessPage::refreshProcesses);
    m_autoRefreshTimer->start();

    refreshProcesses();
}

ProcessPage::~ProcessPage() = default;

void ProcessPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // 工具栏
    auto* toolLayout = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    auto* expandAllBtn = new QPushButton(QStringLiteral("全部展开"), this);
    auto* collapseAllBtn = new QPushButton(QStringLiteral("全部折叠"), this);
    m_fastScan = new QCheckBox(QStringLiteral("快速扫描"), this);
    m_statusLabel = new QLabel(QStringLiteral("进程数: 0"), this);

    connect(refreshBtn, &QPushButton::clicked, this, &ProcessPage::onRefreshClicked);
    connect(expandAllBtn, &QPushButton::clicked, this, [this]() { m_tree->expandAll(); });
    connect(collapseAllBtn, &QPushButton::clicked, this, [this]() { m_tree->collapseAll(); });
    connect(m_fastScan, &QCheckBox::toggled, this, &ProcessPage::onFastScanToggled);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addWidget(expandAllBtn);
    toolLayout->addWidget(collapseAllBtn);
    toolLayout->addWidget(m_fastScan);
    toolLayout->addStretch();
    toolLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(toolLayout);

    // 进程树
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(Col_Count);
    m_tree->setHeaderLabels({
        QStringLiteral("进程名"),
        QStringLiteral("PID"),
        QStringLiteral("PPID"),
        QStringLiteral("CPU"),
        QStringLiteral("内存"),
        QStringLiteral("路径"),
        QStringLiteral("描述")
    });
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setItemsExpandable(true);
    // 2 秒自动刷新会整树重建, 开启动画时展开/收起动画与重建竞争,
    // 行会出现视觉堆叠/错位, 这里必须关闭动画
    m_tree->setAnimated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(Col_Name, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(Col_PID, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Col_PPID, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Col_Cpu, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Col_Memory, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Col_Path, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(Col_Description, QHeaderView::ResizeToContents);
    m_tree->header()->setHighlightSections(false);

    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ProcessPage::onContextMenu);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ProcessPage::onItemSelectionChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ProcessPage::onItemDoubleClicked);

    mainLayout->addWidget(m_tree);
}

void ProcessPage::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    auto* killAction = m_contextMenu->addAction(QStringLiteral("驱动结束进程"));
    connect(killAction, &QAction::triggered, this, &ProcessPage::onKillProcess);

    auto* forceKillAction = m_contextMenu->addAction(QStringLiteral("强制结束进程"));
    connect(forceKillAction, &QAction::triggered, this, &ProcessPage::onForceKillProcess);

    // HVM(Ring-1) 相关处置合并到一个展开子菜单，避免右键菜单过长
    auto* hvmMenu = m_contextMenu->addMenu(QStringLiteral("HVM 虚拟化处置"));
    auto* hvmKillAction = hvmMenu->addAction(QStringLiteral("HVM强杀(#UD)"));
    connect(hvmKillAction, &QAction::triggered, this, &ProcessPage::onHvmKillProcess);
    auto* hvmFreezeAction = hvmMenu->addAction(QStringLiteral("HVM冻结(#PF)"));
    connect(hvmFreezeAction, &QAction::triggered, this, &ProcessPage::onHvmFreezeProcess);
    auto* hvmResumeAction = hvmMenu->addAction(QStringLiteral("HVM恢复"));
    connect(hvmResumeAction, &QAction::triggered, this, &ProcessPage::onHvmResumeProcess);

    auto* suspendAction = m_contextMenu->addAction(QStringLiteral("挂起进程"));
    connect(suspendAction, &QAction::triggered, this, &ProcessPage::onSuspendProcess);

    auto* resumeAction = m_contextMenu->addAction(QStringLiteral("恢复进程"));
    connect(resumeAction, &QAction::triggered, this, &ProcessPage::onResumeProcess);

    m_contextMenu->addSeparator();

    auto* hideAction = m_contextMenu->addAction(QStringLiteral("隐藏进程"));
    connect(hideAction, &QAction::triggered, this, &ProcessPage::onHideProcess);

    auto* unHideAction = m_contextMenu->addAction(QStringLiteral("取消隐藏进程"));
    connect(unHideAction, &QAction::triggered, this, &ProcessPage::onUnHideProcess);

    m_contextMenu->addSeparator();

    auto* protectAction = m_contextMenu->addAction(QStringLiteral("保护进程"));
    connect(protectAction, &QAction::triggered, this, &ProcessPage::onProtectProcess);

    auto* unProtectAction = m_contextMenu->addAction(QStringLiteral("取消保护进程"));
    connect(unProtectAction, &QAction::triggered, this, &ProcessPage::onUnProtectProcess);

    auto* forceProtectAction = m_contextMenu->addAction(QStringLiteral("强力保护进程"));
    connect(forceProtectAction, &QAction::triggered, this, &ProcessPage::onForceProtectProcess);

    auto* unForceProtectAction = m_contextMenu->addAction(QStringLiteral("取消强力保护"));
    connect(unForceProtectAction, &QAction::triggered, this, &ProcessPage::onUnForceProtectProcess);

    m_contextMenu->addSeparator();

    auto* memKillAction = m_contextMenu->addAction(QStringLiteral("破坏进程内存"));
    connect(memKillAction, &QAction::triggered, this, &ProcessPage::onMemKillProcess);

    auto* setPPLAction = m_contextMenu->addAction(QStringLiteral("设置 PPL 保护"));
    connect(setPPLAction, &QAction::triggered, this, &ProcessPage::onSetPPL);

    auto* setCriticalAction = m_contextMenu->addAction(QStringLiteral("设置为关键进程"));
    connect(setCriticalAction, &QAction::triggered, this, &ProcessPage::onSetCritical);

    auto* unSetCriticalAction = m_contextMenu->addAction(QStringLiteral("取消关键进程"));
    connect(unSetCriticalAction, &QAction::triggered, this, &ProcessPage::onUnSetCritical);

    m_contextMenu->addSeparator();

    auto* openLocationAction = m_contextMenu->addAction(QStringLiteral("打开文件所在位置"));
    connect(openLocationAction, &QAction::triggered, this, &ProcessPage::onOpenFileLocation);

    auto* copyCmdLineAction = m_contextMenu->addAction(QStringLiteral("复制命令行"));
    connect(copyCmdLineAction, &QAction::triggered, this, &ProcessPage::onCopyCommandLine);

    auto* copyPathAction = m_contextMenu->addAction(QStringLiteral("复制文件路径"));
    connect(copyPathAction, &QAction::triggered, this, &ProcessPage::onCopyFilePath);

    m_contextMenu->addSeparator();

    auto* propsAction = m_contextMenu->addAction(QStringLiteral("属性"));
    connect(propsAction, &QAction::triggered, this, &ProcessPage::onShowProcessProperties);
}

// 获取（带缓存的）exe 图标
QIcon ProcessPage::iconForPath(const QString& path)
{
    if (path.isEmpty()) return QIcon();

    auto it = m_iconCache.constFind(path);
    if (it != m_iconCache.constEnd()) return it.value();

    // 使用 QFileIconProvider 获取文件关联图标（exe 文件会返回其内嵌图标）
    static QFileIconProvider provider;
    QIcon icon = provider.icon(QFileInfo(path));

    m_iconCache.insert(path, icon);
    return icon;
}

// 刷新开始前采样系统总时间和各进程 CPU 时间
void ProcessPage::updateCpuBeginSample()
{
    FILETIME ftIdle, ftKernel, ftUser;
    if (GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
        ULARGE_INTEGER total;
        total.LowPart = ftKernel.dwLowDateTime;
        total.HighPart = ftKernel.dwHighDateTime;
        m_lastTotalTime = total;
    }

    m_cpuSamples.clear();
}

// 计算单进程 CPU 占用百分比（相对于单核 100%）
double ProcessPage::cpuUsageForPid(quint32 pid)
{
    return 0.0;  // 在 refreshProcesses 中直接处理，这里保留接口
}

// 获取进程的 CPU 时间（Kernel + User）
static bool GetProcessCpuTimes(quint32 pid, ULARGE_INTEGER& kernel, ULARGE_INTEGER& user)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    BOOL ok = GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser);
    CloseHandle(hProc);
    if (!ok) return false;

    kernel.LowPart = ftKernel.dwLowDateTime;
    kernel.HighPart = ftKernel.dwHighDateTime;
    user.LowPart = ftUser.dwLowDateTime;
    user.HighPart = ftUser.dwHighDateTime;
    return true;
}

void ProcessPage::refreshProcesses()
{
    auto& ctx = AppContext::instance();
    bool fastScan = m_fastScan->isChecked();

    // ===== 1. 通过驱动 EPROCESSEnumProcess 获取所有进程 PID（含隐藏进程）=====
    QVector<quint32> driverPids;
    if (ctx.driverLoaded()) {
        driverPids = ctx.driver().enumProcesses();
    }

    // ===== 2. 通过 Toolhelp32 获取进程名/PPID（用户层可见进程）=====
    QHash<quint32, ProcInfo> procMap;
    QVector<quint32> pidOrder;
    QSet<quint32> toolhelpPids;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                ProcInfo info;
                info.pid = pe.th32ProcessID;
                info.ppid = pe.th32ParentProcessID;
                info.name = QString::fromWCharArray(pe.szExeFile);
                procMap.insert(info.pid, info);
                pidOrder.append(info.pid);
                toolhelpPids.insert(info.pid);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // ===== 3. 驱动枚举的 PID 中，Toolhelp32 看不到的就是隐藏进程 =====
    QVector<quint32> hiddenPids;
    for (quint32 pid : driverPids) {
        if (!toolhelpPids.contains(pid) && pid != 0) {
            ProcInfo info;
            info.pid = pid;
            info.ppid = 0;
            info.name = QString();  // 隐藏进程名未知
            info.hidden = true;
            procMap.insert(pid, info);
            pidOrder.append(pid);
            hiddenPids.append(pid);
        }
    }

    // 如果驱动未加载，回退到只用 Toolhelp32
    if (driverPids.isEmpty()) {
        // pidOrder 已经是 Toolhelp32 的结果
    }

    // ===== 4. 用驱动 IOCTL_GETPROCESSIMAGENAME 获取路径 =====
    if (!fastScan && ctx.driverLoaded()) {
        for (auto it = procMap.begin(); it != procMap.end(); ++it) {
            QString path = ctx.driver().getProcessDosImageName(it.value().pid);
            if (!path.isEmpty()) {
                it.value().path = path;
                // 如果进程名是空的（隐藏进程），从路径提取
                if (it.value().name.isEmpty()) {
                    it.value().name = QFileInfo(path).fileName();
                }
            }
        }
    }

    // ===== 5. 用 OpenProcess 补充内存/CPU 信息 =====
    if (!fastScan) {
        for (auto it = procMap.begin(); it != procMap.end(); ++it) {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, it.value().pid);
            if (hProc) {
                // 路径回退
                if (it.value().path.isEmpty()) {
                    wchar_t pathBuf[MAX_PATH] = {0};
                    DWORD pathLen = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, pathBuf, &pathLen)) {
                        it.value().path = QString::fromWCharArray(pathBuf);
                        if (it.value().name.isEmpty()) {
                            it.value().name = QFileInfo(it.value().path).fileName();
                        }
                    }
                }

                // 内存
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    it.value().workingSet = pmc.WorkingSetSize;
                }

                CloseHandle(hProc);
            }
        }

        // 文件描述
        for (auto it = procMap.begin(); it != procMap.end(); ++it) {
            if (!it.value().path.isEmpty()) {
                it.value().description = QFileInfo(it.value().path).baseName();
            }
        }
    }

    // ===== 6. 计算 CPU 占用（与上次采样对比）=====
    bool firstRefresh = m_cpuSamples.isEmpty();
    if (!fastScan) {
        FILETIME ftSysIdle, ftSysKernel, ftSysUser;
        bool hasSysTime = GetSystemTimes(&ftSysIdle, &ftSysKernel, &ftSysUser);
        ULARGE_INTEGER sysKernel, sysUser, sysTotal;
        sysKernel.LowPart = ftSysKernel.dwLowDateTime;
        sysKernel.HighPart = ftSysKernel.dwHighDateTime;
        sysUser.LowPart = ftSysUser.dwLowDateTime;
        sysUser.HighPart = ftSysUser.dwHighDateTime;
        sysTotal.QuadPart = sysKernel.QuadPart + sysUser.QuadPart;

        QHash<quint32, CpuSample> newSamples;
        for (auto it = procMap.begin(); it != procMap.end(); ++it) {
            ULARGE_INTEGER curKernel, curUser;
            if (GetProcessCpuTimes(it.value().pid, curKernel, curUser)) {
                CpuSample sample;
                sample.lastKernel = curKernel;
                sample.lastUser = curUser;
                sample.lastTime = sysTotal;
                newSamples.insert(it.value().pid, sample);

                // 与上次采样对比
                auto prevIt = m_cpuSamples.constFind(it.value().pid);
                if (prevIt != m_cpuSamples.constEnd() && hasSysTime) {
                    ULONGLONG procDelta = (curKernel.QuadPart - prevIt->lastKernel.QuadPart) +
                                          (curUser.QuadPart - prevIt->lastUser.QuadPart);
                    ULONGLONG totalDelta = sysTotal.QuadPart - m_lastTotalTime.QuadPart;
                    if (totalDelta > 0) {
                        // 单核 100% 基准
                        double usage = (double)procDelta / (double)totalDelta * 100.0;
                        it.value().cpuUsage = usage;
                    }
                }
            }
        }
        m_cpuSamples = newSamples;
        m_lastTotalTime = sysTotal;
    }

    // ===== 7. 保存当前选中与滚动位置 =====
    // 注意: PID 0 ([System Process]) 的选中状态也要恢复, 因此用
    // "是否有选中" 标记而不是 selectedPid != 0 判断
    bool hadSelection = m_tree->currentItem() != nullptr;
    quint32 selectedPid = currentPid();
    int scrollPos = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;

    // ===== 8. 记录展开状态 =====
    // 注意: PID 0 (System Idle/[System Process]) 也必须记录, 否则每次
    // 自动刷新(2秒)后该节点会被折叠回去 —— 这正是"展开后过两秒又
    // 自动折回去"的根源
    QSet<quint32> expandedPids;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_tree->topLevelItem(i);
        QStack<QTreeWidgetItem*> stack;
        stack.push(it);
        while (!stack.isEmpty()) {
            QTreeWidgetItem* cur = stack.pop();
            if (cur->isExpanded()) {
                bool ok = false;
                quint32 pid = cur->text(Col_PID).toUInt(&ok);
                if (ok) expandedPids.insert(pid);
            }
            for (int j = 0; j < cur->childCount(); ++j) {
                stack.push(cur->child(j));
            }
        }
    }
    // 记录隐藏树节点是否展开
    bool hiddenTreeExpanded = false;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_tree->topLevelItem(i);
        if (it->data(Col_PID, Qt::UserRole).toString() == QStringLiteral("hidden_root")) {
            hiddenTreeExpanded = it->isExpanded();
            break;
        }
    }

    // ===== 9. 构建树 =====
    // 2 秒定时刷新会整树重建: 重建期间禁用重绘, 否则中间布局与展开/选中
    // 恢复交错渲染, 行会出现视觉堆叠/错位
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    QHash<quint32, QTreeWidgetItem*> itemMap;

    // 先创建所有普通进程 item
    for (const auto& pid : pidOrder) {
        const ProcInfo& info = procMap.value(pid);
        if (info.hidden) continue;  // 隐藏进程单独处理

        auto* item = new QTreeWidgetItem();
        item->setText(Col_Name, info.name.isEmpty() ? QStringLiteral("*Hid or Exited*") : info.name);
        item->setText(Col_PID, QString::number(info.pid));
        item->setText(Col_PPID, QString::number(info.ppid));
        item->setText(Col_Cpu, info.cpuUsage > 0.001
            ? QString::number(info.cpuUsage, 'f', 1) + QStringLiteral("%")
            : QStringLiteral("0%"));
        item->setText(Col_Memory, info.workingSet > 0
            ? (QString::number(info.workingSet / 1024) + QStringLiteral(" KB"))
            : QString());
        item->setText(Col_Path, info.path);
        item->setText(Col_Description, info.description);

        // 图标
        if (!info.path.isEmpty()) {
            item->setIcon(Col_Name, iconForPath(info.path));
        }

        // 空名进程红色标记
        if (info.name.isEmpty()) {
            for (int c = 0; c < Col_Count; ++c) {
                item->setForeground(c, Qt::red);
            }
        }

        itemMap.insert(pid, item);
    }

    // 建立父子关系（普通进程）
    DWORD myPid = GetCurrentProcessId();
    for (const auto& pid : pidOrder) {
        const ProcInfo& info = procMap.value(pid);
        if (info.hidden) continue;
        QTreeWidgetItem* item = itemMap.value(pid);
        if (!item) continue;

        QTreeWidgetItem* parentItem = itemMap.value(info.ppid);
        if (parentItem && parentItem != item) {
            parentItem->addChild(item);
        } else {
            m_tree->addTopLevelItem(item);
        }

        // 高亮当前进程
        if (pid == myPid) {
            for (int c = 0; c < Col_Count; ++c) {
                item->setBackground(c, QColor(173, 216, 230));
            }
        }
    }

    // ===== 10. 隐藏进程单独树 =====
    if (!hiddenPids.isEmpty()) {
        auto* hiddenRoot = new QTreeWidgetItem();
        hiddenRoot->setText(Col_Name, QStringLiteral("【隐藏进程】(无法获取父进程)"));
        hiddenRoot->setForeground(Col_Name, Qt::darkRed);
        QFont boldFont = hiddenRoot->font(Col_Name);
        boldFont.setBold(true);
        hiddenRoot->setFont(Col_Name, boldFont);
        hiddenRoot->setData(Col_PID, Qt::UserRole, QStringLiteral("hidden_root"));
        hiddenRoot->setIcon(Col_Name, style()->standardIcon(QStyle::SP_MessageBoxWarning));

        int hiddenCount = 0;
        for (quint32 pid : hiddenPids) {
            const ProcInfo& info = procMap.value(pid);
            auto* item = new QTreeWidgetItem();
            item->setText(Col_Name, info.name.isEmpty() ? QStringLiteral("(未知)") : info.name);
            item->setText(Col_PID, QString::number(info.pid));
            item->setText(Col_PPID, QStringLiteral("N/A"));
            item->setText(Col_Cpu, info.cpuUsage > 0.001
                ? QString::number(info.cpuUsage, 'f', 1) + QStringLiteral("%")
                : QStringLiteral("0%"));
            item->setText(Col_Memory, info.workingSet > 0
                ? (QString::number(info.workingSet / 1024) + QStringLiteral(" KB"))
                : QString());
            item->setText(Col_Path, info.path);
            item->setText(Col_Description, info.description);

            // 红色标记
            for (int c = 0; c < Col_Count; ++c) {
                item->setForeground(c, Qt::red);
            }

            if (!info.path.isEmpty()) {
                item->setIcon(Col_Name, iconForPath(info.path));
            }

            hiddenRoot->addChild(item);
            itemMap.insert(pid, item);
            hiddenCount++;
        }

        m_tree->addTopLevelItem(hiddenRoot);
        // 隐藏树默认展开（让用户立刻看到隐藏进程）
        hiddenRoot->setExpanded(true);
    }

    // ===== 11. 恢复展开状态 =====
    if (firstRefresh) {
        // 首次刷新全部展开
        m_tree->expandAll();
    }
    else {
        for (auto it = itemMap.constBegin(); it != itemMap.constEnd(); ++it) {
            if (expandedPids.contains(it.key())) {
                it.value()->setExpanded(true);
            }
        }
    }

    // ===== 12. 更新状态栏 =====
    int totalVisible = pidOrder.size() - hiddenPids.size();
    int totalHidden = hiddenPids.size();
    QString statusText;
    if (totalHidden > 0) {
        statusText = QStringLiteral("进程数: %1 (含 %2 个隐藏)").arg(pidOrder.size()).arg(totalHidden);
    } else {
        statusText = QStringLiteral("进程数: %1").arg(pidOrder.size());
    }
    m_statusLabel->setText(statusText);

    // ===== 13. 恢复选中与滚动位置 =====
    // 先恢复选中, 再统一恢复滚动位置; 重建期间重绘已禁用, 中间滚动不会绘制
    if (hadSelection && itemMap.contains(selectedPid)) {
        m_tree->setCurrentItem(itemMap.value(selectedPid));
    }
    if (m_tree->verticalScrollBar()) {
        m_tree->verticalScrollBar()->setValue(scrollPos);
    }

    // 重建完成, 恢复重绘
    m_tree->setUpdatesEnabled(true);
}

void ProcessPage::onRefreshClicked()
{
    refreshProcesses();
}

void ProcessPage::onFastScanToggled(bool checked)
{
    Q_UNUSED(checked);
    refreshProcesses();
}

quint32 ProcessPage::currentPid() const
{
    auto* item = m_tree->currentItem();
    if (!item) return 0;
    // 跳过隐藏树根节点
    if (item->data(Col_PID, Qt::UserRole).toString() == QStringLiteral("hidden_root")) {
        return 0;
    }
    bool ok = false;
    quint32 pid = item->text(Col_PID).toUInt(&ok);
    return ok ? pid : 0;
}

QString ProcessPage::currentProcessName() const
{
    auto* item = m_tree->currentItem();
    return item ? item->text(Col_Name) : QString();
}

QString ProcessPage::currentProcessPath() const
{
    auto* item = m_tree->currentItem();
    return item ? item->text(Col_Path) : QString();
}

void ProcessPage::onContextMenu(const QPoint& pos)
{
    auto* item = m_tree->itemAt(pos);
    if (!item) return;
    // 隐藏树根节点不弹菜单
    if (item->data(Col_PID, Qt::UserRole).toString() == QStringLiteral("hidden_root")) return;
    m_tree->setCurrentItem(item);
    m_contextMenu->exec(m_tree->viewport()->mapToGlobal(pos));
}

void ProcessPage::onItemSelectionChanged()
{
    // 可在此更新按钮状态
}

void ProcessPage::onItemDoubleClicked(QTreeWidgetItem* item, int col)
{
    Q_UNUSED(col);
    Q_UNUSED(item);
    onShowProcessProperties();
}

// 检测进程是否仍在运行（先声明供 killProcessWithFallback 使用）
static bool IsProcessRunning(quint32 pid);

void ProcessPage::killProcessWithFallback(quint32 pid)
{
    auto& ctx = AppContext::instance();

    // 驱动结束进程：ZwKillProcess -> PspTerminateProcess -> ApcTerminateProcess
    // 每一步之后都要确认进程是否已经退出：对正在退出/已退出的进程继续
    // 插入终止 APC 会让 APC 落在销毁中的线程栈上，直接蓝屏。
    if (ctx.driverLoaded()) {
        ctx.driver().zwKillProcess(pid);
        Sleep(400);
        if (!IsProcessRunning(pid)) return;

        ctx.driver().pspTerminateProcess(pid);
        Sleep(400);
        if (!IsProcessRunning(pid)) return;

        ctx.driver().apcTerminateProcess(pid);
    } else {
        // 后备：用 TerminateProcess
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }
}

// 检测进程是否仍在运行
static bool IsProcessRunning(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

// 检测进程是否有控制台（命令行）窗口
static bool HasConsoleWindow(quint32 pid)
{
    struct EnumData { quint32 pid; bool found; } data{pid, false};
    EnumWindows([](HWND hwnd, LPARAM lp)->BOOL {
        auto* d = reinterpret_cast<EnumData*>(lp);
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == d->pid) {
            wchar_t cls[64] = {0};
            GetClassNameW(hwnd, cls, 64);
            // ConsoleWindowClass 是 Windows 控制台窗口的类名
            if (wcscmp(cls, L"ConsoleWindowClass") == 0) {
                d->found = true;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    return data.found;
}

// 向进程的所有控制台窗口发送 Ctrl 信号
static void SendConsoleCtrlSignals(quint32 pid)
{
    struct EnumData { quint32 pid; } data{pid};
    EnumWindows([](HWND hwnd, LPARAM lp)->BOOL {
        auto* d = reinterpret_cast<EnumData*>(lp);
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == d->pid) {
            wchar_t cls[64] = {0};
            GetClassNameW(hwnd, cls, 64);
            if (wcscmp(cls, L"ConsoleWindowClass") == 0) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                // 控制台窗口收到 WM_CLOSE 会触发 Ctrl+Break
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    // 也可以用 GenerateConsoleCtrlEvent，但仅对同 console 的子进程有效
    // 这里用 PostMessage 方式作为通用方案
    // 额外尝试发送 CTRL_C_EVENT/CTRL_BREAK_EVENT
    FreeConsole();
    if (AttachConsole(pid)) {
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
        FreeConsole();
    }
}

// 回调：向进程的所有窗口发送关闭消息
BOOL CALLBACK ProcessPage::sendCloseMsgToWnd(HWND hwnd, LPARAM lParam)
{
    DWORD pid = (DWORD)lParam;
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == pid) {
        // 依次发送各种关闭消息
        PostMessageW(hwnd, WM_DESTROY, 0, 0);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        PostMessageW(hwnd, WM_NCDESTROY, 0, 0);
        PostMessageW(hwnd, WM_QUERYENDSESSION, 0, 0);
    }
    return TRUE;
}

bool ProcessPage::forceKillProcess(quint32 pid)
{
    // 1. 先尝试驱动 zwKillProcess
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        ctx.driver().zwKillProcess(pid);
        Sleep(300);
        if (!IsProcessRunning(pid)) return true;
    }

    // 2. 检测是否有命令行窗口，有则发 Ctrl 信号
    if (HasConsoleWindow(pid)) {
        SendConsoleCtrlSignals(pid);
        Sleep(500);
        if (!IsProcessRunning(pid)) return true;
    }

    // 3. 向所有窗口发送关闭消息
    EnumWindows(sendCloseMsgToWnd, (LPARAM)pid);
    Sleep(500);
    if (!IsProcessRunning(pid)) return true;

    // 4. 进程还在，走驱动结束进程
    killProcessWithFallback(pid);
    Sleep(400);
    return !IsProcessRunning(pid);
}

void ProcessPage::onForceKillProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;

    QString name = currentProcessName();
    if (name.isEmpty()) name = QString::number(pid);

    auto ret = QMessageBox::warning(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定强制结束进程: %1 吗?\nPID: %2").arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    forceKillProcess(pid);
    QTimer::singleShot(500, this, &ProcessPage::refreshProcesses);
}

void ProcessPage::onKillProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;

    QString name = currentProcessName();
    if (name.isEmpty()) name = QString::number(pid);

    auto ret = QMessageBox::warning(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定结束进程: %1 吗?\nPID: %2").arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (pid != 0) {
        killProcessWithFallback(pid);
        QTimer::singleShot(500, this, &ProcessPage::refreshProcesses);
    }
}

// 把 HVM 处置结果翻译成一句人话。
static QString hvmProcessStatusText(const KernelDriver::HvProcessResult& r, quint32 pid)
{
    if (r.status == 0) {
        if (r.rows.isEmpty()) {
            return QStringLiteral("处置已下达 PID %1（处置表为空，请重新下达）").arg(pid);
        }
        const auto& row = r.rows.first();
        QString where = QStringLiteral("线性 0x%1 -> 物理 0x%2")
            .arg(row.guestLinearAddress, 0, 16)
            .arg(row.guestPhysicalAddress, 0, 16);
        return QStringLiteral("处置已下达 PID %1（%2），拦截次数 %3")
            .arg(pid)
            .arg(where)
            .arg(row.interceptCount);
    }
    QString reason;
    switch (r.status) {
    case 3:  reason = QStringLiteral("常驻未停止（驱动会自动停止/恢复，出现此状态说明停止失败）"); break;
    case 4:  reason = QStringLiteral("目标进程解析失败（已退出或无地址空间）"); break;
    case 7:  reason = QStringLiteral("该进程已有处置在生效"); break;
    case 8:  reason = QStringLiteral("需要 CR3 跟踪（先启动一次 hypervisor 再试）"); break;
    case 9:  reason = QStringLiteral("需要 EPTP 切换支持（当前 CPU 不支持执行-only EPT 叶）"); break;
    case 10: reason = QStringLiteral("目标页翻译失败（进程可能正在退出）"); break;
    case 11: reason = QStringLiteral("目标受保护（Idle/System/驱动自身）"); break;
    case 12: reason = QStringLiteral("HVM 资源未准备"); break;
    default: reason = QStringLiteral("状态码 %1 (NTSTATUS 0x%2)")
                 .arg(r.status).arg((quint32)r.lastStatus, 8, 16, QLatin1Char('0')); break;
    }
    return QStringLiteral("HVM 处置失败：") + reason;
}

void ProcessPage::onHvmKillProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    QString name = currentProcessName();
    if (name.isEmpty()) name = QString::number(pid);

    auto ret = QMessageBox::warning(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定用 HVM 强杀进程: %1 吗?\nPID: %2\n\nRing-1 拒绝执行其关键页，命中后注入 #UD，"
								"由系统走未处理异常结束进程。对驱动级保护进程有效，不可逆。\n\n"
								"⚠️ 实验性功能：Intel 平台上处置命中瞬间可能整机死机（上游 KSword 同样存在，"
								"该路径仅在 AMD 上验证过）。请保存所有工作后再继续！").arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& driver = AppContext::instance().driver();
    KernelDriver::HvProcessResult r;
    const bool ok = driver.hvmTerminateProcess(pid, 0, &r);
    QMessageBox::information(this, QStringLiteral("EFCHSword"),
        ok ? hvmProcessStatusText(r, pid)
           : QStringLiteral("IOCTL 失败（驱动未加载或版本过旧，Win32 错误 %1）")
               .arg(driver.lastIoError()));
    QTimer::singleShot(800, this, &ProcessPage::refreshProcesses);
}

void ProcessPage::onHvmFreezeProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    QString name = currentProcessName();
    if (name.isEmpty()) name = QString::number(pid);

    auto ret = QMessageBox::warning(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定用 HVM 冻结进程: %1 吗?\nPID: %2\n\nRing-1 拒绝执行其关键页，"
								"命中后注入 #PF，进程原地卡死；可用 HVM恢复 撤销。\n\n"
								"⚠️ 实验性功能：Intel 平台上处置命中瞬间可能整机死机（上游 KSword 同样存在，"
								"该路径仅在 AMD 上验证过）。请保存所有工作后再继续！").arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& driver = AppContext::instance().driver();
    KernelDriver::HvProcessResult r;
    const bool ok = driver.hvmFreezeProcess(pid, 0, &r);
    QMessageBox::information(this, QStringLiteral("EFCHSword"),
        ok ? hvmProcessStatusText(r, pid)
           : QStringLiteral("IOCTL 失败（驱动未加载或版本过旧，Win32 错误 %1）")
               .arg(driver.lastIoError()));
}

void ProcessPage::onHvmResumeProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;

    auto& driver = AppContext::instance().driver();
    KernelDriver::HvProcessResult r;
    const bool ok = driver.hvmResumeProcess(pid, &r);
    QMessageBox::information(this, QStringLiteral("EFCHSword"),
        ok ? QStringLiteral("PID %1 的 HVM 处置已撤销，地址空间切回基座后恢复执行。").arg(pid)
           : hvmProcessStatusText(r, pid));
}

void ProcessPage::onSuspendProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    if (!ctx.driver().suspendProcess(pid)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("挂起进程失败"));
    }
}

void ProcessPage::onResumeProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    if (!ctx.driver().resumeProcess(pid)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("恢复进程失败"));
    }
}

void ProcessPage::onHideProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("隐藏进程可能导致蓝屏!确定隐藏 PID:%1 吗?").arg(pid),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    ctx.driver().hideProcess(pid);
}

void ProcessPage::onUnHideProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().unHideProcess(pid);
}

void ProcessPage::onProtectProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().protectProcess(pid);
}

void ProcessPage::onUnProtectProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().unProtectProcess(pid);
}

void ProcessPage::onForceProtectProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().forceProtectProcess(pid);
}

void ProcessPage::onUnForceProtectProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().unForceProtectProcess(pid);
}

void ProcessPage::onMemKillProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("破坏进程内存数据可能导致进程崩溃!确定继续吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    ctx.driver().memKillProcess(pid);
}

void ProcessPage::onSetPPL()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    QStringList items = {
        QStringLiteral("None (0x00)"), QStringLiteral("PPL_Authenticode (0x11)"),
        QStringLiteral("PPL_CodeGen (0x21)"), QStringLiteral("PPL_Antimalware (0x31)"),
        QStringLiteral("PPL_Lsa (0x41)"), QStringLiteral("PPL_Windows (0x51)"),
        QStringLiteral("PPL_WinTcb (0x61)"), QStringLiteral("PPL_WinSystem (0x71)"),
        QStringLiteral("PP_Authenticode (0x12)"), QStringLiteral("PP_CodeGen (0x22)"),
        QStringLiteral("PP_Antimalware (0x32)"), QStringLiteral("PP_Lsa (0x42)"),
        QStringLiteral("PP_Windows (0x52)"), QStringLiteral("PP_WinTcb (0x62)"),
        QStringLiteral("PP_WinSystem (0x72)")
    };
    quint32 levels[] = {
        PPL_None, PPL_Authenticode, PPL_CodeGen, PPL_Antimalware,
        PPL_Lsa, PPL_Windows, PPL_WinTcb, PPL_WinSystem,
        PP_Authenticode, PP_CodeGen, PP_Antimalware, PP_Lsa,
        PP_Windows, PP_WinTcb, PP_WinSystem
    };

    bool ok = false;
    QString sel = QInputDialog::getItem(this, QStringLiteral("设置 PPL"),
        QStringLiteral("选择保护级别:"), items, 0, false, &ok);
    if (!ok) return;
    int idx = items.indexOf(sel);
    if (idx < 0) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("设置 PPL 可能导致蓝屏!确定继续吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().setPPL(pid, levels[idx]);
}

void ProcessPage::onSetCritical()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().setProcessCritical(pid, 1);
}

void ProcessPage::onUnSetCritical()
{
    quint32 pid = currentPid();
    if (pid == 0) return;
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().setProcessCritical(pid, 0);
}

void ProcessPage::onOpenFileLocation()
{
    QString path = currentProcessPath();
    if (path.isEmpty() || path == QStringLiteral("*Hid or Exited*")) return;

    QString cmd = QStringLiteral("explorer.exe /select,\"%1\"").arg(path);
    QProcess::startDetached(cmd);
}

void ProcessPage::onCopyCommandLine()
{
    // 当前树形框未保留命令行列；保留接口供未来扩展
    auto* item = m_tree->currentItem();
    if (!item) return;
    // 复制进程名作为后备
    QGuiApplication::clipboard()->setText(item->text(Col_Name));
}

void ProcessPage::onCopyFilePath()
{
    QString path = currentProcessPath();
    if (!path.isEmpty()) {
        QGuiApplication::clipboard()->setText(path);
    }
}

void ProcessPage::onShowProcessProperties()
{
    quint32 pid = currentPid();
    if (pid == 0) return;

    auto* dlg = new ProcessPropertiesDialog(pid, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}
