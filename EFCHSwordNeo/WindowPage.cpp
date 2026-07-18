#include "WindowPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

// 窗口信息列
enum WindowColumn {
    Col_Handle = 0,
    Col_Title,
    Col_Class,
    Col_TID,
    Col_PID,
    Col_ProcessName,
    Col_Status,
    Col_Count
};

// 枚举窗口回调数据
struct EnumWindowData {
    bool showHidden = false;     // 显示隐藏窗口
    bool showNoTitle = false;    // 显示无标题窗口
    QVector<HWND> hwnds;
};

static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* data = reinterpret_cast<EnumWindowData*>(lParam);

    bool isVisible = IsWindowVisible(hwnd) != 0;
    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 256);
    bool hasTitle = wcslen(title) > 0;

    if (!data->showHidden && !isVisible) return TRUE;
    if (!data->showNoTitle && !hasTitle) return TRUE;

    data->hwnds.append(hwnd);
    return TRUE;
}

// 获取窗口所属进程名
static QString getProcessNameByPid(quint32 pid)
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

WindowPage::WindowPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupContextMenu();

    // 自动刷新定时器
    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(3000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &WindowPage::refreshWindows);
    m_autoRefreshTimer->start();

    refreshWindows();
}

WindowPage::~WindowPage() = default;

void WindowPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // 工具栏
    auto* toolLayout = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    m_showHidden = new QCheckBox(QStringLiteral("显示隐藏窗口"), this);
    m_showNoTitle = new QCheckBox(QStringLiteral("显示无标题窗口"), this);
    m_statusLabel = new QLabel(QStringLiteral("窗口数: 0"), this);

    connect(refreshBtn, &QPushButton::clicked, this, &WindowPage::onRefreshClicked);
    connect(m_showHidden, &QCheckBox::toggled, this, &WindowPage::onShowHiddenToggled);
    connect(m_showNoTitle, &QCheckBox::toggled, this, &WindowPage::onShowHiddenToggled);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addWidget(m_showHidden);
    toolLayout->addWidget(m_showNoTitle);
    toolLayout->addStretch();
    toolLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(toolLayout);

    // 窗口列表
    m_table = new QTableWidget(this);
    m_table->setColumnCount(Col_Count);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("句柄"),
        QStringLiteral("窗口标题"),
        QStringLiteral("类名"),
        QStringLiteral("TID"),
        QStringLiteral("PID"),
        QStringLiteral("进程名"),
        QStringLiteral("状态")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setColumnWidth(Col_Handle, 100);
    m_table->setColumnWidth(Col_Title, 200);
    m_table->setColumnWidth(Col_Class, 150);
    m_table->setColumnWidth(Col_TID, 70);
    m_table->setColumnWidth(Col_PID, 70);
    m_table->setColumnWidth(Col_ProcessName, 120);

    connect(m_table, &QTableWidget::customContextMenuRequested, this, &WindowPage::onContextMenu);

    mainLayout->addWidget(m_table);
}

void WindowPage::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    auto* closeAction = m_contextMenu->addAction(QStringLiteral("关闭窗口"));
    connect(closeAction, &QAction::triggered, this, &WindowPage::onCloseWindow);

    auto* forceCloseAction = m_contextMenu->addAction(QStringLiteral("强制关闭窗口"));
    connect(forceCloseAction, &QAction::triggered, this, &WindowPage::onForceCloseWindow);

    m_contextMenu->addSeparator();

    auto* showAction = m_contextMenu->addAction(QStringLiteral("显示窗口"));
    connect(showAction, &QAction::triggered, this, &WindowPage::onShowWindow);

    auto* hideAction = m_contextMenu->addAction(QStringLiteral("隐藏窗口"));
    connect(hideAction, &QAction::triggered, this, &WindowPage::onHideWindow);

    auto* forceShowAction = m_contextMenu->addAction(QStringLiteral("强制显示窗口"));
    connect(forceShowAction, &QAction::triggered, this, &WindowPage::onForceShowWindow);

    m_contextMenu->addSeparator();

    auto* restoreAction = m_contextMenu->addAction(QStringLiteral("还原窗口"));
    connect(restoreAction, &QAction::triggered, this, &WindowPage::onRestoreWindow);

    auto* minimizeAction = m_contextMenu->addAction(QStringLiteral("最小化窗口"));
    connect(minimizeAction, &QAction::triggered, this, &WindowPage::onMinimizeWindow);

    auto* maximizeAction = m_contextMenu->addAction(QStringLiteral("最大化窗口"));
    connect(maximizeAction, &QAction::triggered, this, &WindowPage::onMaximizeWindow);

    m_contextMenu->addSeparator();

    auto* disableAction = m_contextMenu->addAction(QStringLiteral("禁用窗口"));
    connect(disableAction, &QAction::triggered, this, &WindowPage::onDisableWindow);

    auto* enableAction = m_contextMenu->addAction(QStringLiteral("启用窗口"));
    connect(enableAction, &QAction::triggered, this, &WindowPage::onEnableWindow);

    m_contextMenu->addSeparator();

    auto* topmostAction = m_contextMenu->addAction(QStringLiteral("置顶窗口"));
    connect(topmostAction, &QAction::triggered, this, &WindowPage::onSetTopmost);

    auto* untopmostAction = m_contextMenu->addAction(QStringLiteral("取消置顶"));
    connect(untopmostAction, &QAction::triggered, this, &WindowPage::onUnsetTopmost);

    auto* transparentAction = m_contextMenu->addAction(QStringLiteral("设置透明度"));
    connect(transparentAction, &QAction::triggered, this, &WindowPage::onSetTransparent);

    m_contextMenu->addSeparator();

    auto* copyTitleAction = m_contextMenu->addAction(QStringLiteral("复制窗口标题"));
    connect(copyTitleAction, &QAction::triggered, this, &WindowPage::onCopyWindowTitle);

    auto* copyHandleAction = m_contextMenu->addAction(QStringLiteral("复制窗口句柄"));
    connect(copyHandleAction, &QAction::triggered, this, &WindowPage::onCopyWindowHandle);

    auto* openProcAction = m_contextMenu->addAction(QStringLiteral("打开进程位置"));
    connect(openProcAction, &QAction::triggered, this, &WindowPage::onOpenProcessLocation);
}

void WindowPage::refreshWindows()
{
    EnumWindowData data;
    data.showHidden = m_showHidden->isChecked();
    data.showNoTitle = m_showNoTitle->isChecked();
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));

    m_table->setRowCount(0);
    m_table->setRowCount(data.hwnds.size());

    for (int i = 0; i < data.hwnds.size(); ++i) {
        HWND hwnd = data.hwnds[i];

        // 句柄
        m_table->setItem(i, Col_Handle,
            new QTableWidgetItem(QStringLiteral("0x%1").arg((quintptr)hwnd, 0, 16)));

        // 标题
        wchar_t title[256] = {0};
        GetWindowTextW(hwnd, title, 256);
        m_table->setItem(i, Col_Title, new QTableWidgetItem(QString::fromWCharArray(title)));

        // 类名
        wchar_t className[256] = {0};
        GetClassNameW(hwnd, className, 256);
        m_table->setItem(i, Col_Class, new QTableWidgetItem(QString::fromWCharArray(className)));

        // TID 和 PID
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        m_table->setItem(i, Col_TID, new QTableWidgetItem(QString::number(tid)));
        m_table->setItem(i, Col_PID, new QTableWidgetItem(QString::number(pid)));

        // 进程名
        m_table->setItem(i, Col_ProcessName, new QTableWidgetItem(getProcessNameByPid(pid)));

        // 状态
        QStringList status;
        if (IsWindowVisible(hwnd)) status << QStringLiteral("可见");
        else status << QStringLiteral("隐藏");
        if (IsWindowEnabled(hwnd)) status << QStringLiteral("启用");
        else status << QStringLiteral("禁用");
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_MINIMIZE) status << QStringLiteral("最小化");
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_MAXIMIZE) status << QStringLiteral("最大化");
        if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) status << QStringLiteral("置顶");
        m_table->setItem(i, Col_Status, new QTableWidgetItem(status.join(", ")));
    }

    m_statusLabel->setText(QStringLiteral("窗口数: %1").arg(data.hwnds.size()));
}

void WindowPage::onRefreshClicked()
{
    refreshWindows();
}

void WindowPage::onShowHiddenToggled(bool checked)
{
    Q_UNUSED(checked);
    refreshWindows();
}

HWND WindowPage::currentHwnd() const
{
    int row = m_table->currentRow();
    if (row < 0) return nullptr;
    auto* item = m_table->item(row, Col_Handle);
    if (!item) return nullptr;
    QString text = item->text();
    bool ok = false;
    return reinterpret_cast<HWND>(text.toULongLong(&ok, 16));
}

void WindowPage::onContextMenu(const QPoint& pos)
{
    QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid()) return;
    m_contextMenu->exec(m_table->viewport()->mapToGlobal(pos));
}

void WindowPage::onCloseWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    QTimer::singleShot(500, this, &WindowPage::refreshWindows);
}

void WindowPage::onForceCloseWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("强制关闭窗口可能导致进程数据丢失!确定继续吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    // 强制关闭窗口（不结束进程）：发送多种关闭消息
    // 如果是命令行窗口，额外发 Ctrl 信号
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    // 检测是否为控制台窗口
    wchar_t cls[64] = {0};
    GetClassNameW(hwnd, cls, 64);
    bool isConsole = (wcscmp(cls, L"ConsoleWindowClass") == 0);

    if (isConsole && pid != 0) {
        // 控制台窗口：发送 Ctrl+C / Ctrl+Break / Ctrl+Close
        FreeConsole();
        if (AttachConsole(pid)) {
            GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
            FreeConsole();
        }
        // 控制台的 Ctrl+Close 通过 WM_CLOSE 触发
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    // 向窗口发送关闭消息序列
    PostMessageW(hwnd, WM_DESTROY, 0, 0);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    PostMessageW(hwnd, WM_NCDESTROY, 0, 0);
    PostMessageW(hwnd, WM_QUERYENDSESSION, 0, 0);

    QTimer::singleShot(500, this, &WindowPage::refreshWindows);
}

void WindowPage::onShowWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOW);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onHideWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    ShowWindow(hwnd, SW_HIDE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onForceShowWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    // 对应易语言: ShowWindow + 窗口_显示隐藏 + 窗口_强制显示
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onRestoreWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    ShowWindow(hwnd, SW_RESTORE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onMinimizeWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    ShowWindow(hwnd, SW_MINIMIZE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onMaximizeWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    ShowWindow(hwnd, SW_MAXIMIZE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onDisableWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    EnableWindow(hwnd, FALSE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onEnableWindow()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    EnableWindow(hwnd, TRUE);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onSetTopmost()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onUnsetTopmost()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    QTimer::singleShot(300, this, &WindowPage::refreshWindows);
}

void WindowPage::onSetTransparent()
{
    HWND hwnd = currentHwnd();
    if (!hwnd) return;

    bool ok = false;
    int alpha = QInputDialog::getInt(this, QStringLiteral("设置透明度"),
        QStringLiteral("透明度 (0-255, 0=完全透明, 255=不透明):"), 128, 0, 255, 1, &ok);
    if (!ok) return;

    // 使用 SetLayeredWindowAttributes 设置透明度
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
}

void WindowPage::onCopyWindowTitle()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* item = m_table->item(row, Col_Title);
    if (item) QGuiApplication::clipboard()->setText(item->text());
}

void WindowPage::onCopyWindowHandle()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* item = m_table->item(row, Col_Handle);
    if (item) QGuiApplication::clipboard()->setText(item->text());
}

void WindowPage::onOpenProcessLocation()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* pidItem = m_table->item(row, Col_PID);
    if (!pidItem) return;
    quint32 pid = pidItem->text().toUInt();
    if (pid == 0) return;

    // 获取进程路径
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return;
    wchar_t path[MAX_PATH] = {0};
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, path, &len)) {
        QProcess::startDetached(QStringLiteral("explorer.exe /select,\"%1\"")
            .arg(QString::fromWCharArray(path)));
    }
    CloseHandle(hProc);
}
