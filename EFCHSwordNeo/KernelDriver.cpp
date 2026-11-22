#include "KernelDriver.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>

#include <winternl.h>

// NtLoadDriver / NtUnloadDriver 函数类型
typedef LONG(NTAPI* NtLoadDriver_t)(PUNICODE_STRING DriverServiceName);
typedef LONG(NTAPI* NtUnloadDriver_t)(PUNICODE_STRING DriverServiceName);
#define STATUS_SUCCESS_NT ((LONG)0x00000000L)

// IOCTL 功能码定义（与驱动 hello.cpp 中的定义一致）
// Process
#define IOCTL_KILLPROCESS              0x800
#define IOCTL_SUSPENDPROCESS           0x801
#define IOCTL_RESUMEPROCESS            0x802
#define IOCTL_HIDEPROCESS              0x803
#define IOCTL_UNHIDEPROCESS            0x804
#define IOCTL_PROTECTPROCESS           0x805
#define IOCTL_UNPROTECTPROCESS         0x806
#define IOCTL_MEMKILLPROCESS           0x807
#define IOCTL_SETPPL                   0x808
#define IOCTL_SETPID                   0x809
#define IOCTL_SETPROCESSCRITICAL       0x80A
#define IOCTL_EPROCESSENUMPROCESS      0x80B
#define IOCTL_GETPPL                   0x80C
#define IOCTL_APCTERMINATEPROCESS      0x80D
#define IOCTL_GETPROCESSIMAGENAME      0x80E
#define IOCTL_FORCEHIDEPROCESS         0x80F
#define IOCTL_UNFORCEHIDEPROCESS       0x810
// ForceProtect/UnForceProtect 在易语言中用 2063/2064 = 0x80F/0x810
// 但驱动里 0x80F/0x810 是 FORCEHIDE/UNFORCEHIDE。易语言代码:
//   ForceProtectProcess -> Ioctl_Code(34, 2063, 0, 0) = 0x80F
//   UnForceProtectProcess -> Ioctl_Code(34, 2064, 0, 0) = 0x810
// 这里存在易语言版本与驱动定义的命名差异，按易语言实际调用码为准
#define IOCTL_FORCEPROTECTPROCESS      0x80F
#define IOCTL_UNFORCEPROTECTPROCESS    0x810
#define IOCTL_ENUMTHREADBYPROCESS      0x811   // 输入PID，输出 ULONG count + THREAD_OUTPUT_ENTRY[]
#define IOCTL_GETDRIVERPATCHBYETHREAD  0x812   // 输入PETHREAD(8B)，输出 WCHAR[] 驱动完整路径

// Disk / FileSystem
#define IOCTL_NTFSDELETEFILE           0x900
#define IOCTL_ZWDELETEFILE             0x901
#define IOCTL_OCCUPYFILE               0x902
#define IOCTL_WRITEDISK                0x903
#define IOCTL_FORCEDELETEFILE          0x905
#define IOCTL_FORCEWRITEDISK           0x906
#define IOCTL_FORCEOCCUPYFILE          0x907
#define IOCTL_FORCEWRITEFILE           0x908
#define IOCTL_IRPQUERYFILEANDFOLDERS   0x909
#define IOCTL_IRPSETINFORMATIONFILE    0x90A
#define IOCTL_IRPRENAMEFILE            0x90B
#define IOCTL_IRPCREATEFILE            0x90C
#define IOCTL_IRPCREATEDIRECTORY       0x90D
#define IOCTL_IRPCOPYFILE              0x90E
#define IOCTL_IRPMOVEFILE              0x90F
#define IOCTL_ZWWRITEFILE              0x910
#define IOCTL_ZWREADFILE               0x911
#define IOCTL_IRPREADFILE              0x912
#define IOCTL_IRPGETFILESIZE           0x913
#define IOCTL_IRPREADDISK              0x914
#define IOCTL_IRPGETFILEANDFOLDERQUANTITY 0x915
#define IOCTL_POSIXFORCEDELETEFILE     0x916
#define IOCTL_STORDIRECT_WRITEDISK     0x917
#define IOCTL_STORDIRECT_READDISK      0x918
#define IOCTL_GETVOLUMERAIDPORT        0x919
#define IOCTL_FLUSHMFT                 0x920
#define IOCTL_NTFS_INIT                0x921   // 输入WCHAR盘符, 初始化NTFS卷上下文
#define IOCTL_NTFS_READFILE            0x922   // 输入4096B路径, 输出文件内容
#define IOCTL_NTFS_ENUMDIR             0x925   // 输入路径, 输出 NTFS_DATA_INFO[]
#define IOCTL_NTFS_GETQTY              0x926   // 输入路径, 输出 NTFS_FILE_FOLDER_QUANTITY
#define IOCTL_NTFS_DELETEFILE          0x927   // 输入路径(UTF-16), 删除文件或目录(直接解析NTFS)

// Thread
#define IOCTL_SUSPENDTHREAD            0xA01
#define IOCTL_RESUMETHREAD             0xA02
#define IOCTL_TERMINATETHREAD          0xA03
#define IOCTL_PSPTERMINATEPROCESS_FAKE 0xA04
#define IOCTL_PSPTERMINATETHREADBYPOINTER_EASY 0xA05
#define IOCTL_PSPTERMINATETHREADBYPOINTER      0xA06
#define IOCTL_APCTERMINATETHREAD       0xA07
#define IOCTL_APCTERMINATETHREADBYETHREAD 0xA08  // 输入PETHREAD(8B), APC方式销毁线程

// System
#define IOCTL_KEBUGCHECK               0xB00
#define IOCTL_SYSTEMBSOD               0xB01
#define IOCTL_FORCEUNLOADDRIVER        0xB02
#define IOCTL_HALTCPU                  0xB03
#define IOCTL_HALTALLCPUS              0xB04
#define IOCTL_HALPOWERCONTROL          0xB05
#define IOCTL_INTELVT_SUPPORT          0xB06
#define IOCTL_INTELVT_INIT             0xB07
#define IOCTL_INTELVT_STOP             0xB08
#define IOCTL_BSOD_RAINBOW             0xB09
#define IOCTL_BSOD_SETCOLOR            0xB0A
#define IOCTL_SENDDATA_TO_PORT         0xB0B
#define IOCTL_READDATA_IN_PORT         0xB0C
#define IOCTL_ENUMDRIVERS              0xB0D
#define IOCTL_ENUMDEVICES              0xB0E
#define IOCTL_FORCEUNLOADDEVICE        0xB0F
#define IOCTL_ENUMDRIVERSPATH          0xB10
#define IOCTL_ANTIDRIVERIOCONTROL      0xB11
#define IOCTL_DISENA_SYSTEMFUNCTION    0xB12
#define IOCTL_BSOD_SETSTOPCODE         0xB13

// Registry
#define IOCTL_WRITEREGISTERKEY         0xC00
#define IOCTL_DELETEREGISTERKEY        0xC01
#define IOCTL_DELETEREGISTERTREE       0xC02

// Hook
#define IOCTL_HOOK_PROTECTPROCESS      0xD00
#define IOCTL_HOOK_UNPROTECTPROCESS    0xD01
#define IOCTL_HOOK_PROTECTTHREAD       0xD02
#define IOCTL_HOOK_UNPROTECTTHREAD     0xD03
#define IOCTL_HOOK_DISABLEBSOD         0xD04
#define IOCTL_HOOK_QUERYSTATUS         0xD05
#define IOCTL_HOOK_DISABLENTDEVICEIOCONTROLFILE 0xD06

// ===== ARK 扩展（与驱动 h/Kernel/ArkNeo.h 保持一致）=====
// PspCidTable
#define IOCTL_CID_ENUMPROCESSES        0x600
#define IOCTL_CID_ENUMTHREADS          0x601
#define IOCTL_CID_TERMINATETHREAD      0x602
#define IOCTL_CID_TERMINATEPROCESS     0x603
#define IOCTL_CID_ENUMSYSTEMTHREADS    0x604
#define IOCTL_ENUMSYSCALLBACKS         0x605
#define IOCTL_ENUMUNLOADEDDRIVERS      0x606
// 键盘监视
#define IOCTL_KBD_START                0x610
#define IOCTL_KBD_STOP                 0x611
#define IOCTL_KBD_READLOG             0x612
#define IOCTL_KBD_CLEARLOG            0x613
#define IOCTL_KBD_GETSTATUS           0x614
#define IOCTL_KBD_ENUMHOTKEYS         0x615
#define IOCTL_KBD_MODHOTKEY           0x616
#define IOCTL_KBD_DELHOTKEY           0x617
// 进程行为监视
#define IOCTL_BEHAVIOR_READ           0x620
#define IOCTL_BEHAVIOR_CLEAR          0x621
#define IOCTL_BEHAVIOR_SETFILTER      0x622
#define IOCTL_FILEMON_START           0x623
#define IOCTL_FILEMON_STOP            0x624
#define IOCTL_HARDEN_ADD              0x625
#define IOCTL_HARDEN_REMOVE           0x626
#define IOCTL_FILEMON_STATUS          0x627
// 命名空间
#define IOCTL_NS_ENUMDIR               0x700
#define IOCTL_NS_DELETEOBJECT         0x701
#define IOCTL_NS_QUERYPATH            0x702

// ---- 注册表全内核操作 (ArkReg.cpp) ----
#define IOCTL_REG_ENUMSUBKEYS         0x710
#define IOCTL_REG_ENUMVALUES          0x711
#define IOCTL_REG_CREATEKEY           0x712
#define IOCTL_REG_DELETEKEY           0x713
#define IOCTL_REG_DELETEVALUE         0x714
#define IOCTL_REG_SETVALUE            0x715
#define IOCTL_REG_RENAMEKEY           0x716
#define IOCTL_REG_RENAMEVALUE         0x717
// 进程扩展
#define IOCTL_ENUMPROCESSHANDLES       0x813
#define IOCTL_CLOSEHANDLE              0x814
#define IOCTL_ARK_READMEMORY           0x815
#define IOCTL_ARK_WRITEMEMORY          0x816
#define IOCTL_ARK_QUERYVM              0x817
#define IOCTL_ARK_GETPRIORITY          0x818
#define IOCTL_ARK_SETPRIORITY          0x819
#define IOCTL_ARK_GETAFFINITY          0x81A
#define IOCTL_ARK_SETAFFINITY          0x81B
#define IOCTL_ARK_GETPRIVILEGES        0x81C
#define IOCTL_ARK_SETPRIVILEGE         0x81D
#define IOCTL_ARK_GETTOKENINFO         0x81E
// 文件占用扫描
#define IOCTL_SCANFILEOCCUPY           0x928
// 内核视图
#define IOCTL_ARK_ENUMDRIVEROBJS       0xB14
#define IOCTL_ARK_GETDRIVERDETAIL      0xB15
#define IOCTL_ARK_ENUMMODULES          0xB16
#define IOCTL_ARK_ENUMDEVICES          0xB17
#define IOCTL_ARK_GETDEVICEDETAIL      0xB18
#define IOCTL_ARK_QUERYSSDT            0xB19
#define IOCTL_ARK_QUERYIDT             0xB1A
#define IOCTL_ARK_ENUMOBJTYPES         0xB1B

// 设备命名空间
#define DEVICE_NAME     L"\\Device\\EFCHKernelDriver"
#define DOS_DEVICE_NAME L"\\DosDevices\\EFCHKernelDriver"

// 服务访问权限
#define SERVICE_ALL_ACCESS_W2K (STANDARD_RIGHTS_REQUIRED | SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | \
    SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS | SERVICE_START | SERVICE_STOP | \
    SERVICE_PAUSE_CONTINUE | SERVICE_INTERROGATE | SERVICE_USER_DEFINED_CONTROL)

KernelDriver::KernelDriver() {}

KernelDriver::~KernelDriver()
{
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
}

// ===== 内部辅助 =====

QByteArray KernelDriver::ansiToUnicodeBytes(const QString& s)
{
    // 转成 UTF-16LE 字节流，含结尾 \0\0
    QByteArray bytes;
    const ushort* utf16 = s.utf16();
    int len = s.length();
    bytes.resize((len + 1) * 2);
    memcpy(bytes.data(), utf16, len * 2);
    bytes[len * 2] = 0;
    bytes[len * 2 + 1] = 0;
    return bytes;
}

QString KernelDriver::unicodeBytesToAnsi(const QByteArray& bytes)
{
    // UTF-16LE 字节流转 QString（遇 \0\0 截断）
    const ushort* data = reinterpret_cast<const ushort*>(bytes.constData());
    int maxLen = bytes.size() / 2;
    int len = 0;
    while (len < maxLen && data[len] != 0) ++len;
    return QString::fromUtf16(data, len);
}

HANDLE KernelDriver::getDriveHandle(const QString& driverName)
{
    // 尝试通过 \\.\driverName 打开已加载的设备
    QString devicePath = QStringLiteral("\\\\.\\") + driverName;
    HANDLE h = CreateFileW(devicePath.toStdWString().c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    return h;
}

// 写入驱动服务注册表项（NtLoadDriver 需要）
// 使用 Win32 API 直接写注册表，避免 QSettings 的编码问题
static bool writeDriverRegistryEntry(const QString& serviceName, const QString& sysFilePath)
{
    // 注册表路径: SYSTEM\CurrentControlSet\Services\<serviceName>
    std::wstring subKey = L"SYSTEM\\CurrentControlSet\\Services\\";
    subKey += serviceName.toStdWString();

    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, nullptr,
                              0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) {
        qWarning() << "RegCreateKeyExW failed, error:" << rc;
        return false;
    }

    // ImagePath 需要带 \??\ 前缀
    std::wstring imagePath = L"\\??\\";
    imagePath += QDir::toNativeSeparators(sysFilePath).toStdWString();

    DWORD type = 1;          // SERVICE_KERNEL_DRIVER
    DWORD start = 3;          // SERVICE_DEMAND_START
    DWORD errorControl = 0;   // SERVICE_ERROR_IGNORE

    rc = RegSetValueExW(hKey, L"ImagePath", 0, REG_SZ,
                        (const BYTE*)imagePath.c_str(),
                        (DWORD)((imagePath.size() + 1) * sizeof(WCHAR)));
    if (rc != ERROR_SUCCESS) {
        qWarning() << "RegSetValueExW ImagePath failed, error:" << rc;
        RegCloseKey(hKey);
        return false;
    }

    RegSetValueExW(hKey, L"Type", 0, REG_DWORD, (const BYTE*)&type, sizeof(type));
    RegSetValueExW(hKey, L"Start", 0, REG_DWORD, (const BYTE*)&start, sizeof(start));
    RegSetValueExW(hKey, L"ErrorControl", 0, REG_DWORD, (const BYTE*)&errorControl, sizeof(errorControl));

    RegCloseKey(hKey);
    return true;
}

// 删除驱动服务注册表项
static void deleteDriverRegistryEntry(const QString& serviceName)
{
    std::wstring subKey = L"SYSTEM\\CurrentControlSet\\Services\\";
    subKey += serviceName.toStdWString();
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, subKey.c_str());
}

HANDLE KernelDriver::loadDrive(const QString& path, const QString& serviceName)
{
    // 使用 NtLoadDriver 加载驱动（比 SCM 更底层，更隐蔽）
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) {
        qWarning() << "Cannot get ntdll.dll handle";
        return INVALID_HANDLE_VALUE;
    }

    auto pNtLoadDriver = reinterpret_cast<NtLoadDriver_t>(
        GetProcAddress(hNtdll, "NtLoadDriver"));
    auto pNtUnloadDriver = reinterpret_cast<NtUnloadDriver_t>(
        GetProcAddress(hNtdll, "NtUnloadDriver"));
    if (!pNtLoadDriver) {
        qWarning() << "Cannot get NtLoadDriver address";
        return INVALID_HANDLE_VALUE;
    }

    // 构造注册表路径: \Registry\Machine\SYSTEM\CurrentControlSet\Services\<serviceName>
    std::wstring regPath = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    regPath += serviceName.toStdWString();

    UNICODE_STRING uniName;
    uniName.Buffer = const_cast<PWSTR>(regPath.c_str());
    uniName.Length = (USHORT)(regPath.size() * sizeof(WCHAR));
    uniName.MaximumLength = uniName.Length + sizeof(WCHAR);

    // 0. 先尝试卸载残留的驱动（上次崩溃/被杀遗留的服务项）
    //    卸载失败时保留注册表项，否则驻留的驱动会丢失服务项变成僵尸
    if (pNtUnloadDriver) {
        LONG preStatus = pNtUnloadDriver(&uniName);
        qInfo() << "Pre-cleanup NtUnloadDriver status: 0x" << Qt::hex << preStatus;
        if (preStatus == STATUS_SUCCESS_NT || preStatus == 0xC0000034L) {
            deleteDriverRegistryEntry(serviceName);
        }
    } else {
        deleteDriverRegistryEntry(serviceName);
    }

    // 1. 写入注册表服务项
    if (!writeDriverRegistryEntry(serviceName, path)) {
        qWarning() << "Failed to write driver registry entry";
        return INVALID_HANDLE_VALUE;
    }

    // 2. 调用 NtLoadDriver
    LONG status = pNtLoadDriver(&uniName);
    qInfo() << "NtLoadDriver status: 0x" << Qt::hex << status
            << " path:" << path << " service:" << serviceName;

    // STATUS_OBJECT_NAME_COLLISION (0xC0000035) 表示驱动已加载，视为成功
    if (status != STATUS_SUCCESS_NT && status != 0xC0000035L) {
        qWarning() << "NtLoadDriver failed, NTSTATUS: 0x" << Qt::hex << status
                   << " (STATUS_INVALID_IMAGE_HASH=0xC0000428, falling back to SCM)";

        // NtLoadDriver 失败，回退到 SCM 方式（易语言版本可能就是用 SCM）
        deleteDriverRegistryEntry(serviceName);
        HANDLE hScm = loadDriveViaSCM(path, serviceName);
        if (hScm != INVALID_HANDLE_VALUE) {
            qInfo() << "SCM fallback succeeded";
            return hScm;
        }
        qWarning() << "SCM fallback also failed";
        return INVALID_HANDLE_VALUE;
    }

    // 3. 打开设备句柄
    HANDLE h = getDriveHandle(serviceName);
    if (h == INVALID_HANDLE_VALUE) {
        qWarning() << "Driver loaded but cannot open device handle, GetLastError:" << GetLastError();
    }
    return h;
}

// SCM 方式加载驱动（回退方案）
HANDLE KernelDriver::loadDriveViaSCM(const QString& path, const QString& serviceName)
{
    SC_HANDLE hMgr = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hMgr) {
        qWarning() << "OpenSCManagerW failed, error:" << GetLastError();
        return INVALID_HANDLE_VALUE;
    }

    std::wstring wsPath = QDir::toNativeSeparators(path).toStdWString();
    std::wstring wsName = serviceName.toStdWString();

    // 先删除残留服务
    SC_HANDLE hOld = OpenServiceW(hMgr, wsName.c_str(), DELETE | SERVICE_QUERY_STATUS);
    if (hOld) {
        SERVICE_STATUS ss;
        ControlService(hOld, SERVICE_CONTROL_STOP, &ss);
        DeleteService(hOld);
        CloseServiceHandle(hOld);
        Sleep(500);
    }

    // 创建服务
    SC_HANDLE hSvc = CreateServiceW(hMgr, wsName.c_str(), wsName.c_str(),
                                    SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                                    SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                    wsPath.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!hSvc) {
        // 服务可能已存在，尝试打开
        hSvc = OpenServiceW(hMgr, wsName.c_str(), SERVICE_ALL_ACCESS);
    }

    if (!hSvc) {
        qWarning() << "CreateService/OpenService failed, error:" << GetLastError();
        CloseServiceHandle(hMgr);
        return INVALID_HANDLE_VALUE;
    }

    // 启动服务
    BOOL ok = StartServiceW(hSvc, 0, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        // ERROR_SERVICE_ALREADY_RUNNING = 1056 也算成功
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            qWarning() << "StartServiceW failed, error:" << err;
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hMgr);
            return INVALID_HANDLE_VALUE;
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hMgr);

    // 等待设备就绪
    Sleep(500);
    return getDriveHandle(serviceName);
}

bool KernelDriver::uninstallDrive(const QString& serviceName, bool stopFirst)
{
    // 使用 NtUnloadDriver 卸载驱动
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    auto pNtUnloadDriver = reinterpret_cast<NtUnloadDriver_t>(
        GetProcAddress(hNtdll, "NtUnloadDriver"));
    if (!pNtUnloadDriver) return false;

    // 构造注册表路径
    std::wstring regPath = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    regPath += serviceName.toStdWString();

    UNICODE_STRING uniName;
    uniName.Buffer = const_cast<PWSTR>(regPath.c_str());
    uniName.Length = (USHORT)(regPath.size() * sizeof(WCHAR));
    uniName.MaximumLength = uniName.Length + sizeof(WCHAR);

    LONG status = pNtUnloadDriver(&uniName);

    // STATUS_SUCCESS 或 STATUS_OBJECT_NAME_NOT_FOUND 都算成功
    if (status == STATUS_SUCCESS_NT || status == 0xC0000034L) {
        // 只有确认驱动已卸载（或本来就不存在）才删除注册表项
        deleteDriverRegistryEntry(serviceName);
        return true;
    }

    // 卸载失败：必须保留注册表项！
    // 此时驱动仍驻留内存（引用计数未归零），若删掉服务项，之后
    // NtUnloadDriver 将再也找不到服务，驱动变成无法卸载的僵尸，
    // 新驱动也无法加载（设备名冲突），只能重启系统。
    qWarning() << "NtUnloadDriver failed, NTSTATUS: 0x" << Qt::hex << status
               << "- registry entry kept so the driver can still be unloaded later";
    return false;
}

bool KernelDriver::installDriver(const QString& sysFilePath, QString* errMsg)
{
    if (m_hDevice != INVALID_HANDLE_VALUE && m_hDevice != nullptr) {
        if (errMsg) *errMsg = QStringLiteral("驱动已加载");
        return true;
    }

    // 1. 先尝试打开已加载的设备（可能上次加载后未卸载，sys 文件仍被占用无法重写）
    //    这一步在文件存在检查之前，避免在驱动已加载但 sys 不可写时误报失败
    m_hDevice = getDriveHandle(m_driverName);
    if (m_hDevice != INVALID_HANDLE_VALUE && m_hDevice != nullptr) {
        // 版本校验：上次崩溃残留的旧版驱动可能带有已修复的严重缺陷
        // （如特征码扫描死循环导致的 CPU/内存狂飙），绝不能直接复用。
        // 版本不符 -> 卸载旧驱动并返回失败，让上层走全新安装流程覆盖 sys 后重载。
        quint32 ver = getDriverVersion();
        if (ver == kExpectedDriverVersion) {
            qInfo() << "Driver already loaded (version" << ver << "), reuse device handle";
            m_driverPath = sysFilePath;
            if (errMsg) *errMsg = QStringLiteral("驱动已加载（复用现有实例）");
            return true;
        }
        qWarning() << "Stale driver detected: loaded version" << ver
                   << "!= expected" << kExpectedDriverVersion
                   << "- unloading for reinstall";
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;

        if (uninstallDrive(m_driverName, true)) {
            // 旧驱动已卸载，sys 文件不再被占用，上层可覆盖新文件后重新加载
            if (errMsg) *errMsg = QStringLiteral("检测到旧版残留驱动已卸载，正在重新安装");
            return false;
        }
        // 卸载失败：设备对象仍存在，新驱动无法加载（设备名冲突）。
        // 降级复用旧驱动保证程序可用；注册表项已保留，下次仍可重试卸载。
        HANDLE hFallback = getDriveHandle(m_driverName);
        if (hFallback != INVALID_HANDLE_VALUE && hFallback != nullptr) {
            m_hDevice = hFallback;
            m_driverPath = sysFilePath;
            qWarning() << "Stale driver unload failed, falling back to existing instance";
            if (errMsg) *errMsg = QStringLiteral("旧版驱动卸载失败，已复用现有实例（建议重启系统后重新运行）");
            return true;
        }
        if (errMsg) *errMsg = QStringLiteral("旧版驱动卸载失败且设备不可用，请重启系统后重试");
        return false;
    }
    m_hDevice = INVALID_HANDLE_VALUE;

    // 2. 设备未加载，检查 sys 文件
    QFileInfo fi(sysFilePath);
    if (!fi.exists()) {
        if (errMsg) *errMsg = QStringLiteral("驱动文件不存在: ") + sysFilePath;
        return false;
    }
    qInfo() << "Installing driver:" << sysFilePath << "size:" << fi.size() << "bytes";

    m_driverPath = sysFilePath;

    // 3. 通过 NtLoadDriver 加载
    m_hDevice = loadDrive(sysFilePath, m_driverName);

    if (m_hDevice == INVALID_HANDLE_VALUE || m_hDevice == nullptr) {
        m_hDevice = INVALID_HANDLE_VALUE;
        if (errMsg) *errMsg = QStringLiteral("驱动加载失败，请检查杀毒软件是否拦截，"
                                              "或以管理员身份运行。GetLastError=")
                            + QString::number(GetLastError());
        return false;
    }

    qInfo() << "Driver installed successfully, hDevice:" << m_hDevice;
    return true;
}

bool KernelDriver::uninstallDriver()
{
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    bool ok = uninstallDrive(m_driverName, true);
    // 仅在卸载成功后删除 sys 文件；失败时驱动仍占用该文件
    // （删除也不会生效），保留以便下次重试卸载后覆盖新版本
    if (ok && !m_driverPath.isEmpty()) {
        QFile::remove(m_driverPath);
    }
    return ok;
}

// 查询已加载驱动的版本号（驱动 IOCTL_DRV_GET_INFO, 功能码 0xD06）
// 输入 LONG=1，输出 ULONG 版本。失败（含旧驱动不支持该命令）返回 0
quint32 KernelDriver::getDriverVersion()
{
    if (m_hDevice == INVALID_HANDLE_VALUE || m_hDevice == nullptr) {
        return 0;
    }
    LONG request = 1;
    quint32 version = 0;
    DWORD br = 0;
    if (!deviceIoControl(0xD06, &request, sizeof(request), &version, sizeof(version), &br)) {
        return 0;
    }
    if (br < sizeof(version)) {
        return 0;
    }
    return version;
}

bool KernelDriver::deviceIoControl(quint32 function, LPVOID inBuffer, DWORD inSize,
                                   LPVOID outBuffer, DWORD outSize, LPDWORD bytesReturned)
{
    if (m_hDevice == INVALID_HANDLE_VALUE) {
        m_lastError = ERROR_INVALID_HANDLE;
        return false;
    }
    DWORD br = 0;
    if (!bytesReturned) bytesReturned = &br;
    BOOL ok = DeviceIoControl(m_hDevice, ioctlCode(function),
                              inBuffer, inSize,
                              outBuffer, outSize,
                              bytesReturned, nullptr);
    m_lastError = ok ? ERROR_SUCCESS : GetLastError();
    return ok != FALSE;
}

// ===== Hypervisor (Intel VT-x, 0xB06-0xB08) =====

bool KernelDriver::hvQuery(HvQueryInfo& out)
{
    ZeroMemory(&out, sizeof(out));
    DWORD br = 0;
    if (!deviceIoControl(0xB06, nullptr, 0, &out, sizeof(out), &br)) return false;
    return br >= sizeof(out);
}

bool KernelDriver::hvStart(quint32 options)
{
    return deviceIoControl(0xB07, &options, sizeof(options), nullptr, 0);
}

bool KernelDriver::hvStop()
{
    return deviceIoControl(0xB08, nullptr, 0, nullptr, 0);
}

bool KernelDriver::hvReadMem(quint32 pid, quint64 address, QByteArray& outData)
{
    // 大小在调用前由 UI 限定 <= 0x8000；这里通过 outData 长度传递期望大小
    const int size = outData.size();
    if (size <= 0 || size > 0x8000) return false;
    QByteArray buf;
    buf.resize(sizeof(HvMemAccess) + size);
    auto* acc = reinterpret_cast<HvMemAccess*>(buf.data());
    acc->pid = pid;
    acc->size = (quint32)size;
    acc->address = address;
    DWORD br = 0;
    if (!deviceIoControl(0x628, buf.data(), (DWORD)buf.size(), buf.data(), (DWORD)buf.size(), &br)) return false;
    if (br < sizeof(HvMemAccess) + size) return false;
    outData = QByteArray(buf.constData() + sizeof(HvMemAccess), size);
    return true;
}

bool KernelDriver::hvWriteMem(quint32 pid, quint64 address, const QByteArray& data)
{
    const int size = data.size();
    if (size <= 0 || size > 0x8000) return false;
    QByteArray buf;
    buf.resize(sizeof(HvMemAccess) + size);
    auto* acc = reinterpret_cast<HvMemAccess*>(buf.data());
    acc->pid = pid;
    acc->size = (quint32)size;
    acc->address = address;
    memcpy(buf.data() + sizeof(HvMemAccess), data.constData(), size);
    return deviceIoControl(0x629, buf.data(), (DWORD)buf.size(), buf.data(), (DWORD)buf.size());
}

// ===== Hypervisor R-1 进程处置 (0x62A) =====
// 线格式与驱动 h/Kernel/ArkHv.h 的 HV_PROCESS_CONTROL / HV_PROCESS_RESULT 一致。

bool KernelDriver::hvmProcessControl(quint32 operation, quint32 pid, quint64 linearAddress,
                                     HvProcessResult& out)
{
#pragma pack(push, 8)
    struct WireRow {
        quint32 pid;
        quint32 disposition;
        quint64 directoryBase;
        quint64 guestPhysicalAddress;
        quint64 guestLinearAddress;
        quint64 interceptCount;
        quint32 hierarchyIndex;
        quint32 reserved;
    };
    struct WireIn {
        quint32 pid;
        quint32 operation;
        quint64 linearAddress;
    };
    struct WireOut {
        quint32 status;
        quint32 rowCount;
        quint32 returnedRows;
        quint32 residentActive;
        qint32 lastStatus;
        quint64 stateFlags;
        WireRow rows[8];
    };
#pragma pack(pop)
    static_assert(sizeof(WireRow) == 48, "wire row layout mismatch");
    static_assert(sizeof(WireOut) == 416, "wire result layout mismatch");

    out = HvProcessResult{};
    WireIn in{ pid, operation, linearAddress };
    WireOut io{};
    DWORD br = 0;
    if (!deviceIoControl(0x62A, &in, sizeof(in), &io, sizeof(io), &br)) return false;
    if (br < sizeof(io)) return false;

    out.status = io.status;
    out.rowCount = io.rowCount;
    out.returnedRows = io.returnedRows;
    out.residentActive = io.residentActive;
    out.lastStatus = io.lastStatus;
    out.stateFlags = io.stateFlags;
    const quint32 n = qMin<quint32>(io.returnedRows, 8);
    for (quint32 i = 0; i < n; ++i) {
        HvProcessRow row;
        row.pid = io.rows[i].pid;
        row.disposition = io.rows[i].disposition;
        row.directoryBase = io.rows[i].directoryBase;
        row.guestPhysicalAddress = io.rows[i].guestPhysicalAddress;
        row.guestLinearAddress = io.rows[i].guestLinearAddress;
        row.interceptCount = io.rows[i].interceptCount;
        row.hierarchyIndex = io.rows[i].hierarchyIndex;
        out.rows.push_back(row);
    }
    return true;
}

bool KernelDriver::hvmFreezeProcess(quint32 pid, quint64 linearAddress, HvProcessResult* out)
{
    HvProcessResult r;
    if (!hvmProcessControl(HvProcOpFreeze, pid, linearAddress, r)) return false;
    if (out) *out = r;
    return r.status == 0;
}

bool KernelDriver::hvmTerminateProcess(quint32 pid, quint64 linearAddress, HvProcessResult* out)
{
    HvProcessResult r;
    if (!hvmProcessControl(HvProcOpTerminate, pid, linearAddress, r)) return false;
    if (out) *out = r;
    return r.status == 0;
}

bool KernelDriver::hvmResumeProcess(quint32 pid, HvProcessResult* out)
{
    HvProcessResult r;
    if (!hvmProcessControl(HvProcOpRelease, pid, 0, r)) return false;
    if (out) *out = r;
    return r.status == 0;
}

bool KernelDriver::hvParityNow()
{
    return deviceIoControl(0x62B, nullptr, 0, nullptr, 0);
}

// ===== 进程操作 =====

bool KernelDriver::zwKillProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_KILLPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::apcTerminateProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_APCTERMINATEPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::suspendProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_SUSPENDPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::resumeProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_RESUMEPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::hideProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_HIDEPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::unHideProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_UNHIDEPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::protectProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_PROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::unProtectProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_UNPROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::forceProtectProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_FORCEPROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::unForceProtectProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_UNFORCEPROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::memKillProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_MEMKILLPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::setPPL(quint32 pid, quint32 protectType)
{
    struct { quint32 pid; quint32 protectType; } data = { pid, protectType };
    return deviceIoControl(IOCTL_SETPPL, &data, sizeof(data), nullptr, 0);
}

bool KernelDriver::setPID(quint64 pid, quint64 newPid)
{
    struct { quint64 pid; quint64 newPid; } data = { pid, newPid };
    return deviceIoControl(IOCTL_SETPID, &data, sizeof(data), nullptr, 0);
}

bool KernelDriver::setProcessCritical(quint64 pid, qint32 isCritical)
{
    struct { quint64 pid; qint32 isCritical; } data = { pid, isCritical };
    return deviceIoControl(IOCTL_SETPROCESSCRITICAL, &data, sizeof(data), nullptr, 0);
}

bool KernelDriver::pspTerminateProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_PSPTERMINATEPROCESS_FAKE, &pid, sizeof(pid), nullptr, 0);
}

QVector<quint32> KernelDriver::enumProcesses()
{
    QVector<quint32> result;
    QByteArray buffer(65535 * 4, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_EPROCESSENUMPROCESS, nullptr, 0, buffer.data(), buffer.size(), &br)) {
        return result;
    }
    const quint32* pids = reinterpret_cast<const quint32*>(buffer.constData());
    int count = br / 4;
    for (int i = 0; i < count; ++i) {
        if (pids[i] == 0) break;
        result.append(pids[i]);
    }
    return result;
}

qint32 KernelDriver::getPPL(quint32 pid)
{
    quint32 out = 0;
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_GETPPL, &pid, sizeof(pid), &out, sizeof(out), &br)) {
        return -1;
    }
    return (qint32)out;
}

QString KernelDriver::getProcessDosImageName(quint32 pid)
{
    QByteArray buffer(1024, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_GETPROCESSIMAGENAME, &pid, sizeof(pid), buffer.data(), buffer.size(), &br)) {
        return QString();
    }
    return unicodeBytesToAnsi(buffer);
}

QVector<ThreadInfo> KernelDriver::enumThreadsByProcess(quint32 pid)
{
    // 驱动协议 (IOCTL_ENUMTHREADBYPROCESS, function=0x811):
    //   输入:  HANDLE pid  (8 字节, 64位)
    //   输出:  ULONG count + count × THREAD_OUTPUT_ENTRY { HANDLE Tid; PETHREAD EThread; }
    //          每条目 16 字节 (64位), 驱动内部线程上限 8192
    //   缓冲区不足: 返回 STATUS_BUFFER_TOO_SMALL, 输出首 ULONG = 所需字节数
    QVector<ThreadInfo> result;
    if (m_hDevice == INVALID_HANDLE_VALUE) return result;

    const DWORD kMaxThreads = 8192;
    const DWORD kEntrySize = 16;  // sizeof(HANDLE) + sizeof(PETHREAD) on x64
    const DWORD kBufSize = sizeof(ULONG) + kMaxThreads * kEntrySize;

    QByteArray buffer(kBufSize, '\0');
    DWORD br = 0;
    HANDLE pidIn = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid));
    if (!deviceIoControl(IOCTL_ENUMTHREADBYPROCESS, &pidIn, sizeof(pidIn),
                         buffer.data(), buffer.size(), &br)) {
        return result;
    }
    if (br < sizeof(ULONG)) return result;

    const ULONG count = *reinterpret_cast<const ULONG*>(buffer.constData());
    if (count == 0 || count > kMaxThreads) return result;

    const DWORD bytesNeeded = sizeof(ULONG) + count * kEntrySize;
    if (br < bytesNeeded) return result;

    const UCHAR* pEntries = reinterpret_cast<const UCHAR*>(buffer.constData()) + sizeof(ULONG);
    result.reserve(count);
    for (ULONG i = 0; i < count; ++i) {
        const UCHAR* pEntry = pEntries + i * kEntrySize;
        // THREAD_OUTPUT_ENTRY { HANDLE Tid (8B); PETHREAD EThread (8B) }
        quint64 tidHandle = *reinterpret_cast<const quint64*>(pEntry);
        quint64 ethread   = *reinterpret_cast<const quint64*>(pEntry + 8);
        ThreadInfo info;
        info.tid     = static_cast<quint32>(tidHandle);
        info.ethread = ethread;
        result.append(info);
    }
    return result;
}

QString KernelDriver::getDriverPathByEThread(quint64 ethread)
{
    // 驱动协议 (IOCTL_GETDRIVERPATCHBYETHREAD, function=0x812):
    //   输入:  PETHREAD (8 字节, 64位内核地址)
    //   输出:  WCHAR[] 驱动完整路径 (UTF-16, null 结尾), info = 字节长度
    QString result;
    if (m_hDevice == INVALID_HANDLE_VALUE) return result;
    if (ethread == 0) return result;

    QByteArray outBuf(2048, '\0');  // 驱动内部 buffer[1024] WCHAR = 2048 字节
    DWORD br = 0;
    quint64 ethreadIn = ethread;
    if (!deviceIoControl(IOCTL_GETDRIVERPATCHBYETHREAD, &ethreadIn, sizeof(ethreadIn),
                         outBuf.data(), outBuf.size(), &br)) {
        return result;
    }
    if (br < sizeof(WCHAR)) return result;

    int chars = br / sizeof(WCHAR);
    if (chars > 0) {
        result = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(outBuf.constData()), chars);
    }
    return result;
}

// ===== 线程操作 =====

bool KernelDriver::suspendThread(quint32 tid)
{
    return deviceIoControl(IOCTL_SUSPENDTHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::resumeThread(quint32 tid)
{
    return deviceIoControl(IOCTL_RESUMETHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::terminateThread(quint32 tid)
{
    return deviceIoControl(IOCTL_TERMINATETHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::pspTerminateThreadByPointerEasy(quint32 tid)
{
    return deviceIoControl(IOCTL_PSPTERMINATETHREADBYPOINTER_EASY, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::pspTerminateThreadByPointer(quint64 tid, qint32 exitCode, bool directTerminate)
{
    struct { quint64 tid; qint32 exitCode; quint8 directTerminate; } data;
    data.tid = tid;
    data.exitCode = exitCode;
    data.directTerminate = directTerminate ? 1 : 0;
    return deviceIoControl(IOCTL_PSPTERMINATETHREADBYPOINTER, &data, sizeof(data), nullptr, 0);
}

bool KernelDriver::apcTerminateThread(quint32 tid)
{
    return deviceIoControl(IOCTL_APCTERMINATETHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::apcTerminateThreadByEThread(quint64 ethread)
{
    // 驱动协议 (IOCTL_APCTERMINATETHREADBYETHREAD, function=0xA08):
    //   输入: PETHREAD (8 字节, 64位内核地址)
    //   输出: 无
    if (ethread == 0) return false;
    quint64 ethreadIn = ethread;
    return deviceIoControl(IOCTL_APCTERMINATETHREADBYETHREAD, &ethreadIn, sizeof(ethreadIn), nullptr, 0);
}

// ===== 文件操作 =====

bool KernelDriver::zwDeleteFile(const QString& ntFilePath)
{
    QByteArray uni = ansiToUnicodeBytes(ntFilePath);
    return deviceIoControl(IOCTL_ZWDELETEFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::occupyFile(const QString& ntFilePath)
{
    QByteArray uni = ansiToUnicodeBytes(ntFilePath);
    return deviceIoControl(IOCTL_OCCUPYFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::forceDeleteFile(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    return deviceIoControl(IOCTL_FORCEDELETEFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::forceOccupyFile(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    return deviceIoControl(IOCTL_FORCEOCCUPYFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::forceWriteFile(const QString& filePath, const QByteArray& data)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    if (uni.size() > 512) return false;
    // 补齐到 512 字节
    uni.resize(512);
    QByteArray inBuffer = uni + (data.isEmpty() ? QByteArray(1, '\0') : data);
    return deviceIoControl(IOCTL_FORCEWRITEFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::irpCreateFile(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    return deviceIoControl(IOCTL_IRPCREATEFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::irpCreateDirectory(const QString& dirPath)
{
    QByteArray uni = ansiToUnicodeBytes(dirPath);
    return deviceIoControl(IOCTL_IRPCREATEDIRECTORY, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::irpCopyFile(const QString& oldPath, const QString& newPath)
{
    QByteArray oldUni = ansiToUnicodeBytes(oldPath);
    QByteArray newUni = ansiToUnicodeBytes(newPath);
    if (oldUni.size() > 4096 || newUni.size() > 4096) return false;
    oldUni.resize(4096);
    newUni.resize(4096);
    QByteArray inBuffer = oldUni + newUni;
    return deviceIoControl(IOCTL_IRPCOPYFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::irpMoveFile(const QString& oldPath, const QString& newPath)
{
    QByteArray oldUni = ansiToUnicodeBytes(oldPath);
    QByteArray newUni = ansiToUnicodeBytes(newPath);
    if (oldUni.size() > 4096 || newUni.size() > 4096) return false;
    oldUni.resize(4096);
    newUni.resize(4096);
    QByteArray inBuffer = oldUni + newUni;
    return deviceIoControl(IOCTL_IRPMOVEFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::irpRenameFile(const QString& oldName, const QString& newName)
{
    QByteArray oldUni = ansiToUnicodeBytes(oldName);
    QByteArray newUni = ansiToUnicodeBytes(newName);
    if (oldUni.size() > 4096 || newUni.size() > 4096) return false;
    oldUni.resize(4096);
    newUni.resize(4096);
    QByteArray inBuffer = oldUni + newUni;
    return deviceIoControl(IOCTL_IRPRENAMEFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::irpSetInformationFile(const QString& filePath, quint64 attribute)
{
    QByteArray attrBytes(reinterpret_cast<const char*>(&attribute), sizeof(attribute));
    QByteArray uni = ansiToUnicodeBytes(filePath);
    QByteArray inBuffer = attrBytes + uni;
    if (inBuffer.size() < 10) return false;
    return deviceIoControl(IOCTL_IRPSETINFORMATIONFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::posixForceDeleteFile(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    return deviceIoControl(IOCTL_POSIXFORCEDELETEFILE, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::zwWriteFile(const QString& ntFilePath, const QByteArray& data)
{
    QByteArray uni = ansiToUnicodeBytes(ntFilePath);
    if (uni.size() > 512) return false;
    uni.resize(512);
    QByteArray inBuffer = uni + (data.isEmpty() ? QByteArray(1, '\0') : data);
    return deviceIoControl(IOCTL_ZWWRITEFILE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::zwReadFile(const QString& ntFilePath, QByteArray& outData)
{
    // 需要先获取文件大小（去掉 \??\ 前缀）
    QString filePath = ntFilePath;
    if (filePath.startsWith(QStringLiteral("\\??\\"))) {
        filePath = filePath.mid(4);
    }
    QFileInfo fi(filePath);
    qint64 fileSize = fi.size();
    if (fileSize <= 0) return false;

    QByteArray uni = ansiToUnicodeBytes(ntFilePath);
    outData.resize(fileSize);
    DWORD br = 0;
    return deviceIoControl(IOCTL_ZWREADFILE, uni.data(), uni.size(), outData.data(), outData.size(), &br);
}

bool KernelDriver::irpReadFile(const QString& filePath, QByteArray& outData)
{
    QFileInfo fi(filePath);
    qint64 fileSize = fi.size();
    if (fileSize <= 0) return false;

    QByteArray uni = ansiToUnicodeBytes(filePath);
    outData.resize(fileSize);
    DWORD br = 0;
    return deviceIoControl(IOCTL_IRPREADFILE, uni.data(), uni.size(), outData.data(), outData.size(), &br);
}

qint64 KernelDriver::irpGetFileSize(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    quint64 size = 0;
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_IRPGETFILESIZE, uni.data(), uni.size(), &size, sizeof(size), &br)) {
        return -1;
    }
    return (qint64)size;
}

bool KernelDriver::irpReadDisk(quint64 startSector, quint64 sectorNumber, QByteArray& outData)
{
    if (sectorNumber == 0) {
        outData = QByteArray(512, '\0');
        return true;
    }
    struct { quint64 start; quint64 number; } inData = { startSector, sectorNumber };
    outData.resize(sectorNumber * 512);
    DWORD br = 0;
    return deviceIoControl(IOCTL_IRPREADDISK, &inData, sizeof(inData), outData.data(), outData.size(), &br);
}

bool KernelDriver::irpQueryFileAndFolders(const QString& filePath, QVector<DataInfoAnsi>& outList)
{
    qint32 quantity = irpGetFileAndFolderQuantity(filePath);
    if (quantity == 0) return false;

    QByteArray uni = ansiToUnicodeBytes(filePath);
    QByteArray outBuffer(quantity * 2052, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_IRPQUERYFILEANDFOLDERS, uni.data(), uni.size(),
                         outBuffer.data(), outBuffer.size(), &br)) {
        return false;
    }

    int count = br / 2052;
    outList.clear();
    outList.reserve(count);
    for (int i = 0; i < count; ++i) {
        QByteArray entry = outBuffer.mid(i * 2052, 2052);
        QByteArray uniName = entry.left(2048);
        DataInfoAnsi info;
        info.fileName = unicodeBytesToAnsi(uniName).trimmed();
        info.isDirectory = *reinterpret_cast<const bool*>(entry.constData() + 2048);
        outList.append(info);
    }
    return true;
}

qint32 KernelDriver::irpGetFileAndFolderQuantity(const QString& filePath)
{
    QByteArray uni = ansiToUnicodeBytes(filePath);
    quint32 quantity = 0;
    DWORD br = 0;
    deviceIoControl(IOCTL_IRPGETFILEANDFOLDERQUANTITY, uni.data(), uni.size(), &quantity, sizeof(quantity), &br);
    return (qint32)quantity;
}

bool KernelDriver::flushMFT(const QString& driveLetter)
{
    QByteArray uni = ansiToUnicodeBytes(driveLetter);
    return deviceIoControl(IOCTL_FLUSHMFT, uni.data(), uni.size(), nullptr, 0);
}

// ===== DirectNTFS 操作 =====
// 驱动 NTFS 路径格式: \Windows\System32 (反斜杠开头, 相对卷根, 不含盘符)
// 用户路径 C:\Windows\System32 -> NTFS 路径 \Windows\System32
static QByteArray toNtfsPathBuffer(const QString& userPath)
{
    // 构造 4096 字节的 UTF-16 路径缓冲区 (NTFS_PATH_BUFFER_SIZE)
    QString p = userPath;
    // 去掉盘符前缀 "X:"
    if (p.length() >= 2 && p[1] == QChar(':')) {
        p = p.mid(2);
    }
    // 确保以 \ 开头
    if (!p.startsWith(QChar('\\'))) {
        p.prepend(QChar('\\'));
    }
    // 转为 UTF-16 字节
    std::wstring ws = p.toStdWString();
    QByteArray buf(4096, '\0');
    int copyBytes = qMin<int>((int)(ws.size() * sizeof(wchar_t)), 4094);
    memcpy(buf.data(), ws.c_str(), copyBytes);
    // 确保结尾有双 null (UTF-16 null terminator)
    return buf;
}

bool KernelDriver::ntfsInitVolume(wchar_t driveLetter)
{
    // 输入: WCHAR 盘符 (2 字节)
    WCHAR dl = driveLetter;
    return deviceIoControl(IOCTL_NTFS_INIT, &dl, sizeof(dl), nullptr, 0);
}

bool KernelDriver::ntfsReadFile(const QString& filePath, QByteArray& outData)
{
    // 输入: 4096 字节 UTF-16 路径
    // 输出: 文件内容 (METHOD_BUFFERED, 输入输出共用 SystemBuffer, 驱动会先拷贝路径到本地)
    // 最多读取 16MB, 超出部分截断
    const DWORD kMaxRead = 16 * 1024 * 1024;
    QByteArray pathBuf = toNtfsPathBuffer(filePath);
    outData.resize(kMaxRead);
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_NTFS_READFILE, pathBuf.data(), pathBuf.size(),
                         outData.data(), outData.size(), &br)) {
        outData.clear();
        return false;
    }
    outData.resize(br);
    return true;
}

bool KernelDriver::ntfsEnumDirectory(const QString& dirPath, QVector<NtfsDataInfo>& outList)
{
    // 输入: 路径 (UTF-16)
    // 输出: NTFS_DATA_INFO 数组, 每项 2052 字节
    outList.clear();
    QByteArray pathBuf = toNtfsPathBuffer(dirPath);
    // 最多 1024 项 (1024 * 2052 ≈ 2MB)
    const int kMaxEntries = 1024;
    const DWORD kEntrySize = sizeof(NtfsDataInfo);  // 2052
    QByteArray outBuf(kMaxEntries * kEntrySize, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_NTFS_ENUMDIR, pathBuf.data(), pathBuf.size(),
                         outBuf.data(), outBuf.size(), &br)) {
        return false;
    }
    int count = br / kEntrySize;
    const NtfsDataInfo* pEntries = reinterpret_cast<const NtfsDataInfo*>(outBuf.constData());
    for (int i = 0; i < count; ++i) {
        outList.append(pEntries[i]);
    }
    return true;
}

bool KernelDriver::ntfsCountDirectory(const QString& dirPath, NtfsFileFolderQuantity& outQty)
{
    // 输入: 路径 (UTF-16)
    // 输出: NTFS_FILE_FOLDER_QUANTITY (20 字节)
    QByteArray pathBuf = toNtfsPathBuffer(dirPath);
    outQty = NtfsFileFolderQuantity();
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_NTFS_GETQTY, pathBuf.data(), pathBuf.size(),
                         &outQty, sizeof(outQty), &br)) {
        return false;
    }
    return br >= sizeof(outQty);
}

bool KernelDriver::ntfsDeleteFile(const QString& filePath)
{
    // 驱动协议 (IOCTL_NTFS_DELETEFILE, function=0x927):
    //   输入: UTF-16 路径 (最多 4096 字节, null 结尾)
    //   输出: 无 (info=0)
    //   要求: 已通过 IOCTL_NTFS_INIT 初始化对应卷
    //   驱动调用 NtfsSecureDeleteFilePath, 文件和目录均可删除
    QByteArray pathBuf = toNtfsPathBuffer(filePath);
    return deviceIoControl(IOCTL_NTFS_DELETEFILE, pathBuf.data(), pathBuf.size(), nullptr, 0);
}

// ===== 磁盘操作 =====

bool KernelDriver::writeDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data)
{
    if ((qint64)sectorNumber * 512 != data.size()) return false;
    QByteArray inBuffer;
    inBuffer.reserve(16 + 1 + data.size());
    inBuffer.append(reinterpret_cast<const char*>(&startSector), 8);
    inBuffer.append(reinterpret_cast<const char*>(&sectorNumber), 8);
    inBuffer.append('\0');
    inBuffer.append(data);
    return deviceIoControl(IOCTL_WRITEDISK, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::forceWriteDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data)
{
    if ((qint64)sectorNumber * 512 != data.size()) return false;
    QByteArray inBuffer;
    inBuffer.reserve(16 + 1 + data.size());
    inBuffer.append(reinterpret_cast<const char*>(&startSector), 8);
    inBuffer.append(reinterpret_cast<const char*>(&sectorNumber), 8);
    inBuffer.append('\0');
    inBuffer.append(data);
    return deviceIoControl(IOCTL_FORCEWRITEDISK, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::storDirectReadDisk(quint64 startSector, quint64 sectorNumber,
    qint16 raidPort, QByteArray& outData)
{
    if (sectorNumber == 0) {
        outData = QByteArray(512, '\0');
        return true;
    }
    // 与 storDirectWriteDisk 保持一致：手动拼，无 struct padding
    QByteArray inBuffer;
    inBuffer.append(reinterpret_cast<const char*>(&raidPort), 2);
    inBuffer.append(reinterpret_cast<const char*>(&startSector), 8);
    inBuffer.append(reinterpret_cast<const char*>(&sectorNumber), 8);

    outData.resize(sectorNumber * 512);
    DWORD br = 0;
    return deviceIoControl(IOCTL_STORDIRECT_READDISK,
        inBuffer.data(), inBuffer.size(),
        outData.data(), outData.size(), &br);
}

bool KernelDriver::storDirectWriteDisk(quint64 startSector, quint64 sectorNumber, qint16 raidPort, const QByteArray& data)
{
    if ((qint64)sectorNumber * 512 != data.size()) return false;
    QByteArray inBuffer;
    inBuffer.reserve(2 + 16 + data.size());
    inBuffer.append(reinterpret_cast<const char*>(&raidPort), 2);
    inBuffer.append(reinterpret_cast<const char*>(&startSector), 8);
    inBuffer.append(reinterpret_cast<const char*>(&sectorNumber), 8);
    inBuffer.append(data);
    return deviceIoControl(IOCTL_STORDIRECT_WRITEDISK, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

qint16 KernelDriver::getVolumeRaidPort(const QString& volumeLetter)
{
    QByteArray uni = ansiToUnicodeBytes(volumeLetter);
    qint16 port = 0;
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_GETVOLUMERAIDPORT, uni.data(), uni.size(), &port, sizeof(port), &br)) {
        return -1;
    }
    return port;
}

// ===== 系统操作 =====

bool KernelDriver::bsod(quint32 code)
{
    return deviceIoControl(IOCTL_KEBUGCHECK, &code, sizeof(code), nullptr, 0);
}

bool KernelDriver::bsodAuto()
{
    return deviceIoControl(IOCTL_SYSTEMBSOD, nullptr, 0, nullptr, 0);
}

bool KernelDriver::forceUnloadDriver(const QString& driverName)
{
    QByteArray uni = ansiToUnicodeBytes(driverName);
    return deviceIoControl(IOCTL_FORCEUNLOADDRIVER, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::forceUnloadDevice(const QString& deviceName, qint16 operation)
{
    QByteArray opBytes(reinterpret_cast<const char*>(&operation), sizeof(operation));
    QByteArray uni = ansiToUnicodeBytes(deviceName);
    QByteArray inBuffer = opBytes + uni;
    return deviceIoControl(IOCTL_FORCEUNLOADDEVICE, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::antiDriverIoControl(const QString& driverName)
{
    QByteArray uni = ansiToUnicodeBytes(driverName);
    return deviceIoControl(IOCTL_ANTIDRIVERIOCONTROL, uni.data(), uni.size(), nullptr, 0);
}

bool KernelDriver::haltCPU()
{
    return deviceIoControl(IOCTL_HALTCPU, nullptr, 0, nullptr, 0);
}

bool KernelDriver::haltAllCPUs()
{
    return deviceIoControl(IOCTL_HALTALLCPUS, nullptr, 0, nullptr, 0);
}

bool KernelDriver::halPowerControl(HalPowerControlCode controlCode)
{
    qint32 code = (qint32)controlCode;
    return deviceIoControl(IOCTL_HALPOWERCONTROL, &code, sizeof(code), nullptr, 0);
}

bool KernelDriver::intelVTSupport()
{
    return deviceIoControl(IOCTL_INTELVT_SUPPORT, nullptr, 0, nullptr, 0);
}

bool KernelDriver::intelVTInit()
{
    return deviceIoControl(IOCTL_INTELVT_INIT, nullptr, 0, nullptr, 0);
}

bool KernelDriver::intelVTStop()
{
    return deviceIoControl(IOCTL_INTELVT_STOP, nullptr, 0, nullptr, 0);
}

bool KernelDriver::bsodRainbow()
{
    return deviceIoControl(IOCTL_BSOD_RAINBOW, nullptr, 0, nullptr, 0);
}

bool KernelDriver::bsodSetColor(const BsodColorConfig& color)
{
    return deviceIoControl(IOCTL_BSOD_SETCOLOR, const_cast<BsodColorConfig*>(&color), sizeof(color), nullptr, 0);
}

bool KernelDriver::bsodSetStopCode(const QString& stopCode)
{
    QByteArray uni = ansiToUnicodeBytes(stopCode);
    return deviceIoControl(IOCTL_BSOD_SETSTOPCODE, uni.data(), uni.size(), nullptr, 0);
}

// ===== 端口 I/O =====

bool KernelDriver::sendDataToPort(quint16 port, quint16 type, const QByteArray& data)
{
    QByteArray inBuffer;
    inBuffer.reserve(4 + data.size());
    inBuffer.append(reinterpret_cast<const char*>(&port), 2);
    inBuffer.append(reinterpret_cast<const char*>(&type), 2);
    inBuffer.append(data);
    return deviceIoControl(IOCTL_SENDDATA_TO_PORT, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

QByteArray KernelDriver::readDataInPort(quint16 port, quint16 type)
{
    struct { quint16 port; quint16 type; } inData = { port, type };
    QByteArray out(type, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_READDATA_IN_PORT, &inData, sizeof(inData), out.data(), out.size(), &br)) {
        return QByteArray();
    }
    return out;
}

bool KernelDriver::portPowerOff()
{
    return sendDataToPort(100, 1, QByteArray(1, (char)0xFE)); // __outbyte(0x64, 0xFE)
}

qint32 KernelDriver::portReadCMOS(qint32 address)
{
    if (!sendDataToPort(112, 1, QByteArray(1, (char)(address & 0xFF)))) return -1;
    QByteArray result = readDataInPort(113, 1);
    if (result.size() >= 1) return (quint8)result[0];
    return -1;
}

bool KernelDriver::portWriteCMOS(qint32 address, qint32 data)
{
    if (!sendDataToPort(112, 1, QByteArray(1, (char)(address & 0xFF)))) return false;
    return sendDataToPort(113, 1, QByteArray(1, (char)(data & 0xFF)));
}

bool KernelDriver::portKillBIOSPassword()
{
    if (!portWriteCMOS(16, 47)) return false;
    return portWriteCMOS(46, 0);
}

void KernelDriver::portDisEnaKeyboard(bool enable)
{
    sendDataToPort(100, 1, QByteArray(1, enable ? (char)0xAE : (char)0xAD));
}

void KernelDriver::portDisEnaPS2Mouse(bool enable)
{
    sendDataToPort(100, 1, QByteArray(1, enable ? (char)0xA8 : (char)0xA7));
}

// ===== 驱动/设备枚举 =====

QStringList KernelDriver::enumDrivers()
{
    QStringList result;
    QByteArray out(512 * 1024, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_ENUMDRIVERS, nullptr, 0, out.data(), out.size(), &br)) {
        return result;
    }
    int count = br / 1024;
    for (int i = 0; i < count; ++i) {
        QByteArray nameBytes = out.mid(i * 1024, 1024);
        QString name = unicodeBytesToAnsi(nameBytes);
        int idx = name.lastIndexOf('\\');
        if (idx >= 0) name = name.mid(idx + 1);
        result.append(name);
    }
    return result;
}

QStringList KernelDriver::enumDevices()
{
    QStringList result;
    QByteArray out(512 * 1024, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_ENUMDEVICES, nullptr, 0, out.data(), out.size(), &br)) {
        return result;
    }
    int count = br / 1024;
    for (int i = 0; i < count; ++i) {
        QByteArray nameBytes = out.mid(i * 1024, 1024);
        QString name = unicodeBytesToAnsi(nameBytes);
        int idx = name.lastIndexOf('\\');
        if (idx >= 0) name = name.mid(idx + 1);
        result.append(name);
    }
    return result;
}

QStringList KernelDriver::enumDriversPath()
{
    QStringList result;
    QByteArray out(512 * 1024, '\0');
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_ENUMDRIVERSPATH, nullptr, 0, out.data(), out.size(), &br)) {
        return result;
    }
    int count = br / 1024;
    for (int i = 0; i < count; ++i) {
        QByteArray nameBytes = out.mid(i * 1024, 1024);
        QString name = unicodeBytesToAnsi(nameBytes);
        result.append(name);
    }
    return result;
}

// ===== 系统功能开关 =====

bool KernelDriver::disableLoadDriver(bool disable)
{
    return disEnaSystemFunction(disable ? SF_DisableLoadDriver : SF_EnableLoadDriver);
}

bool KernelDriver::disableCreateProcess(bool disable)
{
    return disEnaSystemFunction(disable ? SF_DisableCreateProcess : SF_EnableCreateProcess);
}

bool KernelDriver::disableRegisterControl(bool disable)
{
    return disEnaSystemFunction(disable ? SF_DisableRegisterControl : SF_EnableRegisterControl);
}

bool KernelDriver::disEnaSystemFunction(SystemFunctionOp op)
{
    qint16 v = (qint16)op;
    return deviceIoControl(IOCTL_DISENA_SYSTEMFUNCTION, &v, sizeof(v), nullptr, 0);
}

// ===== 注册表操作 =====

bool KernelDriver::writeRegisterKey(const QString& keyRoot, const QString& keyPath,
                                    const QString& keyName, quint32 keyType, const QByteArray& keyData)
{
    QByteArray tempPath = ansiToUnicodeBytes(keyRoot + keyPath);
    tempPath.resize(2048);
    QByteArray tempName = ansiToUnicodeBytes(keyName);
    tempName.resize(1024);
    QByteArray typeBytes(reinterpret_cast<const char*>(&keyType), sizeof(keyType));
    QByteArray inBuffer = tempPath + tempName + typeBytes + keyData;
    return deviceIoControl(IOCTL_WRITEREGISTERKEY, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::writeRegisterKeyDWORD(const QString& keyRoot, const QString& keyPath,
                                         const QString& keyName, qint32 keyData)
{
    QByteArray data(reinterpret_cast<const char*>(&keyData), sizeof(keyData));
    return writeRegisterKey(keyRoot, keyPath, keyName, REG_DWORD, data);
}

bool KernelDriver::writeRegisterKeyQWORD(const QString& keyRoot, const QString& keyPath,
                                         const QString& keyName, qint64 keyData)
{
    QByteArray data(reinterpret_cast<const char*>(&keyData), sizeof(keyData));
    return writeRegisterKey(keyRoot, keyPath, keyName, REG_QWORD, data);
}

bool KernelDriver::writeRegisterKeySZ(const QString& keyRoot, const QString& keyPath,
                                      const QString& keyName, const QString& keyData)
{
    QByteArray tempPath = ansiToUnicodeBytes(keyRoot + keyPath);
    tempPath.resize(2048);
    QByteArray tempName = ansiToUnicodeBytes(keyName);
    tempName.resize(1024);
    quint32 keyType = REG_SZ;
    QByteArray typeBytes(reinterpret_cast<const char*>(&keyType), sizeof(keyType));
    QByteArray dataUni = ansiToUnicodeBytes(keyData);
    QByteArray inBuffer = tempPath + tempName + typeBytes + dataUni;
    return deviceIoControl(IOCTL_WRITEREGISTERKEY, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::deleteRegisterKey(const QString& keyRoot, const QString& keyPath, const QString& keyName)
{
    QByteArray tempPath = ansiToUnicodeBytes(keyRoot + keyPath);
    tempPath.resize(2048);
    QByteArray tempName = ansiToUnicodeBytes(keyName);
    tempName.resize(1024);
    QByteArray inBuffer = tempPath + tempName;
    return deviceIoControl(IOCTL_DELETEREGISTERKEY, inBuffer.data(), inBuffer.size(), nullptr, 0);
}

bool KernelDriver::deleteRegisterTree(const QString& keyRoot, const QString& keyPath)
{
    QByteArray temp = ansiToUnicodeBytes(keyRoot + keyPath);
    return deviceIoControl(IOCTL_DELETEREGISTERTREE, temp.data(), temp.size(), nullptr, 0);
}

// ===== Hook 操作 =====

bool KernelDriver::hookAntiTerminateProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_HOOK_PROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::hookUnAntiTerminateProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_HOOK_UNPROTECTPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::hookProtectThread(quint32 tid)
{
    return deviceIoControl(IOCTL_HOOK_PROTECTTHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::hookUnProtectThread(quint32 tid)
{
    return deviceIoControl(IOCTL_HOOK_UNPROTECTTHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::hookDisableBSOD(qint32 operation)
{
    return deviceIoControl(IOCTL_HOOK_DISABLEBSOD, &operation, sizeof(operation), nullptr, 0);
}

bool KernelDriver::hookQueryStatus(qint32 operation)
{
    quint32 out = 0;
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_HOOK_QUERYSTATUS, &operation, sizeof(operation), &out, sizeof(out), &br)) {
        return false;
    }
    return out != 0;
}

bool KernelDriver::hookDisableNtDeviceIoControlFile()
{
    return deviceIoControl(IOCTL_HOOK_DISABLENTDEVICEIOCONTROLFILE, nullptr, 0, nullptr, 0);
}

// ==================== ARK 扩展 ====================
// 注意：METHOD_BUFFERED 下输入输出共用 SystemBuffer。
// 所有"输入结构+输出结构"的 IOCTL 都必须先在输出尺寸的缓冲里放好输入，
// 驱动读完输入参数后才会覆写缓冲为输出。

// ---- 辅助：宽字符串拷入 wchar 缓冲（保证 null 结尾） ----
static void copyWStrToBuffer(wchar_t* dst, size_t dstChars, const QString& src)
{
    size_t n = qMin<size_t>((size_t)src.size(), dstChars - 1);
    for (size_t i = 0; i < n; ++i) {
        dst[i] = src[(int)i].unicode();
    }
    dst[n] = L'\0';
}

static QString wStrToQString(const wchar_t* s, size_t maxChars)
{
    size_t n = 0;
    while (n < maxChars && s[n] != L'\0') ++n;
    return QString::fromWCharArray(s, (int)n);
}

// ===== ARK: PspCidTable =====

bool KernelDriver::cidEnumProcesses(ArkCidProcessArray& out)
{
    return deviceIoControl(IOCTL_CID_ENUMPROCESSES, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::cidEnumThreads(ArkCidThreadArray& out, quint32 ownerPid)
{
    if (!deviceIoControl(IOCTL_CID_ENUMTHREADS, nullptr, 0, &out, sizeof(out)))
        return false;
    // 驱动返回全部线程，按 ownerPid 过滤
    if (ownerPid != 0) {
        quint32 w = 0;
        for (quint32 i = 0; i < out.count; ++i) {
            if (out.entries[i].ownerPid == ownerPid) {
                out.entries[w++] = out.entries[i];
            }
        }
        out.count = w;
    }
    return true;
}

bool KernelDriver::cidTerminateThread(quint32 tid)
{
    return deviceIoControl(IOCTL_CID_TERMINATETHREAD, &tid, sizeof(tid), nullptr, 0);
}

bool KernelDriver::cidTerminateProcess(quint32 pid)
{
    return deviceIoControl(IOCTL_CID_TERMINATEPROCESS, &pid, sizeof(pid), nullptr, 0);
}

bool KernelDriver::cidEnumSystemThreads(ArkSysThreadArray& out)
{
    // 驱动协议 (IOCTL_CID_ENUMSYSTEMTHREADS, function=0x604):
    //   输入:  无
    //   输出:  ARK_SYS_THREAD_ARRAY（CID 全量枚举 + PsIsSystemThread 过滤，
    //          StartAddress 已在内核完成模块归属，无需逐线程二次查询）
    return deviceIoControl(IOCTL_CID_ENUMSYSTEMTHREADS, nullptr, 0,
                           &out, sizeof(out));
}

bool KernelDriver::arkEnumSysCallbacks(ArkCallbackArray& out)
{
    return deviceIoControl(IOCTL_ENUMSYSCALLBACKS, nullptr, 0,
                           &out, sizeof(out));
}

bool KernelDriver::arkEnumUnloadedDrivers(ArkUnloadedArray& out)
{
    return deviceIoControl(IOCTL_ENUMUNLOADEDDRIVERS, nullptr, 0,
                           &out, sizeof(out));
}

// ===== ARK: 键盘监视 =====

bool KernelDriver::kbdStart()
{
    // 某些旧版 WDM 分发器对 METHOD_BUFFERED 的完全空缓冲返回 87；
    // 命令本身无数据，传一个明确的占位字节兼容这类分发器。
    quint32 command = 0;
    return deviceIoControl(IOCTL_KBD_START, &command, sizeof(command), nullptr, 0);
}

bool KernelDriver::kbdStop()
{
    quint32 command = 0;
    return deviceIoControl(IOCTL_KBD_STOP, &command, sizeof(command), nullptr, 0);
}

bool KernelDriver::kbdReadLog(quint64 lastSeq, ArkKbdLog& out)
{
    // 输入: ULONGLONG lastSeq；输出: ARK_KBD_LOG（共用缓冲）
    QByteArray buf(sizeof(ArkKbdLog), Qt::Uninitialized);
    memcpy(buf.data(), &lastSeq, sizeof(lastSeq));
    if (!deviceIoControl(IOCTL_KBD_READLOG, buf.data(), sizeof(quint64),
                         buf.data(), sizeof(ArkKbdLog))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkKbdLog));
    return true;
}

bool KernelDriver::kbdClearLog()
{
    return deviceIoControl(IOCTL_KBD_CLEARLOG, nullptr, 0, nullptr, 0);
}

bool KernelDriver::kbdGetStatus(ArkKbdStatus& out)
{
    return deviceIoControl(IOCTL_KBD_GETSTATUS, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::kbdEnumHotkeys(ArkHotkeyArray& out)
{
    // 无输入；输出 ARK_HOTKEY_ARRAY
    return deviceIoControl(IOCTL_KBD_ENUMHOTKEYS, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::kbdModifyHotkey(quint32 pid, quint32 id, quint32 fsModifiers, quint32 vk,
                                   quint32 newFsModifiers, quint32 newVk)
{
    ArkHotkeyModInput input;
    memset(&input, 0, sizeof(input));
    input.pid = pid;
    input.id = id;
    input.fsModifiers = fsModifiers;
    input.vk = vk;
    input.newFsModifiers = newFsModifiers;
    input.newVk = newVk;
    return deviceIoControl(IOCTL_KBD_MODHOTKEY, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::kbdDeleteHotkey(quint32 pid, quint32 id, quint32 fsModifiers, quint32 vk)
{
    ArkHotkeyModInput input;
    memset(&input, 0, sizeof(input));
    input.pid = pid;
    input.id = id;
    input.fsModifiers = fsModifiers;
    input.vk = vk;
    return deviceIoControl(IOCTL_KBD_DELHOTKEY, &input, sizeof(input), nullptr, 0);
}

// ===== ARK: 进程行为监视 =====

bool KernelDriver::behaviorReadLog(quint64 lastSeq, ArkBehaviorLog& out)
{
    // 输入: ULONGLONG lastSeq；输出: ARK_BEHAVIOR_LOG（共用缓冲）
    QByteArray buf(sizeof(ArkBehaviorLog), Qt::Uninitialized);
    memcpy(buf.data(), &lastSeq, sizeof(lastSeq));
    if (!deviceIoControl(IOCTL_BEHAVIOR_READ, buf.data(), sizeof(quint64),
                         buf.data(), sizeof(ArkBehaviorLog))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkBehaviorLog));
    return true;
}

bool KernelDriver::behaviorClearLog()
{
    return deviceIoControl(IOCTL_BEHAVIOR_CLEAR, nullptr, 0, nullptr, 0);
}

bool KernelDriver::behaviorSetFilter(quint32 mask)
{
    quint32 data[2] = { mask, 0 };
    return deviceIoControl(IOCTL_BEHAVIOR_SETFILTER, data, sizeof(data), nullptr, 0);
}

bool KernelDriver::filemonStart()
{
    quint32 command = 0;
    return deviceIoControl(IOCTL_FILEMON_START, &command, sizeof(command), nullptr, 0);
}

bool KernelDriver::filemonStop()
{
    quint32 command = 0;
    return deviceIoControl(IOCTL_FILEMON_STOP, &command, sizeof(command), nullptr, 0);
}

bool KernelDriver::hardenAddPid(quint32 pid, quint64 ntdllBase, quint32* flags)
{
    if (flags) *flags = 0;
    ArkHardenPid data;
    data.pid = pid;
    data.flags = 0;
    data.ntdllBase = ntdllBase;
    // 输入输出共用 16 字节缓冲, 驱动把结果位图写回 data.flags
    if (!deviceIoControl(IOCTL_HARDEN_ADD, &data, sizeof(data), &data, sizeof(data), nullptr)) {
        return false;
    }
    if (flags) *flags = data.flags;
    return true;
}

bool KernelDriver::hardenRemovePid(quint32 pid)
{
    quint32 data[2] = { pid, 0 };
    return deviceIoControl(IOCTL_HARDEN_REMOVE, data, sizeof(data), nullptr, 0);
}

bool KernelDriver::probeResidentDriver()
{
    // 只探测不持有：打开后立即关闭
    HANDLE h = getDriveHandle(m_driverName);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) {
        return false;
    }
    CloseHandle(h);
    return true;
}

// ===== ARK: 命名空间 =====

bool KernelDriver::nsEnumDir(const QString& path, ArkNsArray& out)
{
    // 输入: WCHAR 路径；输出: ARK_NS_ARRAY（共用缓冲）
    std::wstring ws = path.toStdWString();
    if (ws.size() > 500) ws.resize(500);
    QByteArray buf(sizeof(ArkNsArray), Qt::Uninitialized);
    memcpy(buf.data(), ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    if (!deviceIoControl(IOCTL_NS_ENUMDIR, buf.data(),
                         (DWORD)((ws.size() + 1) * sizeof(wchar_t)),
                         buf.data(), sizeof(ArkNsArray))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkNsArray));
    return true;
}

bool KernelDriver::nsDeleteObject(const QString& typeName, const QString& fullPath)
{
    ArkNsDeleteInput input;
    memset(&input, 0, sizeof(input));
    copyWStrToBuffer(input.typeName, 24, typeName);
    copyWStrToBuffer(input.path, 520, fullPath);
    return deviceIoControl(IOCTL_NS_DELETEOBJECT, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::nsQueryPath(const QString& path, ArkNsObjectInfo& out)
{
    std::wstring ws = path.toStdWString();
    if (ws.size() > 500) ws.resize(500);
    QByteArray buf(sizeof(ArkNsObjectInfo), Qt::Uninitialized);
    memcpy(buf.data(), ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    if (!deviceIoControl(IOCTL_NS_QUERYPATH, buf.data(),
                         (DWORD)((ws.size() + 1) * sizeof(wchar_t)),
                         buf.data(), sizeof(ArkNsObjectInfo))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkNsObjectInfo));
    return true;
}

// ===== ARK: 注册表全内核操作 =====
// 输入为 UI 格式路径（HKEY_LOCAL_MACHINE\SOFTWARE\...），内核翻译为
// \Registry\Machine\... 并在 System 令牌上下文执行，可越过普通 ACL 限制。

bool KernelDriver::regEnumSubKeys(const QString& uiPath, ArkRegEnumKeys& out)
{
    std::wstring ws = uiPath.toStdWString();
    if (ws.size() > 380) ws.resize(380);
    QByteArray buf(sizeof(ArkRegEnumKeys), Qt::Uninitialized);
    memset(buf.data(), 0, sizeof(ArkRegEnumKeys));
    memcpy(buf.data(), ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    if (!deviceIoControl(IOCTL_REG_ENUMSUBKEYS, buf.data(),
                         (DWORD)((ws.size() + 1) * sizeof(wchar_t)),
                         buf.data(), sizeof(ArkRegEnumKeys))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkRegEnumKeys));
    // 防御: 驱动返回异常计数时截断, 避免读越界
    if (out.count > 2048) out.count = 2048;
    return true;
}

bool KernelDriver::regEnumValues(const QString& uiPath, ArkRegEnumValues& out)
{
    std::wstring ws = uiPath.toStdWString();
    if (ws.size() > 380) ws.resize(380);
    QByteArray buf(sizeof(ArkRegEnumValues), Qt::Uninitialized);
    memset(buf.data(), 0, sizeof(ArkRegEnumValues));
    memcpy(buf.data(), ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    if (!deviceIoControl(IOCTL_REG_ENUMVALUES, buf.data(),
                         (DWORD)((ws.size() + 1) * sizeof(wchar_t)),
                         buf.data(), sizeof(ArkRegEnumValues))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkRegEnumValues));
    // 防御: 驱动返回异常计数/长度时截断, 避免读越界
    if (out.count > 512) out.count = 512;
    for (quint32 i = 0; i < out.count; ++i) {
        if (out.values[i].dataLength > 1024) out.values[i].dataLength = 1024;
    }
    return true;
}

bool KernelDriver::regCreateKey(const QString& uiPath)
{
    std::wstring ws = uiPath.toStdWString();
    if (ws.size() > 380) ws.resize(380);
    QByteArray buf((qsizetype)(ws.size() + 1) * (qsizetype)sizeof(wchar_t), Qt::Uninitialized);
    memcpy(buf.data(), ws.c_str(), buf.size());
    return deviceIoControl(IOCTL_REG_CREATEKEY, buf.data(), (DWORD)buf.size(), nullptr, 0);
}

bool KernelDriver::regDeleteKey(const QString& uiPath)
{
    std::wstring ws = uiPath.toStdWString();
    if (ws.size() > 380) ws.resize(380);
    QByteArray buf((qsizetype)(ws.size() + 1) * (qsizetype)sizeof(wchar_t), Qt::Uninitialized);
    memcpy(buf.data(), ws.c_str(), buf.size());
    return deviceIoControl(IOCTL_REG_DELETEKEY, buf.data(), (DWORD)buf.size(), nullptr, 0);
}

bool KernelDriver::regDeleteValue(const QString& uiPath, const QString& valueName)
{
    ArkRegValueNameInput input;
    memset(&input, 0, sizeof(input));
    copyWStrToBuffer(input.keyPath, 384, uiPath);
    copyWStrToBuffer(input.valueName, 256, valueName);
    return deviceIoControl(IOCTL_REG_DELETEVALUE, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::regSetValue(const QString& uiPath, const QString& valueName,
                               quint32 type, const QByteArray& data)
{
    ArkRegSetValueInput input;
    memset(&input, 0, sizeof(input));
    copyWStrToBuffer(input.keyPath, 384, uiPath);
    copyWStrToBuffer(input.valueName, 256, valueName);
    input.type = type;

    QByteArray d = data;
    if (d.size() > (int)sizeof(input.data)) d.resize(sizeof(input.data));
    input.dataLength = (quint32)d.size();
    if (!d.isEmpty()) {
        memcpy(input.data, d.constData(), d.size());
    }
    return deviceIoControl(IOCTL_REG_SETVALUE, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::regRenameKey(const QString& uiPath, const QString& newName)
{
    ArkRegRenameKeyInput input;
    memset(&input, 0, sizeof(input));
    copyWStrToBuffer(input.keyPath, 384, uiPath);
    copyWStrToBuffer(input.newName, 256, newName);
    return deviceIoControl(IOCTL_REG_RENAMEKEY, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::regRenameValue(const QString& uiPath, const QString& oldName,
                                  const QString& newName)
{
    ArkRegRenameValueInput input;
    memset(&input, 0, sizeof(input));
    copyWStrToBuffer(input.keyPath, 384, uiPath);
    copyWStrToBuffer(input.oldName, 256, oldName);
    copyWStrToBuffer(input.newName, 256, newName);
    return deviceIoControl(IOCTL_REG_RENAMEVALUE, &input, sizeof(input), nullptr, 0);
}

// ===== ARK: 进程扩展 =====

bool KernelDriver::enumProcessHandles(quint32 pid, quint32 startIndex, ArkHandleArray& out)
{
    QByteArray buf(sizeof(ArkHandleArray), Qt::Uninitialized);
    ArkHandleEnumInput input;
    input.pid = pid;
    input.startIndex = startIndex;
    memcpy(buf.data(), &input, sizeof(input));
    if (!deviceIoControl(IOCTL_ENUMPROCESSHANDLES, buf.data(), sizeof(input),
                         buf.data(), sizeof(ArkHandleArray))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkHandleArray));
    return true;
}

bool KernelDriver::closeProcessHandle(quint32 pid, quint64 handle)
{
    ArkCloseHandleInput input;
    input.pid = pid;
    input.pad = 0;
    input.handle = handle;
    return deviceIoControl(IOCTL_CLOSEHANDLE, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::arkReadProcessMemory(quint32 pid, quint64 address, quint32 size, QByteArray& outData)
{
    if (size == 0 || size > 0x10000) {
        outData.clear();
        return false;
    }
    ArkRwMemoryInput input;
    input.pid = pid;
    input.pad = 0;
    input.address = address;
    input.size = size;
    input.pad2 = 0;

    QByteArray buf(qMax<quint32>(sizeof(ArkRwMemoryInput), size), Qt::Uninitialized);
    memcpy(buf.data(), &input, sizeof(input));
    DWORD br = 0;
    if (!deviceIoControl(IOCTL_ARK_READMEMORY, buf.data(), sizeof(ArkRwMemoryInput),
                         buf.data(), size, &br)) {
        outData.clear();
        return false;
    }
    if (br > size) br = size;
    outData = QByteArray(buf.constData(), (int)br);
    return true;
}

bool KernelDriver::arkWriteProcessMemory(quint32 pid, quint64 address, const QByteArray& data)
{
    if (data.isEmpty() || (quint32)data.size() > 0x10000) return false;

    QByteArray buf(sizeof(ArkRwMemoryInput) + data.size(), Qt::Uninitialized);
    ArkRwMemoryInput input;
    input.pid = pid;
    input.pad = 0;
    input.address = address;
    input.size = (quint32)data.size();
    input.pad2 = 0;
    memcpy(buf.data(), &input, sizeof(input));
    memcpy(buf.data() + sizeof(input), data.constData(), data.size());
    return deviceIoControl(IOCTL_ARK_WRITEMEMORY, buf.data(), (DWORD)buf.size(), nullptr, 0);
}

bool KernelDriver::arkQueryVirtualMemory(quint32 pid, quint64 address, ArkVmInfo& out)
{
    ArkQueryVmInput input;
    input.pid = pid;
    input.pad = 0;
    input.address = address;
    return deviceIoControl(IOCTL_ARK_QUERYVM, &input, sizeof(input), &out, sizeof(out));
}

bool KernelDriver::arkGetPriority(quint32 pid, ArkPriorityInfo& out)
{
    ArkPidInput input;
    input.pid = pid;
    input.pad = 0;
    return deviceIoControl(IOCTL_ARK_GETPRIORITY, &input, sizeof(input), &out, sizeof(out));
}

bool KernelDriver::arkSetPriority(quint32 pid, quint32 priorityClass)
{
    ArkSetPriorityInput input;
    input.pid = pid;
    input.pad = 0;
    input.priorityClass = priorityClass;
    return deviceIoControl(IOCTL_ARK_SETPRIORITY, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::arkGetAffinity(quint32 pid, ArkAffinityInfo& out)
{
    ArkPidInput input;
    input.pid = pid;
    input.pad = 0;
    return deviceIoControl(IOCTL_ARK_GETAFFINITY, &input, sizeof(input), &out, sizeof(out));
}

bool KernelDriver::arkSetAffinity(quint32 pid, quint64 affinity)
{
    ArkSetAffinityInput input;
    input.pid = pid;
    input.pad = 0;
    input.affinity = affinity;
    return deviceIoControl(IOCTL_ARK_SETAFFINITY, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::arkGetTokenPrivileges(quint32 pid, ArkPrivilegeArray& out)
{
    ArkPidInput input;
    input.pid = pid;
    input.pad = 0;
    return deviceIoControl(IOCTL_ARK_GETPRIVILEGES, &input, sizeof(input), &out, sizeof(out));
}

bool KernelDriver::arkSetTokenPrivilege(quint32 pid, quint64 luid, quint32 enable)
{
    ArkSetPrivInput input;
    input.pid = pid;
    input.pad = 0;
    input.luid = luid;
    input.enable = enable;
    input.pad2 = 0;
    return deviceIoControl(IOCTL_ARK_SETPRIVILEGE, &input, sizeof(input), nullptr, 0);
}

bool KernelDriver::arkGetTokenInfo(quint32 pid, ArkTokenInfo& out)
{
    ArkPidInput input;
    input.pid = pid;
    input.pad = 0;
    return deviceIoControl(IOCTL_ARK_GETTOKENINFO, &input, sizeof(input), &out, sizeof(out));
}

// ===== ARK: 文件占用扫描 =====

bool KernelDriver::scanFileOccupy(const QString& ntPath, ArkOccupyArray& out)
{
    // 输入: WCHAR 路径（如 \??\C:\x.txt 或 \Device\HarddiskVolume4\x.txt）
    // 输出: ARK_OCCUPY_ARRAY（共用缓冲）
    std::wstring ws = ntPath.toStdWString();
    if (ws.size() > 1000) ws.resize(1000);
    QByteArray buf(sizeof(ArkOccupyArray), Qt::Uninitialized);
    memcpy(buf.data(), ws.c_str(), (ws.size() + 1) * sizeof(wchar_t));
    if (!deviceIoControl(IOCTL_SCANFILEOCCUPY, buf.data(),
                         (DWORD)((ws.size() + 1) * sizeof(wchar_t)),
                         buf.data(), sizeof(ArkOccupyArray))) {
        return false;
    }
    memcpy(&out, buf.constData(), sizeof(ArkOccupyArray));
    return true;
}

// ===== ARK: 内核视图 =====

bool KernelDriver::arkEnumDriverObjects(ArkDriverObjArray& out)
{
    return deviceIoControl(IOCTL_ARK_ENUMDRIVEROBJS, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::arkGetDriverDetail(quint64 driverObject, ArkDriverDetail& out)
{
    return deviceIoControl(IOCTL_ARK_GETDRIVERDETAIL, &driverObject, sizeof(driverObject),
                           &out, sizeof(out));
}

bool KernelDriver::arkEnumKernelModules(ArkModuleArray& out)
{
    return deviceIoControl(IOCTL_ARK_ENUMMODULES, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::arkEnumDeviceObjects(ArkDeviceArray& out)
{
    return deviceIoControl(IOCTL_ARK_ENUMDEVICES, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::arkGetDeviceDetail(quint64 deviceObject, ArkDeviceDetail& out)
{
    return deviceIoControl(IOCTL_ARK_GETDEVICEDETAIL, &deviceObject, sizeof(deviceObject),
                           &out, sizeof(out));
}

bool KernelDriver::arkQuerySSDT(ArkSsdtInfo& out)
{
    return deviceIoControl(IOCTL_ARK_QUERYSSDT, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::arkQueryIDT(ArkIdtInfo& out)
{
    return deviceIoControl(IOCTL_ARK_QUERYIDT, nullptr, 0, &out, sizeof(out));
}

bool KernelDriver::arkEnumObjectTypes(ArkObjTypeArray& out)
{
    return deviceIoControl(IOCTL_ARK_ENUMOBJTYPES, nullptr, 0, &out, sizeof(out));
}
