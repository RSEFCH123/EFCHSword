#include "MainWindow.h"
#include "KernelPage.h"        // 必须在 AppContext.h(windows.h) 之前: winsock2 顺序
#include "NetMonitorPage.h"    // 同上 (NetTable.h -> winsock2.h)
#include "AppContext.h"
#include "WelcomePage.h"
#include "ProcessPage.h"
#include "WindowPage.h"
#include "FilePage.h"
#include "ToolsPage.h"
#include "StartupPage.h"
#include "RegeditPage.h"
#include "WinNTPage.h"
#include "AboutPage.h"
#include "SystemThreadPage.h"
#include "ProcessBehaviorPage.h"
#include "EtwMonitorPage.h"
#include "HypervisorPage.h"
#include "ThemeManager.h"

#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <QRandomGenerator>

#include <windows.h>
#include <psapi.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setupUi();
    setupStatusBar();
    updateWindowTitle();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setMinimumSize(1000, 650);

    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航栏（样式由 ThemeManager 统一管理）
    m_navList = new QListWidget(this);
    m_navList->setObjectName("navList");
    m_navList->setFixedWidth(160);
    m_navList->setIconSize(QSize(18, 18));

    // 导航项
    m_navList->addItem(QStringLiteral("  欢迎"));
    m_navList->addItem(QStringLiteral("  系统进程"));
    m_navList->addItem(QStringLiteral("  系统线程"));
    m_navList->addItem(QStringLiteral("  行为监视"));
    m_navList->addItem(QStringLiteral("  ETW 监视"));
    m_navList->addItem(QStringLiteral("  窗口列表"));
    m_navList->addItem(QStringLiteral("  文件列表"));
    m_navList->addItem(QStringLiteral("  启动项/服务"));
    m_navList->addItem(QStringLiteral("  工具"));
    m_navList->addItem(QStringLiteral("  注册表"));
    m_navList->addItem(QStringLiteral("  WinNT"));
    m_navList->addItem(QStringLiteral("  内核"));
    m_navList->addItem(QStringLiteral("  Hypervisor"));
    m_navList->addItem(QStringLiteral("  网络监控"));
    m_navList->addItem(QStringLiteral("  关于"));

    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem* item = m_navList->item(i);
        item->setSizeHint(QSize(160, 42));
    }

    // 右侧内容区
    m_stack = new QStackedWidget(this);

    // 创建各页面
    m_welcomePage = new WelcomePage(this);
    m_processPage = new ProcessPage(this);
    m_systemThreadPage = new SystemThreadPage(this);
    m_behaviorPage = new ProcessBehaviorPage(this);
    m_etwPage = new EtwMonitorPage(this);
    m_windowPage = new WindowPage(this);
    m_filePage = new FilePage(this);
    m_startupPage = new StartupPage(this);
    m_toolsPage = new ToolsPage(this);
    m_regeditPage = new RegeditPage(this);
    m_winntPage = new WinNTPage(this);
    m_kernelPage = new KernelPage(this);
    m_hypervisorPage = new HypervisorPage(this);
    m_netMonitorPage = new NetMonitorPage(this);
    m_aboutPage = new AboutPage(this);

    m_stack->addWidget(m_welcomePage);
    m_stack->addWidget(m_processPage);
    m_stack->addWidget(m_systemThreadPage);
    m_stack->addWidget(m_behaviorPage);
    m_stack->addWidget(m_etwPage);
    m_stack->addWidget(m_windowPage);
    m_stack->addWidget(m_filePage);
    m_stack->addWidget(m_startupPage);
    m_stack->addWidget(m_toolsPage);
    m_stack->addWidget(m_regeditPage);
    m_stack->addWidget(m_winntPage);
    m_stack->addWidget(m_kernelPage);
    m_stack->addWidget(m_hypervisorPage);
    m_stack->addWidget(m_netMonitorPage);
    m_stack->addWidget(m_aboutPage);

    mainLayout->addWidget(m_navList);
    mainLayout->addWidget(m_stack, 1);

    setCentralWidget(centralWidget);

    connect(m_navList, &QListWidget::currentRowChanged, this, &MainWindow::onNavigateChanged);
    m_navList->setCurrentRow(0);

    // 监听主题变化
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
}

void MainWindow::setupStatusBar()
{
    auto* sb = statusBar();

    auto& ctx = AppContext::instance();

    m_statusProcessCount = new QLabel(QStringLiteral("进程数: 0"), this);
    m_statusCpu = new QLabel(QStringLiteral("CPU: 0%"), this);
    m_statusMemory = new QLabel(QStringLiteral("内存: 0%"), this);
    m_statusDriverVer = new QLabel(QStringLiteral("驱动: -"), this);
    m_statusVersion = new QLabel(QStringLiteral("版本: ") + ctx.version(), this);

    sb->addWidget(m_statusProcessCount);
    sb->addWidget(m_statusCpu);
    sb->addWidget(m_statusMemory);
    sb->addWidget(m_statusDriverVer);
    sb->addPermanentWidget(m_statusVersion);

    // 定时更新状态栏
    auto* timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, [this]() {
        // CPU 使用率（简化）
        // 内存使用率
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        m_statusMemory->setText(QStringLiteral("物理内存: %1%").arg(ms.dwMemoryLoad));

        // 进程数
        DWORD count = 0;
        DWORD needed = 0;
        EnumProcesses(nullptr, 0, &needed);
        count = needed / sizeof(DWORD);
        m_statusProcessCount->setText(QStringLiteral("进程数: %1").arg(count));

        // 驱动版本（排查旧驱动残留）
        if (AppContext::instance().driverLoaded()) {
            m_statusDriverVer->setText(QStringLiteral("驱动: %1")
                .arg(AppContext::instance().driver().getDriverVersion()));
        } else {
            m_statusDriverVer->setText(QStringLiteral("驱动: 未加载"));
        }
    });
    timer->start();
}

void MainWindow::updateWindowTitle()
{
    auto& ctx = AppContext::instance();

    if (ctx.fakeMode()) {
        // FakeMode 下使用随机标题（对应易语言: 文本_取随机字符）
        int len = QRandomGenerator::global()->bounded(10, 21);
        const QChar chars[] = {
            QChar('a'), QChar('b'), QChar('c'), QChar('d'), QChar('e'),
            QChar('A'), QChar('B'), QChar('C'), QChar('D'), QChar('E'),
            QChar('1'), QChar('2'), QChar('3'), QChar('4'), QChar('5')
        };
        QString random;
        for (int i = 0; i < len; ++i) {
            random += chars[QRandomGenerator::global()->bounded(0, 15)];
        }
        setWindowTitle(random);
        return;
    }

    QString title = QStringLiteral("EFCHSword - 计算机监视与管理软件 for Windows NT [%1 NTVersion:%2.%3 Build:%4] %5")
        .arg(ctx.winName()).arg(ctx.winMajor()).arg(ctx.winMinor()).arg(ctx.winBuild()).arg(ctx.betaVersion());
    if (!ctx.driverLoaded()) {
        title += QStringLiteral(" {Driver not loaded}");
    }
    setWindowTitle(title);
}

void MainWindow::onNavigateChanged(int index)
{
    if (index >= 0 && index < m_stack->count()) {
        m_stack->setCurrentIndex(index);
        // 进入欢迎页时刷新状态
        if (index == 0 && m_welcomePage) {
            m_welcomePage->refreshStatus();
        }
    }
}

void MainWindow::onThemeChanged(int theme)
{
    Q_UNUSED(theme);
    // 主题样式由 ThemeManager 全局应用，这里可做额外调整
    updateWindowTitle();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (QMessageBox::question(this, QStringLiteral("EFCHSword"),
        QStringLiteral("确定退出 EFCHSword 吗?"),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        event->ignore();
        return;
    }

    // 全部清理工作交给 AppContext::cleanup()
    // 不要在此直接调用驱动的 un* 接口：非 FakeMode 下 protectProcess 之外的保护
    // （hideProcess / forceProtectProcess / setProcessCritical）从未被应用，
    // 对未保护的进程调用 unHideProcess 会触发驱动的"重新挂回 ActiveProcessLinks"
    // 逻辑，导致双向链表指针被重复插入而损坏，退出/枚举进程时触发
    // BugCheck 0x50 / 0x139 (FAST_FAIL_CORRUPT_LIST_ENTRY)。
    // cleanup() 内部会按需调用 revokeFakeModeProtections()（仅 FakeMode 生效）
    // 并撤销 initialize() 中做过的 protectProcess / setPPL。
    AppContext::instance().cleanup();
    event->accept();
}
