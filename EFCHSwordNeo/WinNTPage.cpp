#include "WinNTPage.h"
#include "AppContext.h"
#include "PCInfoDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>

#include <windows.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

WinNTPage::WinNTPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    loadSystemInfo();
    loadKernelInfo();
    loadHardwareInfo();
}

WinNTPage::~WinNTPage() = default;

void WinNTPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // 操作系统信息
    auto* osGroup = new QGroupBox(QStringLiteral("操作系统信息"), this);
    auto* osLayout = new QFormLayout(osGroup);
    m_osLabel = new QLabel(osGroup);
    m_osLabel->setWordWrap(true);
    osLayout->addRow(QStringLiteral("系统:"), m_osLabel);
    mainLayout->addWidget(osGroup);

    // CPU 信息
    auto* cpuGroup = new QGroupBox(QStringLiteral("CPU 信息"), this);
    auto* cpuLayout = new QFormLayout(cpuGroup);
    m_cpuLabel = new QLabel(cpuGroup);
    m_cpuLabel->setWordWrap(true);
    cpuLayout->addRow(QStringLiteral("处理器:"), m_cpuLabel);
    mainLayout->addWidget(cpuGroup);

    // 内存信息
    auto* memGroup = new QGroupBox(QStringLiteral("内存信息"), this);
    auto* memLayout = new QFormLayout(memGroup);
    m_memLabel = new QLabel(memGroup);
    m_memLabel->setWordWrap(true);
    memLayout->addRow(QStringLiteral("内存:"), m_memLabel);
    mainLayout->addWidget(memGroup);

    // 内核信息
    auto* kernelGroup = new QGroupBox(QStringLiteral("内核信息"), this);
    auto* kernelLayout = new QFormLayout(kernelGroup);
    m_kernelLabel = new QLabel(kernelGroup);
    m_kernelLabel->setWordWrap(true);
    kernelLayout->addRow(QStringLiteral("内核:"), m_kernelLabel);
    mainLayout->addWidget(kernelGroup);

    // 已加载驱动列表
    auto* driverGroup = new QGroupBox(QStringLiteral("已加载驱动"), this);
    auto* driverLayout = new QVBoxLayout(driverGroup);

    auto* btnLayout = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), driverGroup);
    auto* pcInfoBtn = new QPushButton(QStringLiteral("PC 信息"), driverGroup);
    connect(refreshBtn, &QPushButton::clicked, this, &WinNTPage::onRefresh);
    connect(pcInfoBtn, &QPushButton::clicked, this, [this]() {
        PCInfoDialog dlg(this);
        dlg.exec();
    });
    btnLayout->addWidget(refreshBtn);
    btnLayout->addWidget(pcInfoBtn);
    btnLayout->addStretch();
    driverLayout->addLayout(btnLayout);

    m_driverTable = new QTableWidget(driverGroup);
    m_driverTable->setColumnCount(2);
    m_driverTable->setHorizontalHeaderLabels({QStringLiteral("驱动名称"), QStringLiteral("驱动路径")});
    m_driverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_driverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_driverTable->setAlternatingRowColors(true);
    m_driverTable->verticalHeader()->setVisible(false);
    m_driverTable->horizontalHeader()->setStretchLastSection(true);
    m_driverTable->horizontalHeader()->setHighlightSections(false);
    m_driverTable->setColumnWidth(0, 200);
    driverLayout->addWidget(m_driverTable);

    mainLayout->addWidget(driverGroup);
}

void WinNTPage::loadSystemInfo()
{
    auto& ctx = AppContext::instance();
    QString info = QStringLiteral("%1 NT版本: %2.%3 Build: %4")
        .arg(ctx.winName())
        .arg(ctx.winMajor())
        .arg(ctx.winMinor())
        .arg(ctx.winBuild());

    // 计算机名
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &size);
    info += QStringLiteral("\n计算机名: ") + QString::fromWCharArray(computerName);

    // 用户名
    wchar_t userName[256] = {0};
    size = 256;
    GetUserNameW(userName, &size);
    info += QStringLiteral("\n当前用户: ") + QString::fromWCharArray(userName);

    // 系统目录
    wchar_t sysDir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    info += QStringLiteral("\n系统目录: ") + QString::fromWCharArray(sysDir);

    // Windows 目录
    wchar_t winDir[MAX_PATH] = {0};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    info += QStringLiteral("\nWindows目录: ") + QString::fromWCharArray(winDir);

    m_osLabel->setText(info);
}

void WinNTPage::loadKernelInfo()
{
    auto& ctx = AppContext::instance();
    QString info = QStringLiteral("NT版本: %1.%2 Build: %3")
        .arg(ctx.winMajor())
        .arg(ctx.winMinor())
        .arg(ctx.winBuild());

    // 系统启动时间
    DWORD tickCount = GetTickCount64() / 1000;
    int hours = tickCount / 3600;
    int mins = (tickCount % 3600) / 60;
    int secs = tickCount % 60;
    info += QStringLiteral("\n运行时间: %1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));

    m_kernelLabel->setText(info);
}

void WinNTPage::loadHardwareInfo()
{
    // CPU 信息
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    QString cpuInfo = QStringLiteral("处理器架构: ");
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: cpuInfo += QStringLiteral("x64 (AMD/Intel)"); break;
        case PROCESSOR_ARCHITECTURE_ARM: cpuInfo += QStringLiteral("ARM"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: cpuInfo += QStringLiteral("ARM64"); break;
        case PROCESSOR_ARCHITECTURE_INTEL: cpuInfo += QStringLiteral("x86"); break;
        default: cpuInfo += QStringLiteral("未知"); break;
    }
    cpuInfo += QStringLiteral("\n处理器数量: %1").arg(si.dwNumberOfProcessors);
    cpuInfo += QStringLiteral("\n处理器级别: %1").arg(si.wProcessorLevel);
    cpuInfo += QStringLiteral("\n处理器修订: 0x%1").arg(si.wProcessorRevision, 4, 16, QChar('0'));
    cpuInfo += QStringLiteral("\n页面大小: %1 KB").arg(si.dwPageSize / 1024);
    cpuInfo += QStringLiteral("\n最小/最大应用地址: 0x%1 / 0x%2")
        .arg((quintptr)si.lpMinimumApplicationAddress, 0, 16)
        .arg((quintptr)si.lpMaximumApplicationAddress, 0, 16);

    // CPU 频率
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        if (RegQueryValueExW(hKey, L"~MHz", nullptr, nullptr, (LPBYTE)&mhz, &size) == ERROR_SUCCESS) {
            cpuInfo += QStringLiteral("\nCPU 频率: %1 MHz").arg(mhz);
        }

        wchar_t name[256] = {0};
        size = sizeof(name);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, (LPBYTE)name, &size) == ERROR_SUCCESS) {
            cpuInfo += QStringLiteral("\nCPU 名称: ") + QString::fromWCharArray(name);
        }
        RegCloseKey(hKey);
    }

    m_cpuLabel->setText(cpuInfo);

    // 内存信息
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    QString memInfo;
    memInfo += QStringLiteral("总物理内存: %1 GB").arg((double)ms.ullTotalPhys / (1024.0 * 1024 * 1024), 0, 'f', 2);
    memInfo += QStringLiteral("\n可用物理内存: %1 GB").arg((double)ms.ullAvailPhys / (1024.0 * 1024 * 1024), 0, 'f', 2);
    memInfo += QStringLiteral("\n物理内存使用率: %1%").arg(ms.dwMemoryLoad);
    memInfo += QStringLiteral("\n总虚拟内存: %1 GB").arg((double)ms.ullTotalVirtual / (1024.0 * 1024 * 1024), 0, 'f', 2);
    memInfo += QStringLiteral("\n可用虚拟内存: %1 GB").arg((double)ms.ullAvailVirtual / (1024.0 * 1024 * 1024), 0, 'f', 2);

    m_memLabel->setText(memInfo);

    // 加载驱动列表
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        QStringList drivers = ctx.driver().enumDriversPath();
        m_driverTable->setRowCount(drivers.size());
        for (int i = 0; i < drivers.size(); ++i) {
            // 分离名称和路径
            QString entry = drivers[i];
            // 驱动枚举返回的格式可能不同，直接显示
            m_driverTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_driverTable->setItem(i, 1, new QTableWidgetItem(entry));
        }
    } else {
        // 使用 EnumDeviceDrivers
        LPVOID drivers[1024];
        DWORD cbNeeded = 0;
        if (EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded) && cbNeeded < sizeof(drivers)) {
            int count = cbNeeded / sizeof(LPVOID);
            m_driverTable->setRowCount(count);
            for (int i = 0; i < count; ++i) {
                wchar_t name[MAX_PATH] = {0};
                GetDeviceDriverBaseNameW(drivers[i], name, MAX_PATH);
                m_driverTable->setItem(i, 0, new QTableWidgetItem(QString::fromWCharArray(name)));

                wchar_t path[MAX_PATH] = {0};
                GetDeviceDriverFileNameW(drivers[i], path, MAX_PATH);
                m_driverTable->setItem(i, 1, new QTableWidgetItem(QString::fromWCharArray(path)));
            }
        }
    }
}

void WinNTPage::onRefresh()
{
    loadSystemInfo();
    loadKernelInfo();
    loadHardwareInfo();
}
