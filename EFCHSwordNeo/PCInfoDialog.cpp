// 必须在 windows.h 之前定义，防止 windows.h 包含 winsock.h 与 winsock2.h 冲突
#define _WINSOCKAPI_
#include "PCInfoDialog.h"
#include "AppContext.h"

#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QProcess>
#include <QSettings>

#include <winsock2.h>
#include <windows.h>
#include <devguid.h>   // GUID_DEVCLASS_DISPLAY / GUID_DEVCLASS_NET
#include <setupapi.h>
#include <iphlpapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

PCInfoDialog::PCInfoDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadOsInfo();
    loadCpuInfo();
    loadMemoryInfo();
    loadVideoCards();
    loadNetworkCards();
}

PCInfoDialog::~PCInfoDialog() = default;

void PCInfoDialog::setupUi()
{
    setWindowTitle(QStringLiteral("PC 信息"));
    resize(700, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // OS 信息
    auto* osGroup = new QGroupBox(QStringLiteral("操作系统"), this);
    auto* osLayout = new QFormLayout(osGroup);
    m_osLabel = new QLabel(this);
    m_osLabel->setWordWrap(true);
    osLayout->addRow(QStringLiteral("系统信息:"), m_osLabel);
    mainLayout->addWidget(osGroup);

    // CPU 信息
    auto* cpuGroup = new QGroupBox(QStringLiteral("CPU"), this);
    auto* cpuLayout = new QFormLayout(cpuGroup);
    m_cpuLabel = new QLabel(this);
    m_cpuLabel->setWordWrap(true);
    cpuLayout->addRow(QStringLiteral("CPU 信息:"), m_cpuLabel);
    mainLayout->addWidget(cpuGroup);

    // 内存信息
    auto* memGroup = new QGroupBox(QStringLiteral("内存"), this);
    auto* memLayout = new QFormLayout(memGroup);
    m_memLabel = new QLabel(this);
    m_memLabel->setWordWrap(true);
    memLayout->addRow(QStringLiteral("内存信息:"), m_memLabel);
    mainLayout->addWidget(memGroup);

    // 显卡列表
    auto* videoGroup = new QGroupBox(QStringLiteral("显卡"), this);
    auto* videoLayout = new QVBoxLayout(videoGroup);
    m_videoTable = new QTableWidget(this);
    m_videoTable->setColumnCount(3);
    m_videoTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("驱动版本"), QStringLiteral("适配器信息")});
    m_videoTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_videoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    videoLayout->addWidget(m_videoTable);
    mainLayout->addWidget(videoGroup);

    // 网卡列表
    auto* netGroup = new QGroupBox(QStringLiteral("网卡"), this);
    auto* netLayout = new QVBoxLayout(netGroup);
    m_netTable = new QTableWidget(this);
    m_netTable->setColumnCount(3);
    m_netTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("MAC 地址"), QStringLiteral("描述")});
    m_netTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_netTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    netLayout->addWidget(m_netTable);
    mainLayout->addWidget(netGroup);
}

void PCInfoDialog::loadOsInfo()
{
    auto& ctx = AppContext::instance();
    QString info;

    info += QStringLiteral("系统: %1\n").arg(ctx.winName());
    info += QStringLiteral("版本: %1.%2\n").arg(ctx.winMajor()).arg(ctx.winMinor());
    info += QStringLiteral("Build: %1\n").arg(ctx.winBuild());

    // 计算机名
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &size);
    info += QStringLiteral("计算机名: %1\n").arg(QString::fromWCharArray(computerName));

    // 用户名
    wchar_t userName[256] = {0};
    size = 256;
    GetUserNameW(userName, &size);
    info += QStringLiteral("用户名: %1\n").arg(QString::fromWCharArray(userName));

    // 系统目录
    wchar_t sysDir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    info += QStringLiteral("系统目录: %1\n").arg(QString::fromWCharArray(sysDir));

    // Windows 目录
    wchar_t winDir[MAX_PATH] = {0};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    info += QStringLiteral("Windows 目录: %1\n").arg(QString::fromWCharArray(winDir));

    // 开机时间
    DWORD uptime = GetTickCount64() / 1000;
    info += QStringLiteral("运行时间: %1 秒 (%2 小时 %3 分 %4 秒)")
        .arg(uptime).arg(uptime / 3600).arg((uptime % 3600) / 60).arg(uptime % 60);

    m_osLabel->setText(info);
}

void PCInfoDialog::loadCpuInfo()
{
    QString info;

    // CPU 架构和数量
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    info += QStringLiteral("架构: ");
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: info += QStringLiteral("x64 (AMD/Intel)\n"); break;
        case PROCESSOR_ARCHITECTURE_ARM: info += QStringLiteral("ARM\n"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: info += QStringLiteral("ARM64\n"); break;
        default: info += QStringLiteral("未知\n"); break;
    }
    info += QStringLiteral("处理器数量: %1\n").arg(si.dwNumberOfProcessors);

    // CPU 频率
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    info += QStringLiteral("性能计数器频率: %1 Hz\n").arg(freq.QuadPart);

    // CPU 型号（从注册表读取）
    QSettings cpuReg(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                     QSettings::NativeFormat);
    QString cpuName = cpuReg.value(QStringLiteral("ProcessorNameString")).toString();
    QString vendor = cpuReg.value(QStringLiteral("VendorIdentifier")).toString();
    info += QStringLiteral("型号: %1\n").arg(cpuName);
    info += QStringLiteral("厂商: %1\n").arg(vendor);

    // CPU 频率 (MHz)
    DWORD mhz = cpuReg.value(QStringLiteral("~MHz")).toUInt();
    info += QStringLiteral("主频: %1 MHz").arg(mhz);

    m_cpuLabel->setText(info);
}

void PCInfoDialog::loadMemoryInfo()
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    QString info;
    info += QStringLiteral("物理内存: %1 / %2 MB (%3% 已用)\n")
        .arg((ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024))
        .arg(ms.ullTotalPhys / (1024 * 1024))
        .arg(ms.dwMemoryLoad);
    info += QStringLiteral("可用物理内存: %1 MB\n").arg(ms.ullAvailPhys / (1024 * 1024));
    info += QStringLiteral("虚拟内存: %1 / %2 MB\n")
        .arg((ms.ullTotalVirtual - ms.ullAvailVirtual) / (1024 * 1024))
        .arg(ms.ullTotalVirtual / (1024 * 1024));
    info += QStringLiteral("可用虚拟内存: %1 MB").arg(ms.ullAvailVirtual / (1024 * 1024));

    m_memLabel->setText(info);
}

// 通过 SetupAPI 枚举设备
static QStringList EnumDevices(const GUID* classGuid)
{
    QStringList result;
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(classGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_PROFILE);
    if (hDevInfo == INVALID_HANDLE_VALUE) return result;

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(devData);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); ++i) {
        WCHAR friendlyName[512] = {0};
        DWORD size = 0;
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_FRIENDLYNAME,
                nullptr, (PBYTE)friendlyName, sizeof(friendlyName), &size)) {
            result.append(QString::fromWCharArray(friendlyName));
        } else {
            // 尝试 DEVICEDESC
            if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData, SPDRP_DEVICEDESC,
                    nullptr, (PBYTE)friendlyName, sizeof(friendlyName), &size)) {
                result.append(QString::fromWCharArray(friendlyName));
            } else {
                result.append(QStringLiteral("(未知设备)"));
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return result;
}

void PCInfoDialog::loadVideoCards()
{
    QStringList cards = EnumDevices(&GUID_DEVCLASS_DISPLAY);
    m_videoTable->setRowCount(cards.size());
    for (int i = 0; i < cards.size(); ++i) {
        m_videoTable->setItem(i, 0, new QTableWidgetItem(cards[i]));
        m_videoTable->setItem(i, 1, new QTableWidgetItem(QStringLiteral("-")));
        m_videoTable->setItem(i, 2, new QTableWidgetItem(QStringLiteral("-")));
    }
}

void PCInfoDialog::loadNetworkCards()
{
    QStringList adapters = EnumDevices(&GUID_DEVCLASS_NET);
    m_netTable->setRowCount(adapters.size());

    // 获取 MAC 地址
    ULONG bufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen)
        == ERROR_BUFFER_OVERFLOW) {
        QByteArray buf(bufLen, 0);
        PIP_ADAPTER_ADDRESSES pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddrs, &bufLen)
            == NO_ERROR) {
            int row = 0;
            for (PIP_ADAPTER_ADDRESSES pCur = pAddrs; pCur; pCur = pCur->Next) {
                if (pCur->OperStatus != IfOperStatusUp) continue;
                if (row >= adapters.size()) break;

                // MAC 地址
                QString mac;
                for (ULONG i = 0; i < pCur->PhysicalAddressLength; ++i) {
                    mac += QString::asprintf("%02X-", pCur->PhysicalAddress[i]);
                }
                if (mac.endsWith('-')) mac.chop(1);

                m_netTable->setItem(row, 1, new QTableWidgetItem(mac));

                // 描述
                QString desc = QString::fromWCharArray(pCur->Description);
                m_netTable->setItem(row, 2, new QTableWidgetItem(desc));
                ++row;
            }
        }
    }

    // 填充名称
    for (int i = 0; i < adapters.size(); ++i) {
        if (!m_netTable->item(i, 0)) {
            m_netTable->setItem(i, 0, new QTableWidgetItem(adapters[i]));
        }
        if (!m_netTable->item(i, 1)) {
            m_netTable->setItem(i, 1, new QTableWidgetItem(QStringLiteral("-")));
        }
        if (!m_netTable->item(i, 2)) {
            m_netTable->setItem(i, 2, new QTableWidgetItem(QStringLiteral("-")));
        }
    }
}
