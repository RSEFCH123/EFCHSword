#include "ProcessPropertiesDialog.h"
#include "NetTable.h"   // 必须在 AppContext.h(windows.h) 之前: winsock2 顺序
#include "AppContext.h"
#include "FilePropertiesDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QScrollArea>
#include <QInputDialog>
#include <memory>

#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <psapi.h>

// 用户层特权查询：OpenProcess + OpenProcessToken + GetTokenInformation。
// 修复内核 ZwOpenProcessToken 路径报 998 (ERROR_NOACCESS) 的问题。
static bool getUserModePrivileges(quint32 pid, ArkPrivilegeArray& out)
{
    out.count = 0;
    out.reserved = 0;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (!hProcess) return false;

    HANDLE hToken = NULL;
    bool ok = false;
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        DWORD needed = 0;
        GetTokenInformation(hToken, TokenPrivileges, NULL, 0, &needed);
        if (needed > 0 && needed <= 64 * 1024) {
            QByteArray buf(int(needed), 0);
            if (GetTokenInformation(hToken, TokenPrivileges, buf.data(), needed, &needed)) {
                PTOKEN_PRIVILEGES tp = (PTOKEN_PRIVILEGES)buf.data();
                DWORD n = tp->PrivilegeCount;
                if (n > 64) n = 64;
                out.count = n;
                for (DWORD i = 0; i < n; ++i) {
                    out.privileges[i].luid =
                        (quint64(tp->Privileges[i].Luid.HighPart) << 32) |
                        quint64(tp->Privileges[i].Luid.LowPart);
                    out.privileges[i].attributes = tp->Privileges[i].Attributes;
                    out.privileges[i].reserved = 0;
                }
                ok = true;
            }
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProcess);
    return ok;
}

// 获取进程命令行
static QString getProcessCommandLine(quint32 pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return QString();

    // 简化实现：通过 PEB 读取命令行较复杂，这里返回空
    // 完整实现需要 NtQueryInformationProcess + 读取 PEB->ProcessParameters->CommandLine
    CloseHandle(hProc);
    return QString();
}

// 获取进程路径
static QString getProcessPath(quint32 pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return QString();
    wchar_t path[MAX_PATH] = {0};
    DWORD len = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(hProc, 0, path, &len)) {
        result = QString::fromWCharArray(path);
    }
    CloseHandle(hProc);
    return result;
}

// 获取进程名
static QString getProcessName(quint32 pid)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return QString();
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    QString result;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                result = QString::fromWCharArray(pe.szExeFile);
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

// 获取父进程 PID
static quint32 getParentPid(quint32 pid)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    quint32 parent = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return parent;
}

// 枚举进程线程
// 优先使用驱动 IOCTL_ENUMTHREADBYPROCESS（可发现 PspCidTable 隐藏线程，并返回 ETHREAD 地址），
// 驱动不可用时回退到 ToolHelp32（无 ETHREAD 地址，ethread 字段为 0）
static QVector<ThreadInfo> enumThreads(quint32 pid)
{
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        QVector<ThreadInfo> threads = ctx.driver().enumThreadsByProcess(pid);
        if (!threads.isEmpty()) return threads;
        // 驱动返回空：可能是进程已退出或 IOCTL 失败，回退到 ToolHelp32
    }

    QVector<ThreadInfo> threads;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return threads;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                ThreadInfo info;
                info.tid = te.th32ThreadID;
                info.ethread = 0;  // ToolHelp32 无法获取内核地址
                threads.append(info);
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    return threads;
}

// 枚举进程窗口的回调
struct EnumWindowData {
    quint32 pid;
    QVector<HWND> hwnds;
};

static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* data = reinterpret_cast<EnumWindowData*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->pid && IsWindowVisible(hwnd)) {
        data->hwnds.append(hwnd);
    }
    return TRUE;
}

static QVector<HWND> enumProcessWindows(quint32 pid)
{
    EnumWindowData data;
    data.pid = pid;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.hwnds;
}

// ==================== ARK 辅助 ====================

// 特权 LUID -> 名称（低32位是特权编号）
static QString privilegeNameFromLuid(quint64 luid)
{
    // 常见特权快速映射
    static const struct { quint32 low; const char* name; } kPrivMap[] = {
        {2, "SeCreateTokenPrivilege"}, {3, "SeAssignPrimaryTokenPrivilege"},
        {4, "SeLockMemoryPrivilege"}, {5, "SeIncreaseQuotaPrivilege"},
        {6, "SeMachineAccountPrivilege"}, {7, "SeTcbPrivilege"},
        {8, "SeSecurityPrivilege"}, {9, "SeTakeOwnershipPrivilege"},
        {10, "SeLoadDriverPrivilege"}, {11, "SeSystemProfilePrivilege"},
        {12, "SeSystemtimePrivilege"}, {13, "SeProfileSingleProcessPrivilege"},
        {14, "SeIncreaseBasePriorityPrivilege"}, {15, "SeCreatePagefilePrivilege"},
        {16, "SeCreatePermanentPrivilege"}, {17, "SeBackupPrivilege"},
        {18, "SeRestorePrivilege"}, {19, "SeShutdownPrivilege"},
        {20, "SeDebugPrivilege"}, {21, "SeAuditPrivilege"},
        {22, "SeSystemEnvironmentPrivilege"}, {23, "SeChangeNotifyPrivilege"},
        {24, "SeRemoteShutdownPrivilege"}, {25, "SeUndockPrivilege"},
        {26, "SeSyncAgentPrivilege"}, {27, "SeEnableDelegationPrivilege"},
        {28, "SeManageVolumePrivilege"}, {29, "SeImpersonatePrivilege"},
        {30, "SeCreateGlobalPrivilege"}, {31, "SeTrustedCredManAccessPrivilege"},
        {32, "SeRelabelPrivilege"}, {33, "SeIncreaseWorkingSetPrivilege"},
        {34, "SeTimeZonePrivilege"}, {35, "SeCreateSymbolicLinkPrivilege"},
        {36, "SeDelegateSessionUserImpersonatePrivilege"},
    };
    quint32 low = (quint32)(luid & 0xFFFFFFFFULL);
    for (const auto& p : kPrivMap) {
        if (p.low == low) return QString::fromLatin1(p.name);
    }
    // 尝试系统 API
    LUID l;
    l.LowPart = low;
    l.HighPart = 0;
    wchar_t nameBuf[128] = {0};
    DWORD cch = 128;
    if (LookupPrivilegeNameW(nullptr, &l, nameBuf, &cch)) {
        return QString::fromWCharArray(nameBuf, (int)cch);
    }
    return QStringLiteral("Privilege #%1").arg(low);
}

// 优先级类数值 -> 名称（PROCESS_PRIORITY_CLASS_*，与驱动一致）
static QString priorityClassName(quint32 cls)
{
    switch (cls & 0xFF) {
    case 0: return QStringLiteral("无效");
    case 1: return QStringLiteral("空闲 (Idle)");
    case 2: return QStringLiteral("普通 (Normal)");
    case 3: return QStringLiteral("高 (High)");
    case 4: return QStringLiteral("实时 (Realtime)");
    case 5: return QStringLiteral("低于正常 (Below Normal)");
    case 6: return QStringLiteral("高于正常 (Above Normal)");
    default: return QString::number(cls);
    }
}

// 键盘 MakeCode -> 可读键名
static QString keyNameFromMakeCode(quint16 makeCode, quint16 flags)
{
    if (makeCode == 0) return QStringLiteral("<无>");
    UINT vk = MapVirtualKeyW(makeCode, MAPVK_VSC_TO_VK_EX);
    if (flags & 2) { // E0 前缀（右 Ctrl / 右 Alt / 方向键等）
        if (makeCode == 0x1D) return QStringLiteral("Right Ctrl");
        if (makeCode == 0x38) return QStringLiteral("Right Alt");
    }
    wchar_t keyBuf[64] = {0};
    LONG lParam = (LONG)(makeCode << 16);
    if (GetKeyNameTextW(lParam, keyBuf, 64) > 0) {
        return QString::fromWCharArray(keyBuf);
    }
    if (vk) return QStringLiteral("VK 0x%1").arg(vk, 2, 16, QChar('0'));
    return QStringLiteral("SC 0x%1").arg(makeCode, 2, 16, QChar('0'));
}

// 虚拟键码 -> 可读键名（注册热键显示用）
static QString vkDisplayName(quint32 vk)
{
    if (vk == 0) return QStringLiteral("<无>");
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    wchar_t keyBuf[64] = {0};
    if (sc) {
        LONG lParam = (LONG)((LONG_PTR)sc << 16);
        if (GetKeyNameTextW(lParam, keyBuf, 64) > 0) {
            return QString::fromWCharArray(keyBuf);
        }
    }
    return QStringLiteral("VK 0x%1").arg(vk, 2, 16, QChar('0'));
}

// 修饰键位 -> 显示文本
static QString modifiersDisplayName(quint32 mods)
{
    QStringList parts;
    if (mods & 0x2) parts << QStringLiteral("Ctrl");
    if (mods & 0x1) parts << QStringLiteral("Alt");
    if (mods & 0x4) parts << QStringLiteral("Shift");
    if (mods & 0x8) parts << QStringLiteral("Win");
    if (mods & 0x4000) parts << QStringLiteral("(不重复)");
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" + "));
}

// 获取进程名（用于占用/句柄显示）
static QString pidToName(quint32 pid)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return QString();
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    QString result;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                result = QString::fromWCharArray(pe.szExeFile);
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

// 内存保护常量 -> 字符串
static QString memProtectToString(quint32 protect)
{
    static const struct { quint32 v; const char* s; } kMap[] = {
        {0x01, "NOACCESS"}, {0x02, "READONLY"}, {0x04, "READWRITE"},
        {0x08, "WRITECOPY"}, {0x10, "EXECUTE"}, {0x20, "EXECUTE_READ"},
        {0x40, "EXECUTE_READWRITE"}, {0x80, "EXECUTE_WRITECOPY"},
        {0x100, "GUARD"}, {0x200, "NOACCESS+GUARD"},
    };
    QString s;
    for (const auto& e : kMap) {
        if ((protect & 0xFF) == e.v || protect == e.v) {
            s = QString::fromLatin1(e.s);
            break;
        }
    }
    if (s.isEmpty()) s = QStringLiteral("0x%1").arg(protect, 0, 16);
    if (protect & 0x100) s += QStringLiteral("+GUARD");
    if (protect & 0x400) s += QStringLiteral("+NOCACHE");
    return s;
}

ProcessPropertiesDialog::ProcessPropertiesDialog(quint32 pid, QWidget* parent)
    : QDialog(parent), m_pid(pid)
{
    setupUi();
    loadProcessInfo();
    loadThreads();
    loadWindows();
}

void ProcessPropertiesDialog::setupUi()
{
    setWindowTitle(QStringLiteral("进程属性"));
    setMinimumSize(600, 500);
    resize(820, 660);

    auto* mainLayout = new QVBoxLayout(this);

    // 标签页占满全窗口
    auto* tabWidget = new QTabWidget(this);

    // ===== 基本信息 tab =====
    auto* infoTab = new QWidget(this);
    auto* infoTabLayout = new QVBoxLayout(infoTab);
    infoTabLayout->setContentsMargins(0, 0, 0, 0);
    auto* infoScroll = new QScrollArea(infoTab);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    auto* infoContainer = new QWidget();
    auto* infoContainerLayout = new QVBoxLayout(infoContainer);

    auto* infoGroup = new QGroupBox(QStringLiteral("基本信息"), infoContainer);
    auto* infoLayout = new QFormLayout(infoGroup);

    m_nameLabel = new QLabel(infoGroup);
    m_synopsisLabel = new QLabel(infoGroup);
    m_verLabel = new QLabel(infoGroup);
    m_parentLabel = new QLabel(infoGroup);
    m_startTimeLabel = new QLabel(infoGroup);
    m_userLabel = new QLabel(infoGroup);
    m_imageLabel = new QLabel(infoGroup);
    m_filePathEdit = new QLineEdit(infoGroup);
    m_filePathEdit->setReadOnly(true);
    m_cmdLineEdit = new QLineEdit(infoGroup);
    m_cmdLineEdit->setReadOnly(true);

    infoLayout->addRow(QStringLiteral("进程名:"), m_nameLabel);
    infoLayout->addRow(QStringLiteral("描述:"), m_synopsisLabel);
    infoLayout->addRow(QStringLiteral("版本:"), m_verLabel);
    infoLayout->addRow(QStringLiteral("父进程:"), m_parentLabel);
    infoLayout->addRow(QStringLiteral("启动时间:"), m_startTimeLabel);
    infoLayout->addRow(QStringLiteral("用户:"), m_userLabel);
    infoLayout->addRow(QStringLiteral("镜像:"), m_imageLabel);
    infoLayout->addRow(QStringLiteral("文件目录:"), m_filePathEdit);
    infoLayout->addRow(QStringLiteral("命令行:"), m_cmdLineEdit);

    infoContainerLayout->addWidget(infoGroup);

    // PPL 设置
    auto* pplGroup = new QGroupBox(QStringLiteral("PPL 保护"), infoContainer);
    auto* pplLayout = new QHBoxLayout(pplGroup);
    m_pplCombo = new QComboBox(pplGroup);
    m_pplCombo->addItems({
        QStringLiteral("None (0x00)"), QStringLiteral("PP_Authenticode (0x12)"),
        QStringLiteral("PP_CodeGen (0x22)"), QStringLiteral("PP_Antimalware (0x32)"),
        QStringLiteral("PP_Lsa (0x42)"), QStringLiteral("PP_Windows (0x52)"),
        QStringLiteral("PP_WinTcb (0x62)"), QStringLiteral("PP_WinSystem (0x72)"),
        QStringLiteral("PPL_Authenticode (0x11)"), QStringLiteral("PPL_CodeGen (0x21)"),
        QStringLiteral("PPL_Antimalware (0x31)"), QStringLiteral("PPL_Lsa (0x41)"),
        QStringLiteral("PPL_Windows (0x51)"), QStringLiteral("PPL_WinTcb (0x61)"),
        QStringLiteral("PPL_WinSystem (0x71)")
    });
    m_pplSetBtn = new QPushButton(QStringLiteral("设置"), pplGroup);
    connect(m_pplSetBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onSetPPL);
    pplLayout->addWidget(m_pplCombo, 1);
    pplLayout->addWidget(m_pplSetBtn);
    infoContainerLayout->addWidget(pplGroup);
    infoContainerLayout->addStretch();

    infoScroll->setWidget(infoContainer);
    infoTabLayout->addWidget(infoScroll);
    tabWidget->addTab(infoTab, QStringLiteral("基本信息"));

    // 线程列表
    m_threadTable = new QTableWidget(this);
    m_threadTable->setColumnCount(2);
    m_threadTable->setHorizontalHeaderLabels({QStringLiteral("TID"), QStringLiteral("ETHREAD")});
    m_threadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_threadTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_threadTable->verticalHeader()->setVisible(false);
    m_threadTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_threadTable, &QTableWidget::customContextMenuRequested, [this](const QPoint& pos) {
        QModelIndex idx = m_threadTable->indexAt(pos);
        if (!idx.isValid()) return;
        QMenu menu(this);
        auto* termAction = menu.addAction(QStringLiteral("销毁线程"));
        connect(termAction, &QAction::triggered, this, &ProcessPropertiesDialog::onTerminateThread);
        menu.exec(m_threadTable->viewport()->mapToGlobal(pos));
    });
    tabWidget->addTab(m_threadTable, QStringLiteral("线程"));

    // 窗口列表
    m_windowTable = new QTableWidget(this);
    m_windowTable->setColumnCount(3);
    m_windowTable->setHorizontalHeaderLabels({QStringLiteral("句柄"), QStringLiteral("类名"), QStringLiteral("标题")});
    m_windowTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_windowTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_windowTable->verticalHeader()->setVisible(false);
    m_windowTable->horizontalHeader()->setStretchLastSection(true);
    tabWidget->addTab(m_windowTable, QStringLiteral("窗口"));

    // ===== ARK 新增标签页 =====
    auto* powerTab = new QWidget(this);
    setupPowerTab(powerTab);
    tabWidget->addTab(powerTab, QStringLiteral("权力管理"));

    auto* handleTab = new QWidget(this);
    setupHandleTab(handleTab);
    tabWidget->addTab(handleTab, QStringLiteral("句柄管理"));

    auto* memTab = new QWidget(this);
    setupMemoryTab(memTab);
    tabWidget->addTab(memTab, QStringLiteral("内存管理"));

    auto* tokenTab = new QWidget(this);
    setupTokenTab(tokenTab);
    tabWidget->addTab(tokenTab, QStringLiteral("Token"));

    auto* netTab = new QWidget(this);
    setupNetTab(netTab);
    tabWidget->addTab(netTab, QStringLiteral("网络连接"));

    auto* hotkeyTab = new QWidget(this);
    setupHotkeyTab(hotkeyTab);
    tabWidget->addTab(hotkeyTab, QStringLiteral("热键监视"));

    // 切换到句柄/权力/Token页时自动加载（按内容判断是否已加载，避免跨实例污染）
    connect(tabWidget, &QTabWidget::currentChanged, this, [this, tabWidget](int index) {
        QWidget* w = tabWidget->widget(index);
        if (!w) return;
        if (w->objectName() == QLatin1String("arkPowerTab") && m_privTable->rowCount() == 0) {
            onLoadPower();
        } else if (w->objectName() == QLatin1String("arkHandleTab") && m_handleTable->rowCount() == 0) {
            onLoadHandles();
        } else if (w->objectName() == QLatin1String("arkTokenTab") && m_tokUserLabel->text().isEmpty()) {
            onLoadToken();
        }
    });

    mainLayout->addWidget(tabWidget, 1);

    // 按钮区
    auto* btnLayout = new QHBoxLayout();
    auto* openBtn = new QPushButton(QStringLiteral("打开文件位置"), this);
    auto* filePropsBtn = new QPushButton(QStringLiteral("文件属性"), this);
    auto* killBtn = new QPushButton(QStringLiteral("终止进程"), this);
    auto* refreshThreadBtn = new QPushButton(QStringLiteral("刷新线程"), this);
    auto* refreshWindowBtn = new QPushButton(QStringLiteral("刷新窗口"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);

    connect(openBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onOpenFileLocation);
    connect(filePropsBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onShowFileProperties);
    connect(killBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onKillProcess);
    connect(refreshThreadBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onRefreshThreads);
    connect(refreshWindowBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onRefreshWindows);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(filePropsBtn);
    btnLayout->addWidget(killBtn);
    btnLayout->addWidget(refreshThreadBtn);
    btnLayout->addWidget(refreshWindowBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
}

void ProcessPropertiesDialog::loadProcessInfo()
{
    QString name = getProcessName(m_pid);
    setWindowTitle(name + QStringLiteral(" 的属性"));

    m_nameLabel->setText(name);

    QString path = getProcessPath(m_pid);
    m_filePathEdit->setText(path);

    // 父进程
    quint32 parentPid = getParentPid(m_pid);
    QString parentName = parentPid ? getProcessName(parentPid) : QString();
    m_parentLabel->setText(QStringLiteral("父进程: %1 (PID: %2)").arg(parentName).arg(parentPid));

    // 启动时间
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, m_pid);
    if (hProc) {
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
            SYSTEMTIME st;
            FileTimeToSystemTime(&ftCreate, &st);
            m_startTimeLabel->setText(QStringLiteral("启动时间: %1-%2-%3 %4:%5:%6")
                .arg(st.wYear).arg(st.wMonth, 2, 10, QChar('0')).arg(st.wDay, 2, 10, QChar('0'))
                .arg(st.wHour, 2, 10, QChar('0')).arg(st.wMinute, 2, 10, QChar('0')).arg(st.wSecond, 2, 10, QChar('0')));
        }

        // 判断 32/64 位
        BOOL isWow64 = FALSE;
        IsWow64Process(hProc, &isWow64);
        m_imageLabel->setText(isWow64 ? QStringLiteral("镜像: 32-bits") : QStringLiteral("镜像: 64-bits"));

        CloseHandle(hProc);
    }

    // PPL
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        qint32 ppl = ctx.driver().getPPL(m_pid);
        if (ppl >= 0) {
            int idx = 0;
            switch (ppl) {
                case PPL_None: idx = 0; break;
                case PP_Authenticode: idx = 1; break;
                case PP_CodeGen: idx = 2; break;
                case PP_Antimalware: idx = 3; break;
                case PP_Lsa: idx = 4; break;
                case PP_Windows: idx = 5; break;
                case PP_WinTcb: idx = 6; break;
                case PP_WinSystem: idx = 7; break;
                case PPL_Authenticode: idx = 8; break;
                case PPL_CodeGen: idx = 9; break;
                case PPL_Antimalware: idx = 10; break;
                case PPL_Lsa: idx = 11; break;
                case PPL_Windows: idx = 12; break;
                case PPL_WinTcb: idx = 13; break;
                case PPL_WinSystem: idx = 14; break;
            }
            m_pplCombo->setCurrentIndex(idx);
        }
    } else {
        m_pplSetBtn->setEnabled(false);
    }
}

void ProcessPropertiesDialog::loadThreads()
{
    m_threadTable->setRowCount(0);
    auto threads = enumThreads(m_pid);
    m_threadTable->setRowCount(threads.size());
    for (int i = 0; i < threads.size(); ++i) {
        m_threadTable->setItem(i, 0, new QTableWidgetItem(QString::number(threads[i].tid)));
        // ETHREAD 内核地址（驱动可用时显示，ToolHelp32 回退时为 0 显示 "N/A"）
        if (threads[i].ethread != 0) {
            m_threadTable->setItem(i, 1, new QTableWidgetItem(
                QStringLiteral("0x%1").arg(threads[i].ethread, 0, 16, QChar('0'))));
        } else {
            auto* item = new QTableWidgetItem(QStringLiteral("N/A"));
            item->setForeground(QBrush(Qt::gray));
            m_threadTable->setItem(i, 1, item);
        }
    }
}

void ProcessPropertiesDialog::loadWindows()
{
    m_windowTable->setRowCount(0);
    auto hwnds = enumProcessWindows(m_pid);
    m_windowTable->setRowCount(hwnds.size());
    for (int i = 0; i < hwnds.size(); ++i) {
        m_windowTable->setItem(i, 0, new QTableWidgetItem(QStringLiteral("0x%1").arg((quintptr)hwnds[i], 0, 16)));

        wchar_t className[256] = {0};
        GetClassNameW(hwnds[i], className, 256);
        m_windowTable->setItem(i, 1, new QTableWidgetItem(QString::fromWCharArray(className)));

        wchar_t title[256] = {0};
        GetWindowTextW(hwnds[i], title, 256);
        m_windowTable->setItem(i, 2, new QTableWidgetItem(QString::fromWCharArray(title)));
    }
}

void ProcessPropertiesDialog::onOpenFileLocation()
{
    QString path = m_filePathEdit->text();
    if (path.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("explorer.exe /select,\"%1\"").arg(path));
}

void ProcessPropertiesDialog::onShowFileProperties()
{
    QString path = m_filePathEdit->text();
    if (path.isEmpty()) return;

    FilePropertiesDialog dlg(path, this);
    dlg.exec();
}

void ProcessPropertiesDialog::onKillProcess()
{
    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定结束进程: %1 吗?\nPID: %2").arg(m_nameLabel->text()).arg(m_pid),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        ctx.driver().zwKillProcess(m_pid);
        Sleep(400);
        ctx.driver().pspTerminateProcess(m_pid);
        Sleep(400);
        ctx.driver().apcTerminateProcess(m_pid);
    } else {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, m_pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }
    accept();
}

void ProcessPropertiesDialog::onRefreshThreads()
{
    loadThreads();
}

void ProcessPropertiesDialog::onRefreshWindows()
{
    loadWindows();
}

void ProcessPropertiesDialog::onTerminateThread()
{
    int row = m_threadTable->currentRow();
    if (row < 0) return;
    quint32 tid = m_threadTable->item(row, 0)->text().toUInt();

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定销毁线程: %1 吗?").arg(tid),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    if (!ctx.driver().terminateThread(tid)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("销毁线程失败!"));
    }
}

void ProcessPropertiesDialog::onSetPPL()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    quint32 levels[] = {
        PPL_None, PP_Authenticode, PP_CodeGen, PP_Antimalware,
        PP_Lsa, PP_Windows, PP_WinTcb, PP_WinSystem,
        PPL_Authenticode, PPL_CodeGen, PPL_Antimalware, PPL_Lsa,
        PPL_Windows, PPL_WinTcb, PPL_WinSystem
    };
    int idx = m_pplCombo->currentIndex();
    if (idx < 0 || idx >= 15) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("设置 PPL 可能导致蓝屏!确定继续吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().setPPL(m_pid, levels[idx]);
}

// ==================== 权力管理 ====================

void ProcessPropertiesDialog::setupPowerTab(QWidget* tab)
{
    tab->setObjectName("arkPowerTab");
    auto* layout = new QVBoxLayout(tab);

    // 优先级
    auto* prioGroup = new QGroupBox(QStringLiteral("进程优先级"), tab);
    auto* prioLayout = new QHBoxLayout(prioGroup);
    m_priorityCombo = new QComboBox(prioGroup);
    m_priorityCombo->addItems({
        QStringLiteral("空闲 (Idle)"), QStringLiteral("普通 (Normal)"),
        QStringLiteral("高 (High)"), QStringLiteral("实时 (Realtime)"),
        QStringLiteral("低于正常 (Below Normal)"), QStringLiteral("高于正常 (Above Normal)")
    });
    auto* prioSetBtn = new QPushButton(QStringLiteral("设置优先级"), prioGroup);
    connect(prioSetBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onSetPriority);
    prioLayout->addWidget(m_priorityCombo);
    prioLayout->addWidget(prioSetBtn);
    prioLayout->addStretch();
    layout->addWidget(prioGroup);

    // CPU 亲和度
    auto* affGroup = new QGroupBox(QStringLiteral("CPU 亲和度"), tab);
    auto* affLayout = new QVBoxLayout(affGroup);
    m_affinityLabel = new QLabel(QStringLiteral("当前亲和度: -"), affGroup);
    affLayout->addWidget(m_affinityLabel);

    auto* affScroll = new QScrollArea(affGroup);
    affScroll->setWidgetResizable(true);
    auto* affWidget = new QWidget();
    auto* affChecksLayout = new QGridLayout(affWidget);
    // 按系统实际逻辑处理器数创建（单掩码模型最多 64）
    int cpuCount = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (cpuCount <= 0 || cpuCount > 64) cpuCount = 64;
    const int kCols = 8;
    for (int i = 0; i < cpuCount; ++i) {
        auto* cb = new QCheckBox(QStringLiteral("CPU %1").arg(i), affWidget);
        m_affinityChecks.append(cb);
        affChecksLayout->addWidget(cb, i / kCols, i % kCols);
    }
    affScroll->setWidget(affWidget);
    affLayout->addWidget(affScroll);

    auto* affBtnRow = new QHBoxLayout();
    auto* affApplyBtn = new QPushButton(QStringLiteral("应用亲和度"), affGroup);
    connect(affApplyBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onApplyAffinity);
    affBtnRow->addWidget(affApplyBtn);
    affBtnRow->addStretch();
    affLayout->addLayout(affBtnRow);
    layout->addWidget(affGroup, 1);

    // 令牌特权
    auto* privGroup = new QGroupBox(QStringLiteral("进程令牌特权 (双击行 启用/禁用)"), tab);
    auto* privLayout = new QVBoxLayout(privGroup);
    m_privTable = new QTableWidget(privGroup);
    m_privTable->setColumnCount(3);
    m_privTable->setHorizontalHeaderLabels({QStringLiteral("特权名称"), QStringLiteral("LUID"), QStringLiteral("状态")});
    m_privTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_privTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_privTable->verticalHeader()->setVisible(false);
    m_privTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_privTable, &QTableWidget::cellDoubleClicked, this, &ProcessPropertiesDialog::onTogglePrivilege);
    privLayout->addWidget(m_privTable);
    layout->addWidget(privGroup, 2);
}

void ProcessPropertiesDialog::onLoadPower()
{
    auto& ctx = AppContext::instance();

    // ===== 优先级（R3 优先, 驱动回退）=====
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    bool prioR3 = false;
    if (hProc) {
        DWORD cls = GetPriorityClass(hProc);
        int idx = -1;
        switch (cls) {
        case IDLE_PRIORITY_CLASS:     idx = 0; break;
        case NORMAL_PRIORITY_CLASS:   idx = 1; break;
        case HIGH_PRIORITY_CLASS:     idx = 2; break;
        case REALTIME_PRIORITY_CLASS: idx = 3; break;
        case BELOW_NORMAL_PRIORITY_CLASS: idx = 4; break;
        case ABOVE_NORMAL_PRIORITY_CLASS: idx = 5; break;
        }
        if (idx >= 0) {
            m_priorityCombo->setCurrentIndex(idx);
            prioR3 = true;
        }
        CloseHandle(hProc);
    }
    if (!prioR3 && ctx.driverLoaded()) {
        ArkPriorityInfo prio;
        if (ctx.driver().arkGetPriority(m_pid, prio)) {
            int idx = -1;
            switch (prio.priorityClass & 0xFF) {
            case 1: idx = 0; break;
            case 2: idx = 1; break;
            case 3: idx = 2; break;
            case 4: idx = 3; break;
            case 5: idx = 4; break;
            case 6: idx = 5; break;
            }
            if (idx >= 0) m_priorityCombo->setCurrentIndex(idx);
        }
    }

    // ===== 亲和度（R3 优先, 驱动回退）=====
    bool affR3 = false;
    if (hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, m_pid)) {
        DWORD_PTR pa = 0, sa = 0;
        if (GetProcessAffinityMask(hProc, &pa, &sa)) {
            ULONGLONG processAffinity = (ULONGLONG)pa;
            ULONGLONG systemAffinity = (ULONGLONG)sa;
            m_affinityLabel->setText(QStringLiteral("当前亲和度: 0x%1  (系统最大: 0x%2)")
                .arg(processAffinity, 16, 16, QChar('0'))
                .arg(systemAffinity, 16, 16, QChar('0')));
            for (int i = 0; i < m_affinityChecks.size(); ++i) {
                quint64 bit = 1ULL << i;
                m_affinityChecks[i]->setEnabled((systemAffinity & bit) != 0);
                m_affinityChecks[i]->setChecked((processAffinity & bit) != 0);
            }
            affR3 = true;
        }
        CloseHandle(hProc);
    }
    if (!affR3 && ctx.driverLoaded()) {
        ArkAffinityInfo aff;
        if (ctx.driver().arkGetAffinity(m_pid, aff)) {
            m_affinityLabel->setText(QStringLiteral("当前亲和度: 0x%1  (系统最大: 0x%2)")
                .arg(aff.affinity, 16, 16, QChar('0'))
                .arg(aff.maxAffinity, 16, 16, QChar('0')));
            for (int i = 0; i < m_affinityChecks.size(); ++i) {
                quint64 bit = 1ULL << i;
                m_affinityChecks[i]->setEnabled((aff.maxAffinity & bit) != 0);
                m_affinityChecks[i]->setChecked((aff.affinity & bit) != 0);
            }
        }
    }

    // ===== 特权（用户层优先，内核路径回退）=====
    ArkPrivilegeArray privs;
    privs.count = 0;
    privs.reserved = 0;
    m_privTable->setRowCount(0);
    bool gotPrivs = getUserModePrivileges(m_pid, privs);
    if (!gotPrivs && ctx.driverLoaded()) {
        gotPrivs = ctx.driver().arkGetTokenPrivileges(m_pid, privs);
    }
    if (gotPrivs) {
        m_privTable->setRowCount((int)privs.count);
        for (quint32 i = 0; i < privs.count && i < 64; ++i) {
            const ArkPrivilege& p = privs.privileges[i];
            m_privTable->setItem((int)i, 0, new QTableWidgetItem(privilegeNameFromLuid(p.luid)));
            m_privTable->setItem((int)i, 1, new QTableWidgetItem(
                QStringLiteral("0x%1").arg(p.luid, 0, 16, QChar('0'))));
            QString stateStr = (p.attributes & 2) ? QStringLiteral("已启用") : QStringLiteral("已禁用");
            auto* stateItem = new QTableWidgetItem(stateStr);
            stateItem->setForeground((p.attributes & 2) ? QBrush(Qt::green) : QBrush(Qt::gray));
            m_privTable->setItem((int)i, 2, stateItem);
        }
        if (privs.count == 0) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("进程 %1 的特权查询返回空列表 (受保护进程或令牌不可访问)").arg(m_pid));
        }
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("查询进程特权失败\n"
                           "可能是受保护进程(PPL)或进程已退出。"));
    }
}

void ProcessPropertiesDialog::onSetPriority()
{
    auto& ctx = AppContext::instance();
    static const quint32 classes[] = {1, 2, 3, 4, 5, 6};
    int idx = m_priorityCombo->currentIndex();
    if (idx < 0) return;

    // R3 路径: SetPriorityClass
    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (hProc) {
        static const DWORD r3classes[] = {
            IDLE_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS,
            REALTIME_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS};
        BOOL ok = SetPriorityClass(hProc, r3classes[idx]);
        CloseHandle(hProc);
        if (ok) {
            QMessageBox::information(this, QStringLiteral("EFCHSword"),
                QStringLiteral("优先级已设置为: %1").arg(priorityClassName(classes[idx])));
            return;
        }
    }

    // 驱动路径回退
    if (ctx.driverLoaded()) {
        if (ctx.driver().arkSetPriority(m_pid, classes[idx])) {
            QMessageBox::information(this, QStringLiteral("EFCHSword"),
                QStringLiteral("优先级已设置为: %1").arg(priorityClassName(classes[idx])));
            return;
        }
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("设置优先级失败"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("设置失败 (无驱动且用户层无权限), 请以管理员身份运行"));
    }
}

void ProcessPropertiesDialog::onApplyAffinity()
{
    auto& ctx = AppContext::instance();

    quint64 mask = 0;
    for (int i = 0; i < m_affinityChecks.size(); ++i) {
        if (m_affinityChecks[i]->isChecked() && m_affinityChecks[i]->isEnabled()) {
            mask |= (1ULL << i);
        }
    }
    if (mask == 0) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("至少选择一个 CPU"));
        return;
    }

    // R3 路径: SetProcessAffinityMask
    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (hProc) {
        BOOL ok = SetProcessAffinityMask(hProc, (DWORD_PTR)mask);
        CloseHandle(hProc);
        if (ok) {
            QMessageBox::information(this, QStringLiteral("EFCHSword"),
                QStringLiteral("亲和度已设置为 0x%1").arg(mask, 16, 16, QChar('0')));
            onLoadPower();
            return;
        }
    }

    // 驱动路径回退
    if (ctx.driverLoaded()) {
        if (ctx.driver().arkSetAffinity(m_pid, mask)) {
            QMessageBox::information(this, QStringLiteral("EFCHSword"),
                QStringLiteral("亲和度已设置为 0x%1").arg(mask, 16, 16, QChar('0')));
            onLoadPower();
            return;
        }
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("设置亲和度失败"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("设置失败 (无驱动且用户层无权限), 请以管理员身份运行"));
    }
}

void ProcessPropertiesDialog::onTogglePrivilege()
{
    auto& ctx = AppContext::instance();

    int row = m_privTable->currentRow();
    if (row < 0) return;

    bool enabled = m_privTable->item(row, 2)->text() == QStringLiteral("已启用");
    quint64 luid = m_privTable->item(row, 1)->text().toULongLong(nullptr, 16);
    quint32 newAttr = enabled ? 0 : 2;   // 0=禁用 2=启用

    // R3 路径: AdjustTokenPrivileges（可开别的进程令牌时可用）
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
        HANDLE hToken = NULL;
        bool r3Ok = false;
        if (hProc &&
            OpenProcessToken(hProc, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken) && hToken) {
            TOKEN_PRIVILEGES tp;
            ZeroMemory(&tp, sizeof(tp));
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid.LowPart = (DWORD)(luid & 0xFFFFFFFF);
            tp.Privileges[0].Luid.HighPart = (LONG)((luid >> 32) & 0xFFFFFFFF);
            tp.Privileges[0].Attributes = newAttr;
            // Attempted 自身进程时 AdjustTokenPrivileges 可能报 not-all-assigned，
            // 只要 ERROR_SUCCESS 即部分生效；此处以真实状态刷新判定
            if (AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL)) {
                r3Ok = true;
            }
            CloseHandle(hToken);
        }
        if (hProc) CloseHandle(hProc);

        if (r3Ok) {
            onLoadPower();
            return;
        }
    }

    // 驱动路径回退
    if (ctx.driverLoaded()) {
        if (ctx.driver().arkSetTokenPrivilege(m_pid, luid, newAttr)) {
            onLoadPower();
        } else {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("修改特权失败"));
        }
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("修改特权失败 (无驱动且用户层无法打开目标令牌)"));
    }
}

// ==================== 句柄管理 ====================

void ProcessPropertiesDialog::setupHandleTab(QWidget* tab)
{
    tab->setObjectName("arkHandleTab");
    auto* layout = new QVBoxLayout(tab);

    auto* btnRow = new QHBoxLayout();
    m_handleRefreshBtn = new QPushButton(QStringLiteral("刷新句柄"), tab);
    m_handleCloseBtn = new QPushButton(QStringLiteral("关闭所选句柄"), tab);
    m_handleDetailBtn = new QPushButton(QStringLiteral("句柄详细信息"), tab);
    m_handleCountLabel = new QLabel(QStringLiteral("句柄数: -"), tab);
    connect(m_handleRefreshBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onLoadHandles);
    connect(m_handleCloseBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onCloseSelectedHandle);
    connect(m_handleDetailBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onShowHandleDetails);
    btnRow->addWidget(m_handleRefreshBtn);
    btnRow->addWidget(m_handleCloseBtn);
    btnRow->addWidget(m_handleDetailBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_handleCountLabel);
    layout->addLayout(btnRow);

    m_handleTable = new QTableWidget(tab);
    m_handleTable->setColumnCount(7);
    m_handleTable->setHorizontalHeaderLabels({
        QStringLiteral("句柄"), QStringLiteral("类型"), QStringLiteral("名称"),
        QStringLiteral("访问权限"), QStringLiteral("对象地址"), QStringLiteral("指针计数"), QStringLiteral("句柄计数")});
    m_handleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_handleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_handleTable->verticalHeader()->setVisible(false);
    m_handleTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_handleTable, 1);
}

void ProcessPropertiesDialog::onLoadHandles()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载，句柄管理需要内核驱动"));
        return;
    }

    m_handleTable->setRowCount(0);
    quint32 startIndex = 0;
    int totalRows = 0;

    do {
        // ArkHandleArray 约 290KB, 堆分配避免逼近 1MB 线程栈上限
        auto arr = std::make_unique<ArkHandleArray>();
        if (!ctx.driver().enumProcessHandles(m_pid, startIndex, *arr)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("枚举句柄失败"));
            return;
        }
        for (quint32 i = 0; i < arr->count; ++i) {
            const ArkHandleEntry& e = arr->entries[i];
            int r = m_handleTable->rowCount();
            m_handleTable->insertRow(r);
            m_handleTable->setItem(r, 0, new QTableWidgetItem(QStringLiteral("0x%1").arg(e.handle, 0, 16, QChar('0'))));
            m_handleTable->setItem(r, 1, new QTableWidgetItem(QString::fromWCharArray(e.typeName)));
            m_handleTable->setItem(r, 2, new QTableWidgetItem(QString::fromWCharArray(e.name)));
            m_handleTable->setItem(r, 3, new QTableWidgetItem(QStringLiteral("0x%1").arg(e.grantedAccess, 0, 16, QChar('0'))));
            m_handleTable->setItem(r, 4, new QTableWidgetItem(QStringLiteral("0x%1").arg(e.object, 0, 16, QChar('0'))));
            m_handleTable->setItem(r, 5, new QTableWidgetItem(QString::number(e.pointerCount)));
            m_handleTable->setItem(r, 6, new QTableWidgetItem(QString::number(e.objectHandleCount)));
            ++totalRows;
        }
        startIndex = arr->nextIndex;
    } while (startIndex != 0);

    m_handleCountLabel->setText(QStringLiteral("句柄数: %1").arg(totalRows));
}

void ProcessPropertiesDialog::onCloseSelectedHandle()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    int row = m_handleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("请先选择一个句柄"));
        return;
    }
    quint64 handle = m_handleTable->item(row, 0)->text().toULongLong(nullptr, 16);
    QString desc = m_handleTable->item(row, 1)->text() + QStringLiteral(" / ") + m_handleTable->item(row, 2)->text();

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定关闭句柄 %1 吗?\n%2\n\n关闭关键句柄可能导致目标进程崩溃!")
                .arg(m_handleTable->item(row, 0)->text()).arg(desc),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (ctx.driver().closeProcessHandle(m_pid, handle)) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("句柄已关闭"));
        onLoadHandles();
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("关闭句柄失败"));
    }
}

void ProcessPropertiesDialog::onShowHandleDetails()
{
    int row = m_handleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("请先选择一个句柄"));
        return;
    }

    QString details = QStringLiteral(
        "句柄值:      %1\n"
        "对象类型:    %2\n"
        "对象名称:    %3\n"
        "访问权限:    0x%4\n"
        "内核对象:    0x%5\n"
        "指针计数:    %6\n"
        "句柄计数:    %7\n"
        "所属进程:    %8 (%9)")
        .arg(m_handleTable->item(row, 0)->text())
        .arg(m_handleTable->item(row, 1)->text())
        .arg(m_handleTable->item(row, 2)->text())
        .arg(m_handleTable->item(row, 3)->text())
        .arg(m_handleTable->item(row, 4)->text())
        .arg(m_handleTable->item(row, 5)->text())
        .arg(m_handleTable->item(row, 6)->text())
        .arg(m_pid)
        .arg(m_nameLabel ? m_nameLabel->text() : QString());

    QMessageBox::information(this, QStringLiteral("句柄详细信息"), details);
}

// ==================== 内存管理 ====================

void ProcessPropertiesDialog::setupMemoryTab(QWidget* tab)
{
    tab->setObjectName("arkMemoryTab");
    auto* layout = new QVBoxLayout(tab);

    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel(QStringLiteral("地址:"), tab));
    m_memAddrEdit = new QLineEdit(tab);
    m_memAddrEdit->setPlaceholderText(QStringLiteral("十六进制，如 0x7FF600000000"));
    inputRow->addWidget(m_memAddrEdit);
    inputRow->addWidget(new QLabel(QStringLiteral("大小:"), tab));
    m_memSizeEdit = new QLineEdit(QStringLiteral("256"), tab);
    m_memSizeEdit->setMaximumWidth(80);
    inputRow->addWidget(m_memSizeEdit);

    auto* queryBtn = new QPushButton(QStringLiteral("查询区域"), tab);
    auto* readBtn = new QPushButton(QStringLiteral("读取内存"), tab);
    auto* writeBtn = new QPushButton(QStringLiteral("写入内存"), tab);
    connect(queryBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onQueryMemory);
    connect(readBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onReadMemory);
    connect(writeBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onWriteMemory);
    inputRow->addWidget(queryBtn);
    inputRow->addWidget(readBtn);
    inputRow->addWidget(writeBtn);
    layout->addLayout(inputRow);

    m_memRegionLabel = new QLabel(QStringLiteral("区域信息: -"), tab);
    m_memRegionLabel->setWordWrap(true);
    layout->addWidget(m_memRegionLabel);

    m_memHexView = new QPlainTextEdit(tab);
    m_memHexView->setReadOnly(true);
    m_memHexView->setFont(QFont(QStringLiteral("Consolas")));
    m_memHexView->setPlaceholderText(QStringLiteral("点击 [读取内存] 查看十六进制转储"));
    layout->addWidget(m_memHexView, 1);
}

void ProcessPropertiesDialog::onQueryMemory()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    bool ok = false;
    quint64 addr = m_memAddrEdit->text().toULongLong(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("地址格式无效"));
        return;
    }

    ArkVmInfo info;
    if (!ctx.driver().arkQueryVirtualMemory(m_pid, addr, info)) {
        m_memRegionLabel->setText(QStringLiteral("区域信息: 查询失败（地址无效或进程已退出）"));
        return;
    }

    QString stateStr;
    switch (info.state) {
    case 0x1000: stateStr = QStringLiteral("MEM_COMMIT"); break;
    case 0x10000: stateStr = QStringLiteral("MEM_FREE"); break;
    case 0x2000: stateStr = QStringLiteral("MEM_RESERVE"); break;
    default: stateStr = QString::number(info.state);
    }
    QString typeStr;
    switch (info.type) {
    case 0x1000000: typeStr = QStringLiteral("MEM_IMAGE"); break;
    case 0x40000: typeStr = QStringLiteral("MEM_MAPPED"); break;
    case 0x20000: typeStr = QStringLiteral("MEM_PRIVATE"); break;
    default: typeStr = QString::number(info.type);
    }

    m_memRegionLabel->setText(QStringLiteral(
        "区域信息: 基址 0x%1  大小 0x%2 (%3 字节)\n"
        "状态: %4   保护: %5   类型: %6")
        .arg(info.baseAddress, 0, 16, QChar('0'))
        .arg(info.regionSize, 0, 16, QChar('0'))
        .arg(info.regionSize)
        .arg(stateStr)
        .arg(memProtectToString(info.protect))
        .arg(typeStr));
}

void ProcessPropertiesDialog::onReadMemory()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    bool ok = false;
    quint64 addr = m_memAddrEdit->text().toULongLong(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("地址格式无效"));
        return;
    }
    quint32 size = m_memSizeEdit->text().toUInt();
    if (size == 0 || size > 0x10000) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("大小须在 1-65536 之间"));
        return;
    }

    QByteArray data;
    if (!ctx.driver().arkReadProcessMemory(m_pid, addr, size, data) || data.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("读取内存失败（地址无效或不可读）"));
        return;
    }

    // 十六进制转储
    QString dump;
    for (int i = 0; i < data.size(); i += 16) {
        QString hexPart, asciiPart;
        for (int j = 0; j < 16; ++j) {
            if (i + j < data.size()) {
                quint8 b = (quint8)data[i + j];
                hexPart += QStringLiteral("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
                asciiPart += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
            } else {
                hexPart += QStringLiteral("   ");
            }
        }
        dump += QStringLiteral("%1  %2 %3\n")
            .arg(addr + i, 16, 16, QChar('0'))
            .arg(hexPart)
            .arg(asciiPart);
    }
    m_memHexView->setPlainText(dump);
}

void ProcessPropertiesDialog::onWriteMemory()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }
    bool ok = false;
    quint64 addr = m_memAddrEdit->text().toULongLong(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("地址格式无效"));
        return;
    }

    QString text = QInputDialog::getText(this, QStringLiteral("写入内存"),
        QStringLiteral("输入十六进制字节序列（如 90 90 90 或 909090）:"));
    if (text.isEmpty()) return;

    QString cleaned = text;
    cleaned.remove(QLatin1Char(' '));
    if (cleaned.size() % 2 != 0 || cleaned.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("字节序列无效"));
        return;
    }
    QByteArray data;
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool convOk = false;
        quint8 byte = (quint8)cleaned.mid(i, 2).toUInt(&convOk, 16);
        if (!convOk) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("字节序列含非十六进制字符"));
            return;
        }
        data.append((char)byte);
    }

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定向 0x%1 写入 %2 字节吗? 这可能破坏目标进程!")
                .arg(addr, 0, 16).arg(data.size()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (ctx.driver().arkWriteProcessMemory(m_pid, addr, data)) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("写入成功"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("写入失败"));
    }
}

// ==================== Token ====================

void ProcessPropertiesDialog::setupTokenTab(QWidget* tab)
{
    tab->setObjectName("arkTokenTab");
    auto* layout = new QVBoxLayout(tab);

    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新 Token"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onLoadToken);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto* infoGroup = new QGroupBox(QStringLiteral("令牌信息"), tab);
    auto* form = new QFormLayout(infoGroup);
    m_tokUserLabel = new QLabel(QStringLiteral("-"), infoGroup);
    m_tokSessionLabel = new QLabel(QStringLiteral("-"), infoGroup);
    m_tokElevLabel = new QLabel(QStringLiteral("-"), infoGroup);
    m_tokIntegrityLabel = new QLabel(QStringLiteral("-"), infoGroup);
    m_tokGroupsLabel = new QLabel(QStringLiteral("-"), infoGroup);
    m_tokUserLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("用户 SID:"), m_tokUserLabel);
    form->addRow(QStringLiteral("会话 ID:"), m_tokSessionLabel);
    form->addRow(QStringLiteral("提升类型:"), m_tokElevLabel);
    form->addRow(QStringLiteral("完整性级别:"), m_tokIntegrityLabel);
    form->addRow(QStringLiteral("用户组数量:"), m_tokGroupsLabel);
    layout->addWidget(infoGroup);
    layout->addStretch();

    // 提示：特权管理在 "权力管理" 标签页
    auto* hint = new QLabel(QStringLiteral("提示: 令牌特权的启用/禁用在 [权力管理] 标签页操作"), tab);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);
}

void ProcessPropertiesDialog::onLoadToken()
{
    auto& ctx = AppContext::instance();

    // ===== R3 路径：OpenProcessToken + GetTokenInformation =====
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
        HANDLE hToken = NULL;
        bool r3Ok = false;
        if (hProc && OpenProcessToken(hProc, TOKEN_QUERY, &hToken) && hToken) {
            DWORD retLen = 0;
            DWORD sessionId = 0;
            DWORD elevated = 0;
            DWORD elevType = 0;
            DWORD integrity = 0;
            DWORD groupCount = 0;
            QString userSid;
            bool allOk = true;
            Q_UNUSED(allOk);

            if (GetTokenInformation(hToken, TokenSessionId, &sessionId, sizeof(sessionId), &retLen) &&
                GetTokenInformation(hToken, TokenElevation, &elevated, sizeof(elevated), &retLen) &&
                GetTokenInformation(hToken, TokenElevationType, &elevType, sizeof(elevType), &retLen)) {
                // 完整性级别
                PTOKEN_MANDATORY_LABEL ml = nullptr;
                if (GetTokenInformation(hToken, TokenIntegrityLevel, nullptr, 0, &retLen) ||
                    GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    ml = (PTOKEN_MANDATORY_LABEL)LocalAlloc(LPTR, retLen);
                    if (ml && GetTokenInformation(hToken, TokenIntegrityLevel, ml, retLen, &retLen)) {
                        PUCHAR count = GetSidSubAuthorityCount(ml->Label.Sid);
                        if (count && *count > 0) {
                            integrity = *GetSidSubAuthority(ml->Label.Sid, (DWORD)(*count - 1));
                        }
                    }
                    LocalFree(ml);
                }
                // 用户 SID
                PTOKEN_USER tu = nullptr;
                if (GetTokenInformation(hToken, TokenUser, nullptr, 0, &retLen) ||
                    GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    tu = (PTOKEN_USER)LocalAlloc(LPTR, retLen);
                    if (tu && GetTokenInformation(hToken, TokenUser, tu, retLen, &retLen)) {
                        LPWSTR sidStr = nullptr;
                        if (ConvertSidToStringSidW(tu->User.Sid, &sidStr)) {
                            userSid = QString::fromWCharArray(sidStr);
                            LocalFree(sidStr);
                        }
                    }
                    LocalFree(tu);
                }
                // 组数量
                PTOKEN_GROUPS tg = nullptr;
                if (GetTokenInformation(hToken, TokenGroups, nullptr, 0, &retLen) ||
                    GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    tg = (PTOKEN_GROUPS)LocalAlloc(LPTR, retLen);
                    if (tg && GetTokenInformation(hToken, TokenGroups, tg, retLen, &retLen)) {
                        groupCount = tg->GroupCount;
                    }
                    LocalFree(tg);
                }

                m_tokUserLabel->setText(userSid.isEmpty() ? QStringLiteral("(无法解析)") : userSid);
                m_tokSessionLabel->setText(QString::number(sessionId));
                QString elevStr;
                switch (elevType) {
                case 1: elevStr = QStringLiteral("Default (默认)"); break;
                case 2: elevStr = QStringLiteral("Limited (受限)"); break;
                case 3: elevStr = QStringLiteral("Full (已提升)"); break;
                default: elevStr = QString::number(elevType);
                }
                elevStr += elevated ? QStringLiteral("  [Elevated]") : QStringLiteral("  [Not Elevated]");
                m_tokElevLabel->setText(elevStr);
                QString intStr;
                if (integrity >= 0x4000) intStr = QStringLiteral("System");
                else if (integrity >= 0x3000) intStr = QStringLiteral("High");
                else if (integrity >= 0x2000) intStr = QStringLiteral("Medium");
                else if (integrity >= 0x1000) intStr = QStringLiteral("Low");
                else intStr = QStringLiteral("Untrusted");
                m_tokIntegrityLabel->setText(QStringLiteral("%1 (0x%2)").arg(intStr).arg(integrity, 0, 16));
                m_tokGroupsLabel->setText(QString::number(groupCount));
                r3Ok = true;
            }
            CloseHandle(hToken);
            Q_UNUSED(allOk);
            if (r3Ok) {
                CloseHandle(hProc);
                return;     // R3 成功即返回
            }
        }
        if (hProc) CloseHandle(hProc);
    }

    // ===== 驱动路径回退 =====
    if (!ctx.driverLoaded()) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"),
            QStringLiteral("Token 查询失败: 用户层无法打开目标令牌, 且驱动未加载"));
        return;
    }

    ArkTokenInfo info;
    if (!ctx.driver().arkGetTokenInfo(m_pid, info)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("获取 Token 失败"));
        return;
    }

    m_tokUserLabel->setText(QString::fromWCharArray(info.userSid));
    m_tokSessionLabel->setText(QString::number(info.sessionId));

    QString elevStr;
    switch (info.elevationType) {
    case 1: elevStr = QStringLiteral("Default (默认)"); break;
    case 2: elevStr = QStringLiteral("Limited (受限)"); break;
    case 3: elevStr = QStringLiteral("Full (已提升)"); break;
    default: elevStr = QString::number(info.elevationType);
    }
    elevStr += info.isElevated ? QStringLiteral("  [Elevated]") : QStringLiteral("  [Not Elevated]");
    m_tokElevLabel->setText(elevStr);

    // 完整性级别: 0x2000+RID
    QString intStr;
    quint32 rid = info.integrityLevel & 0xFFFF;
    if (rid >= 0x4000) intStr = QStringLiteral("System");
    else if (rid >= 0x3000) intStr = QStringLiteral("High");
    else if (rid >= 0x2000) intStr = QStringLiteral("Medium");
    else if (rid >= 0x1000) intStr = QStringLiteral("Low");
    else intStr = QStringLiteral("Untrusted");
    m_tokIntegrityLabel->setText(QStringLiteral("%1 (0x%2)").arg(intStr).arg(info.integrityLevel, 0, 16));

    m_tokGroupsLabel->setText(QString::number(info.groupCount));
}

// ==================== 网络连接 ====================

void ProcessPropertiesDialog::setupNetTab(QWidget* tab)
{
    tab->setObjectName("arkNetTab");
    auto* layout = new QVBoxLayout(tab);

    auto* btnRow = new QHBoxLayout();
    m_netRefreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
    connect(m_netRefreshBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onLoadNetConnections);
    btnRow->addWidget(m_netRefreshBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    m_netTable = new QTableWidget(tab);
    m_netTable->setColumnCount(6);
    m_netTable->setHorizontalHeaderLabels({
        QStringLiteral("协议"), QStringLiteral("本地地址"), QStringLiteral("本地端口"),
        QStringLiteral("远程地址"), QStringLiteral("远程端口"), QStringLiteral("状态")});
    m_netTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_netTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_netTable->verticalHeader()->setVisible(false);
    m_netTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_netTable, 1);

    onLoadNetConnections();
}

void ProcessPropertiesDialog::onLoadNetConnections()
{
    m_netTable->setRowCount(0);
    auto conns = enumNetConnections(m_pid, true);
    m_netTable->setRowCount(conns.size());
    for (int i = 0; i < conns.size(); ++i) {
        const NetConnInfo& c = conns[i];
        m_netTable->setItem(i, 0, new QTableWidgetItem(protoToString(c.proto)));
        m_netTable->setItem(i, 1, new QTableWidgetItem(c.localAddr));
        m_netTable->setItem(i, 2, new QTableWidgetItem(QString::number(c.localPort)));
        m_netTable->setItem(i, 3, new QTableWidgetItem(c.remoteAddr));
        m_netTable->setItem(i, 4, new QTableWidgetItem(QString::number(c.remotePort)));
        m_netTable->setItem(i, 5, new QTableWidgetItem(
            c.proto == 6 ? tcpStateToString(c.state) : QStringLiteral("-")));
    }
}

// ==================== 热键监视 ====================
//
// 显示该进程通过 RegisterHotKey 注册的全局热键（读取 win32k gphkHashTable），
// 取代旧的内核键盘 Hook 前台按键记录。

void ProcessPropertiesDialog::setupHotkeyTab(QWidget* tab)
{
    tab->setObjectName("arkHotkeyTab");
    auto* layout = new QVBoxLayout(tab);

    auto* btnRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新热键"), tab);
    connect(refreshBtn, &QPushButton::clicked, this,
            &ProcessPropertiesDialog::onRefreshProcessHotkeys);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_kbdStateLabel = new QLabel(QStringLiteral("就绪"), tab));
    layout->addLayout(btnRow);

    auto* hint = new QLabel(
        QStringLiteral("显示该进程通过 RegisterHotKey 注册的全局热键 (读取 win32k 热键表)。"), tab);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_kbdTable = new QTableWidget(tab);
    m_kbdTable->setColumnCount(4);
    m_kbdTable->setHorizontalHeaderLabels({
        QStringLiteral("热键组合"), QStringLiteral("虚拟键"), QStringLiteral("ID"), QStringLiteral("修饰键")});
    m_kbdTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_kbdTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_kbdTable->verticalHeader()->setVisible(false);
    m_kbdTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_kbdTable, 1);

    onRefreshProcessHotkeys();
}

void ProcessPropertiesDialog::onRefreshProcessHotkeys()
{
    m_kbdTable->setRowCount(0);
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        m_kbdStateLabel->setText(QStringLiteral("驱动未加载"));
        return;
    }

    // ArkHotkeyArray 约 90KB，可接受栈分配
    ArkHotkeyArray arr;
    if (!ctx.driver().kbdEnumHotkeys(arr)) {
        m_kbdStateLabel->setText(QStringLiteral("热键枚举失败 (错误码: %1)")
            .arg(ctx.driver().lastIoError()));
        return;
    }

    int shown = 0;
    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkHotkeyEntry& e = arr.entries[i];
        if (e.pid != m_pid) continue;

        QString keyName = vkDisplayName(e.vk);
        QString combo = modifiersDisplayName(e.fsModifiers);
        QString comboText = (combo == QStringLiteral("-"))
            ? keyName : combo + QStringLiteral(" + ") + keyName;

        int r = m_kbdTable->rowCount();
        m_kbdTable->insertRow(r);
        m_kbdTable->setItem(r, 0, new QTableWidgetItem(comboText));
        m_kbdTable->setItem(r, 1, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(e.vk, 2, 16, QChar('0'))));
        m_kbdTable->setItem(r, 2, new QTableWidgetItem(QString::number(e.id)));
        m_kbdTable->setItem(r, 3, new QTableWidgetItem(combo));
        ++shown;
    }
    m_kbdStateLabel->setText(
        QStringLiteral("该进程注册热键: %1 个 (系统共 %2 个)").arg(shown).arg(arr.count));
}
