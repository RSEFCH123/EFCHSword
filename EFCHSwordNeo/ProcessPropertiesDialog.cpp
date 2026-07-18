#include "ProcessPropertiesDialog.h"
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

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

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
static QVector<quint32> enumThreads(quint32 pid)
{
    QVector<quint32> tids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return tids;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                tids.append(te.th32ThreadID);
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    return tids;
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

    auto* mainLayout = new QVBoxLayout(this);

    // 基本信息
    auto* infoGroup = new QGroupBox(QStringLiteral("基本信息"), this);
    auto* infoLayout = new QFormLayout(infoGroup);

    m_nameLabel = new QLabel(this);
    m_synopsisLabel = new QLabel(this);
    m_verLabel = new QLabel(this);
    m_parentLabel = new QLabel(this);
    m_startTimeLabel = new QLabel(this);
    m_userLabel = new QLabel(this);
    m_imageLabel = new QLabel(this);
    m_filePathEdit = new QLineEdit(this);
    m_filePathEdit->setReadOnly(true);
    m_cmdLineEdit = new QLineEdit(this);
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

    mainLayout->addWidget(infoGroup);

    // PPL 设置
    auto* pplGroup = new QGroupBox(QStringLiteral("PPL 保护"), this);
    auto* pplLayout = new QHBoxLayout(pplGroup);
    m_pplCombo = new QComboBox(this);
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
    m_pplSetBtn = new QPushButton(QStringLiteral("设置"), this);
    connect(m_pplSetBtn, &QPushButton::clicked, this, &ProcessPropertiesDialog::onSetPPL);
    pplLayout->addWidget(m_pplCombo);
    pplLayout->addWidget(m_pplSetBtn);
    mainLayout->addWidget(pplGroup);

    // 线程和窗口列表
    auto* tabWidget = new QTabWidget(this);

    // 线程列表
    m_threadTable = new QTableWidget(this);
    m_threadTable->setColumnCount(1);
    m_threadTable->setHorizontalHeaderLabels({QStringLiteral("TID")});
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
    auto tids = enumThreads(m_pid);
    m_threadTable->setRowCount(tids.size());
    for (int i = 0; i < tids.size(); ++i) {
        m_threadTable->setItem(i, 0, new QTableWidgetItem(QString::number(tids[i])));
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
