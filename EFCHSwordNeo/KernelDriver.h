#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QStringList>
#include <windows.h>

// PPL/PP 保护级别常量
enum ProcessProtectionLevel {
    PPL_None          = 0x00,
    PPL_Authenticode  = 0x11,
    PPL_CodeGen       = 0x21,
    PPL_Antimalware   = 0x31,
    PPL_Lsa           = 0x41,
    PPL_Windows       = 0x51,
    PPL_WinTcb        = 0x61,
    PPL_WinSystem     = 0x71,
    PP_Authenticode   = 0x12,
    PP_CodeGen        = 0x22,
    PP_Antimalware    = 0x32,
    PP_Lsa            = 0x42,
    PP_Windows        = 0x52,
    PP_WinTcb         = 0x62,
    PP_WinSystem      = 0x72
};

// 系统功能开关 (与驱动 IOCTL_DISENA_SYSTEMFUNCTION 的 inbuffer 取值严格一致)
// 驱动 case 定义: 0=EnableLoadDriver 1=DisableLoadDriver
//                 2=EnableCreateProcess 3=DisableCreateProcess
//                 4=EnableRegisterControl 5=DisableRegisterControl
enum SystemFunctionOp {
    SF_EnableLoadDriver         = 0,
    SF_DisableLoadDriver        = 1,
    SF_EnableCreateProcess      = 2,
    SF_DisableCreateProcess     = 3,
    SF_EnableRegisterControl    = 4,
    SF_DisableRegisterControl   = 5
};

// HAL 电源控制
enum HalPowerControlCode {
    HalPower_Halt       = 0,  // 系统停机(关CPU)
    HalPower_PowerOff   = 1,  // 断电
    HalPower_Reboot     = 2,  // 重启
    HalPower_ForceReboot= 3,  // 强制重启
    HalPower_Interactive= 4   // 进入交互模式
};

// 文件/目录信息结构（对应易语言 DATA_INFO_ANSI）
struct DataInfoAnsi {
    QString fileName;
    bool    isDirectory;
};

// 线程信息（驱动 IOCTL_ENUMTHREADBYPROCESS 输出）
struct ThreadInfo {
    quint32 tid = 0;       // 线程 ID
    quint64 ethread = 0;   // 内核 ETHREAD 对象地址（0 表示无此信息，如 ToolHelp32 回退）
};

// NTFS 目录枚举结果项（对应驱动 NTFS_DATA_INFO, 2052 字节）
#pragma pack(push, 1)
struct NtfsDataInfo {
    wchar_t name[1024];   // 文件/目录名 (UTF-16, null 结尾)
    quint32 type;         // 0=文件 1=目录 2=系统元数据文件 3=系统元数据目录
};
#pragma pack(pop)

// NTFS 目录数量统计（对应驱动 NTFS_FILE_FOLDER_QUANTITY, 20 字节）
#pragma pack(push, 1)
struct NtfsFileFolderQuantity {
    quint32 fileCount;          // 普通文件数 (Type=0)
    quint32 folderCount;        // 普通目录数 (Type=1)
    quint32 systemFileCount;    // 系统元数据文件数 (Type=2)
    quint32 systemFolderCount;  // 系统元数据目录数 (Type=3)
    quint32 totalCount;         // 总数
};
#pragma pack(pop)

// BSOD 颜色配置
#pragma pack(push, 1)
struct BsodColorConfig {
    quint8 backgroundRed;
    quint8 backgroundGreen;
    quint8 backgroundBlue;
};
#pragma pack(pop)

// 注册表根键（内核模式路径）
namespace RegRoot {
    const QString HKEY_CLASSES_ROOT_KERNELMODE   = QStringLiteral("\\Registry\\Machine\\SOFTWARE\\Classes\\");
    const QString HKEY_LOCAL_MACHINE_KERNELMODE = QStringLiteral("\\Registry\\Machine\\");
    const QString HKEY_USERS_KERNELMODE         = QStringLiteral("\\Registry\\User\\");
    const QString HKEY_CURRENT_USER_KERNELMODE  = QStringLiteral("\\Registry\\User\\<CurrentUserSID>\\");
}

// 驱动封装类，对应易语言的 EFCHKMD 类
// 驱动 IOCTL 代码使用 CTL_CODE(FILE_DEVICE_UNKNOWN=0x22=34, Function, METHOD_BUFFERED, FILE_ANY_ACCESS)
class KernelDriver
{
public:
    KernelDriver();
    ~KernelDriver();

    // ===== 驱动加载/卸载 =====
    // 安装并加载驱动，返回是否成功。sysFilePath 为 .sys 文件完整路径
    bool installDriver(const QString& sysFilePath, QString* errMsg = nullptr);
    // 卸载驱动
    bool uninstallDriver();
    // 驱动是否已加载
    bool isLoaded() const { return m_hDevice != INVALID_HANDLE_VALUE; }
    // 获取设备句柄
    HANDLE deviceHandle() const { return m_hDevice; }
    // 驱动服务名
    QString driverName() const { return m_driverName; }

    // ===== 进程操作 =====
    bool zwKillProcess(quint32 pid);                    // ZwTerminateProcess
    bool apcTerminateProcess(quint32 pid);              // APC 结束进程
    bool suspendProcess(quint32 pid);                   // PsSuspendProcess
    bool resumeProcess(quint32 pid);                    // PsResumeProcess
    bool hideProcess(quint32 pid);                      // 隐藏进程(断链)
    bool unHideProcess(quint32 pid);                    // 取消隐藏进程
    bool protectProcess(quint32 pid);                   // 保护进程(ObRegisterCallbacks)
    bool unProtectProcess(quint32 pid);                 // 取消保护进程
    bool forceProtectProcess(quint32 pid);              // 强力保护进程
    bool unForceProtectProcess(quint32 pid);            // 取消强力保护
    bool memKillProcess(quint32 pid);                   // 破坏进程内存数据
    bool setPPL(quint32 pid, quint32 protectType);      // 设置 PPL/PP 保护
    bool setPID(quint64 pid, quint64 newPid);           // 重新设置进程 PID
    bool setProcessCritical(quint64 pid, qint32 isCritical); // 设置为系统关键进程
    bool pspTerminateProcess(quint32 pid);              // PspTerminateProcess

    // 枚举进程（通过 EPROCESS 链表，断链/改PID 隐藏无效）
    QVector<quint32> enumProcesses();
    // 获取进程 PPL，失败返回 -1
    qint32 getPPL(quint32 pid);
    // 获取进程 DosImageName
    QString getProcessDosImageName(quint32 pid);
    // 枚举进程线程（通过 EPROCESS 遍历 ThreadListEntry，可发现 PspCidTable 隐藏线程）
    // 返回 ThreadInfo 列表（含 TID 和内核 ETHREAD 地址），失败返回空列表
    QVector<ThreadInfo> enumThreadsByProcess(quint32 pid);
    // 通过 ETHREAD 内核地址获取线程所属驱动的完整路径（IOCTL_GETDRIVERPATCHBYETHREAD）
    // 失败返回空字符串
    QString getDriverPathByEThread(quint64 ethread);

    // ===== 线程操作 =====
    bool suspendThread(quint32 tid);                    // NtSuspendThread
    bool resumeThread(quint32 tid);                     // NtResumeThread
    bool terminateThread(quint32 tid);                  // NtTerminateThread
    bool pspTerminateThreadByPointerEasy(quint32 tid);  // 简单版销毁线程
    bool pspTerminateThreadByPointer(quint64 tid, qint32 exitCode, bool directTerminate);
    bool apcTerminateThread(quint32 tid);
    // 通过 ETHREAD 内核地址 APC 方式销毁线程（IOCTL_APCTERMINATETHREADBYETHREAD）
    bool apcTerminateThreadByEThread(quint64 ethread);

    // ===== 文件操作 =====
    bool zwDeleteFile(const QString& ntFilePath);       // 路径需 \??\C:\xxx
    bool occupyFile(const QString& ntFilePath);         // 占用文件
    bool forceDeleteFile(const QString& filePath);      // IRP 暴力删除
    bool forceOccupyFile(const QString& filePath);      // IRP 暴力占用
    bool forceWriteFile(const QString& filePath, const QByteArray& data); // IRP 暴力写文件
    bool irpCreateFile(const QString& filePath);
    bool irpCreateDirectory(const QString& dirPath);
    bool irpCopyFile(const QString& oldPath, const QString& newPath);
    bool irpMoveFile(const QString& oldPath, const QString& newPath);
    bool irpRenameFile(const QString& oldName, const QString& newName);
    bool irpSetInformationFile(const QString& filePath, quint64 attribute);
    bool posixForceDeleteFile(const QString& filePath);
    bool zwWriteFile(const QString& ntFilePath, const QByteArray& data);
    bool zwReadFile(const QString& ntFilePath, QByteArray& outData);
    bool irpReadFile(const QString& filePath, QByteArray& outData);
    qint64 irpGetFileSize(const QString& filePath);     // 失败返回 -1
    bool irpReadDisk(quint64 startSector, quint64 sectorNumber, QByteArray& outData);
    bool irpQueryFileAndFolders(const QString& filePath, QVector<DataInfoAnsi>& outList);
    qint32 irpGetFileAndFolderQuantity(const QString& filePath);
    bool flushMFT(const QString& driveLetter);

    // ===== DirectNTFS 操作 (直接解析 NTFS, 不经文件系统, 只读) =====
    // 初始化指定盘符的 NTFS 卷上下文。driveLetter 如 'C'
    bool ntfsInitVolume(wchar_t driveLetter);
    // 读取文件内容。filePath 为相对卷根的路径如 "\Windows\System32\drivers\etc\hosts"
    // 需先调用 ntfsInitVolume 初始化对应卷
    bool ntfsReadFile(const QString& filePath, QByteArray& outData);
    // 枚举目录。dirPath 为相对卷根路径如 "\Windows\System32"，根目录传 "\"
    bool ntfsEnumDirectory(const QString& dirPath, QVector<NtfsDataInfo>& outList);
    // 统计目录下文件/文件夹数量。dirPath 同上
    bool ntfsCountDirectory(const QString& dirPath, NtfsFileFolderQuantity& outQty);

    // ===== 磁盘操作 =====
    bool writeDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data);
    bool forceWriteDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data);
    bool storDirectReadDisk(quint64 startSector, quint64 sectorNumber, qint16 raidPort, QByteArray& outData);
    bool storDirectWriteDisk(quint64 startSector, quint64 sectorNumber, qint16 raidPort, const QByteArray& data);
    qint16 getVolumeRaidPort(const QString& volumeLetter); // 失败返回 -1

    // ===== 系统操作 =====
    bool bsod(quint32 code);
    bool bsodAuto();
    bool forceUnloadDriver(const QString& driverName);
    bool forceUnloadDevice(const QString& deviceName, qint16 operation); // 0=IoDeleteDevice 1=UnloadDriver
    bool antiDriverIoControl(const QString& driverName);
    bool haltCPU();
    bool haltAllCPUs();
    bool halPowerControl(HalPowerControlCode controlCode);
    bool intelVTSupport();
    bool intelVTInit();
    bool intelVTStop();
    bool bsodRainbow();
    bool bsodSetColor(const BsodColorConfig& color);
    bool bsodSetStopCode(const QString& stopCode);

    // 端口 I/O
    bool sendDataToPort(quint16 port, quint16 type, const QByteArray& data);
    QByteArray readDataInPort(quint16 port, quint16 type); // type: 1=UCHAR 2=USHORT 4=ULONG
    bool portPowerOff();           // 向 0x64 端口写 0xFE 断电
    qint32 portReadCMOS(qint32 address);   // 失败返回 -1
    bool portWriteCMOS(qint32 address, qint32 data);
    bool portKillBIOSPassword();
    void portDisEnaKeyboard(bool enable);
    void portDisEnaPS2Mouse(bool enable);

    // 驱动/设备枚举
    QStringList enumDrivers();
    QStringList enumDevices();
    QStringList enumDriversPath();

    // 系统功能开关
    bool disableLoadDriver(bool disable);
    bool disableCreateProcess(bool disable);
    bool disableRegisterControl(bool disable);
    bool disEnaSystemFunction(SystemFunctionOp op);

    // ===== 注册表操作 =====
    bool writeRegisterKey(const QString& keyRoot, const QString& keyPath,
                          const QString& keyName, quint32 keyType, const QByteArray& keyData);
    bool writeRegisterKeyDWORD(const QString& keyRoot, const QString& keyPath,
                               const QString& keyName, qint32 keyData);
    bool writeRegisterKeyQWORD(const QString& keyRoot, const QString& keyPath,
                               const QString& keyName, qint64 keyData);
    bool writeRegisterKeySZ(const QString& keyRoot, const QString& keyPath,
                            const QString& keyName, const QString& keyData);
    bool deleteRegisterKey(const QString& keyRoot, const QString& keyPath, const QString& keyName);
    bool deleteRegisterTree(const QString& keyRoot, const QString& keyPath);

    // ===== Hook 操作 =====
    bool hookAntiTerminateProcess(quint32 pid);
    bool hookUnAntiTerminateProcess(quint32 pid);
    bool hookProtectThread(quint32 tid);
    bool hookUnProtectThread(quint32 tid);
    bool hookDisableBSOD(qint32 operation); // 0=取消阻止蓝屏 1=阻止蓝屏
    bool hookQueryStatus(qint32 operation); // 1=DisableBSOD 2=DisableLoadDriver
    bool hookDisableNtDeviceIoControlFile();

    // ===== 通用 IOCTL 调用 =====
    bool deviceIoControl(quint32 function, LPVOID inBuffer, DWORD inSize,
                         LPVOID outBuffer, DWORD outSize, LPDWORD bytesReturned = nullptr);

    // 最近一次 IOCTL 调用的 Win32 错误码（GetLastError）
    // 用于诊断 Storport 等读写失败的原因
    DWORD lastIoError() const { return m_lastError; }

private:
    // 计算 IOCTL 代码: CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS)
    static DWORD ioctlCode(quint32 function) {
        return CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS);
    }

    // 尝试通过服务名获取已加载驱动的设备句柄
    HANDLE getDriveHandle(const QString& driverName);
    // 通过 NtLoadDriver 加载驱动服务
    HANDLE loadDrive(const QString& path, const QString& serviceName);
    // 通过 SCM 加载驱动服务（回退方案）
    HANDLE loadDriveViaSCM(const QString& path, const QString& serviceName);
    // 卸载驱动服务
    bool uninstallDrive(const QString& serviceName, bool stopFirst);

    // ANSI 字符串转 Unicode 字节集（含结束符 \0\0）
    static QByteArray ansiToUnicodeBytes(const QString& s);
    // Unicode 字节集转 ANSI 字符串
    static QString unicodeBytesToAnsi(const QByteArray& bytes);

    HANDLE    m_hDevice = INVALID_HANDLE_VALUE;
    QString   m_driverName = QStringLiteral("EFCHKernelDriver");
    QString   m_driverPath;  // sys 文件路径
    DWORD     m_lastError = 0;  // 最近一次 DeviceIoControl 的 GetLastError
};
