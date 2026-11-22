#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QStringList>
// ע��: ʹ�� WIN32_LEAN_AND_MEAN ���� windows.h ���� winsock.h,
// ��????NetTable.h ??winsock2.h ��ͻ (winsock2.h �������� winsock.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>   // CTL_CODE / FILE_DEVICE_UNKNOWN / METHOD_BUFFERED

// PPL/PP ����������
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

// ϵͳ����????(����??IOCTL_DISENA_SYSTEMFUNCTION ??inbuffer ȡ???�ϸ�һ??
// ���� case ����: 0=EnableLoadDriver 1=DisableLoadDriver
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

// HAL ��Դ����
enum HalPowerControlCode {
    HalPower_Halt       = 0,  // ϵͳͣ��(��CPU)
    HalPower_PowerOff   = 1,  // ????
    HalPower_Reboot     = 2,  // ����
    HalPower_ForceReboot= 3,  // ǿ������
    HalPower_Interactive= 4   // ���뽻��ģʽ
};

 // �ļ�/????��Ϣ�ṹ��??Ӧ��???? DATA_INFO_ANSI?
struct DataInfoAnsi {
    QString fileName;
    bool    isDirectory;
};

 // �߳���Ϣ����??IOCTL_ENUMTHREADBYPROCESS ���?
struct ThreadInfo {
    quint32 tid = 0;       // �߳� ID
    quint64 ethread = 0; // �ں� ETHREAD �����ַ?? ��ʾ��??��Ϣ��?? ToolHelp32 ��
};

 // NTFS ????ö�ٽ�����Ӧ���� NTFS_DATA_INFO, 2052 �ֽ�?
#pragma pack(push, 1)
struct NtfsDataInfo {
    wchar_t name[1024];   // �ļ�/??????(UTF-16, null ��β)
    quint32 type; // 0=�ļ� 1=???? 2=ϵͳԪ��??????3=ϵͳԪ��
};
#pragma pack(pop)

 // NTFS ????����ͳ??��??Ӧ��??NTFS_FILE_FOLDER_QUANTITY, 20 �ֽ�?
#pragma pack(push, 1)
struct NtfsFileFolderQuantity {
    quint32 fileCount;          // ????�ļ��� (Type=0)
    quint32 folderCount;        // ????Ŀ¼�� (Type=1)
    quint32 systemFileCount;    // ϵͳԪ��????���� (Type=2)
    quint32 systemFolderCount;  // ϵͳԪ��????¼�� (Type=3)
    quint32 totalCount;         // ����
};
#pragma pack(pop)

// BSOD ��ɫ����
#pragma pack(push, 1)
struct BsodColorConfig {
    quint8 backgroundRed;
    quint8 backgroundGreen;
    quint8 backgroundBlue;
};
#pragma pack(pop)

// ==================== ARK ��չ�ṹ�������� h/Kernel/ArkNeo.h �ϸ���??====================
 // ע�⣺������ʹ��Ĭ??����??�ֽڣ�����??����??�£���???
// （此处不应有任何 pack 指令：驱动侧默认对齐，UI 侧保持一致）

// ---- PspCidTable ----
struct ArkCidProcessEntry {
    quint32 pid;
    quint32 threadCount;
    quint64 eprocess;
    wchar_t imageName[32];
};
struct ArkCidThreadEntry {
    quint32 tid;
    quint32 ownerPid;
    quint64 ethread;
    quint64 startAddress;
    quint32 flags;      // bit0 = ϵͳ�߳�
    quint32 reserved;
};
struct ArkCidProcessArray {
    quint32 count;
    quint32 reserved;
    ArkCidProcessEntry entries[2048];
};
struct ArkCidThreadArray {
    quint32 count;
    quint32 reserved;
    ArkCidThreadEntry entries[8192];
};

// ---- ϵͳ�߳�ö��??x604��CID ȫ�� + PsIsSystemThread + ��������??----
struct ArkSysThreadEntry {
    quint32 tid;
    quint32 ownerPid;
    quint64 ethread;
    quint64 startAddress;
    wchar_t driverPath[128];
};
struct ArkSysThreadArray {
    quint32 count;          // 系统线程数
    quint32 totalThreads;   // CID 表内全部线程数
    ArkSysThreadEntry entries[1024];
};

// ---- ϵͳ�ص�ö��??x605??----
struct ArkCallbackEntry {
    quint32 kind;           // 0=���̴��� 1=�̴߳��� 2=�������
    quint32 reserved;
    quint64 callback;
    wchar_t ownerPath[128];
};
struct ArkCallbackArray {
    quint32 count;
    quint32 reserved;
    ArkCallbackEntry entries[128];
};

// ---- ж��������ʷ??x606��MmUnloadedDrivers??----
struct ArkUnloadedEntry {
    wchar_t name[64];
    quint64 startAddress;
    quint64 endAddress;
};
struct ArkUnloadedArray {
    quint32 count;
    quint32 reserved;
    ArkUnloadedEntry entries[64];
};

// ---- ????��?? ----
struct ArkKbdEvent {
    quint64 sequence;
    quint64 timestamp;      // 100ns
    quint16 makeCode;
    quint16 flags;          // 0=���� 1=???? 2=E0 4=E1
    quint32 reserved;
};
struct ArkKbdLog {
    quint32 count;
    quint32 reserved;
    quint64 nextSeq;
    ArkKbdEvent events[512];
};
struct ArkKbdStatus {
    quint32 active;
    quint32 filterCount;
    quint32 droppedEvents;
    quint32 reserved;
};

// ---- ע��????ö�� (0x615) ----
struct ArkHotkeyEntry {
    quint32 pid; // ע����̣�����ʧ��Ϊ 0?
quint32 id;             // RegisterHotKey ??id
    quint32 fsModifiers;    // MOD_ALT(1)|MOD_CONTROL(2)|MOD_SHIFT(4)|MOD_WIN(8)|MOD_NOREPEAT(0x4000)
    quint32 vk;             // ����????
    wchar_t imageName[32]; // ע�����ӳ��?
quint64 threadInfo;     // win32k THREADINFO ��ַ
};
struct ArkHotkeyArray {
    quint32 count;
    quint32 reserved;
    ArkHotkeyEntry entries[1024];
};
 // ????/ɾ��????���루�� Pid+Id+Mods+Vk ��λ��Pid=0 ��ʾ�������̹���?
struct ArkHotkeyModInput {
    quint32 pid;
    quint32 id;
    quint32 fsModifiers;
    quint32 vk;
    quint32 newFsModifiers;
    quint32 newVk;
};
 // ������Ϊ��??���루NtdllBase=�û�??ntdll ģ���ַ, ÿ????��ͳ??���?
struct ArkHardenPid {
    quint32 pid;
    quint32 flags;          // ����: ??=�����ɹ� ??=���̹�����λ
    quint64 ntdllBase;
};

// ---- ������Ϊ��?? (0x620-0x62F) ----
enum BehaviorKind {
    BH_ProcCreate   = 0,
    BH_ProcExit     = 1,
    BH_ThreadCreate = 2,
    BH_ThreadExit   = 3,
    BH_ImageLoad    = 4,
    BH_Registry     = 5,
    BH_File         = 6,
    BH_DiskWrite    = 7,
    BH_HandleOpen   = 8,
    BH_DiskRead     = 9,
    BH_NetR3        = 100   // R3 ����???�¼����� UI ????��������????
};
// ���??��λ���루������ ArkNeo.h ??�£�
enum BehaviorMask {
    BM_Proc     = 0x0001,
    BM_Image    = 0x0002,
    BM_Registry = 0x0004,
    BM_File     = 0x0008,
    BM_Disk     = 0x0010,
    BM_Handle   = 0x0020,
    BM_All      = 0x003F
};
 // Op �Ӳ����루�� Kind ���Գ���?
enum BehaviorOp {
    BOP_RegCreateKey   = 1,
    BOP_RegDeleteKey   = 2,
    BOP_RegRenameKey   = 3,
    BOP_RegSetValue    = 4,
    BOP_RegDeleteValue = 5,
    BOP_RegLoadKey     = 6,
    BOP_RegReplaceKey  = 7,
    BOP_RegRestoreKey  = 8,
    BOP_RegOpenKey     = 9,
    BOP_RegQueryValue  = 10,
    BOP_RegEnumValue   = 11,
    BOP_FileCreate     = 1,
    BOP_FileDelete     = 2,
    BOP_FileRename     = 3,
    BOP_FileWrite      = 4,
    BOP_FileLink       = 5,
    BOP_FileRead       = 6,
    BOP_HandleCreate   = 1,
    BOP_HandleDuplicate= 2,
    BOP_HandleThreadCreate = 3,
    BOP_HandleThreadDup    = 4
};
struct ArkBehaviorEvent {
    quint64 sequence;
    quint64 timestamp;          // 100ns
    quint32 kind;               // BehaviorKind
    quint32 op;                 // �Ӳ�����
    quint32 pid;
    quint32 parentPid;          // ���̴���=��PID
    quint32 tid;                // �߳��¼�=TID
    quint32 detail; // ����??????��״?? ����??�ֽ�?
quint32 reserved;
    wchar_t image[260];         // ӳ��??ע���·??�ļ�????/����
};
struct ArkBehaviorLog {
    quint32 count;
    quint32 dropped;
    quint64 nextSeq;
    ArkBehaviorEvent events[512];
};

// ---- �����ռ� ----
struct ArkNsEntry {
    wchar_t name[64];
    wchar_t typeName[24];
    quint64 object;
};
struct ArkNsArray {
    quint32 count;
    quint32 reserved;
    ArkNsEntry entries[1024];
};
struct ArkNsDeleteInput {
    wchar_t typeName[24];
    wchar_t path[520];
};
struct ArkNsObjectInfo {
    wchar_t typeName[24];
    quint64 object;
    qint32  pointerCount;
    qint32  handleCount;
    quint32 reserved;
};

// ---- ע���ȫ�ں˲��� (0x710-0x717) ----
// ����??????UI ��ʽ��HKEY_LOCAL_MACHINE\SOFTWARE\...�����ں˷���??\Registry\...
struct ArkRegSubKey {
    wchar_t name[256];
    quint32 subKeyCount;    // 子键数量（0 = 叶子键）
    quint32 valueCount;
};
struct ArkRegEnumKeys {
    wchar_t keyPath[384];
    quint32 count;
    quint32 reserved;
    ArkRegSubKey subKeys[2048];
};
struct ArkRegValue {
    wchar_t name[256];
    quint32 type;           // REG_SZ / REG_DWORD / ...
    quint32 dataLength;     // 实际数据长度
    quint32 truncated;      // 1 = 数据长度超限被截断
    uchar   data[1024];
};
struct ArkRegEnumValues {
    wchar_t keyPath[384];
    quint32 count;
    quint32 reserved;
    ArkRegValue values[512];
};
struct ArkRegValueNameInput {
    wchar_t keyPath[384];
    wchar_t valueName[256];
};
struct ArkRegSetValueInput {
    wchar_t keyPath[384];
    wchar_t valueName[256];
    quint32 type;
    quint32 dataLength;
    uchar   data[1024];
};
struct ArkRegRenameKeyInput {
    wchar_t keyPath[384];
    wchar_t newName[256];
};
struct ArkRegRenameValueInput {
    wchar_t keyPath[384];
    wchar_t oldName[256];
    wchar_t newName[256];
};

// ---- ���̾�� ----
struct ArkHandleEnumInput {
    quint32 pid;
    quint32 startIndex;
};
struct ArkHandleEntry {
    quint64 handle;
    quint32 grantedAccess;
    quint32 reserved;
    quint64 object;
    qint32  pointerCount;
    qint32  objectHandleCount;
    wchar_t typeName[20];
    wchar_t name[256];
};
struct ArkHandleArray {
    quint32 count;
    quint32 nextIndex;      // 0=��ɣ�������Ϊ��??startIndex
    ArkHandleEntry entries[512];
};
struct ArkCloseHandleInput {
    quint32 pid;
    quint32 pad;
    quint64 handle;
};

// ---- �����ڴ� ----
struct ArkRwMemoryInput {
    quint32 pid;
    quint32 pad;
    quint64 address;
    quint32 size;
    quint32 pad2;
 // д��ʱ�����
};
struct ArkQueryVmInput {
    quint32 pid;
    quint32 pad;
    quint64 address;
};
struct ArkVmInfo {
    quint64 baseAddress;
    quint64 regionSize;
    quint32 state;      // MEM_COMMIT / MEM_FREE / MEM_RESERVE
    quint32 protect;    // PAGE_*
    quint32 type;       // MEM_IMAGE / MEM_MAPPED / MEM_PRIVATE
    quint32 reserved;
};

// ---- ����??/ �׺�??/ ���� ----
struct ArkPidInput {
    quint32 pid;
    quint32 pad;
};
struct ArkPriorityInfo {
    quint32 priorityClass;
    quint32 reserved;
};
struct ArkSetPriorityInput {
    quint32 pid;
    quint32 pad;
    quint32 priorityClass;
};
struct ArkAffinityInfo {
    quint64 affinity;
    quint64 maxAffinity;
};
struct ArkSetAffinityInput {
    quint32 pid;
    quint32 pad;
    quint64 affinity;
};
struct ArkPrivilege {
    quint64 luid;
    quint32 attributes;     // 0=���� 2=????
    quint32 reserved;
};
struct ArkPrivilegeArray {
    quint32 count;
    quint32 reserved;
    ArkPrivilege privileges[64];
};
struct ArkSetPrivInput {
    quint32 pid;
    quint32 pad;
    quint64 luid;
    quint32 enable;         // 2=???? 0=����
    quint32 pad2;
};
struct ArkTokenInfo {
    quint32 sessionId;
    quint32 elevationType;  // 1=Default 2=Limited 3=Elevated
    quint32 isElevated;
    quint32 integrityLevel;
    quint32 groupCount;
    quint32 reserved;
    wchar_t userSid[128];
};

// ---- �ļ�ռ��???? ----
struct ArkOccupyEntry {
    quint32 pid;
    quint32 grantedAccess;
    quint64 handle;
    quint64 object;
};
struct ArkOccupyArray {
    quint32 count;
    quint32 totalFound;
    quint32 reserved;
    quint32 reserved2;
    ArkOccupyEntry entries[2048];
};

// ---- �������� / ģ�� / ��?? ----
struct ArkDriverObjEntry {
    wchar_t name[64];
    quint64 driverObject;
    quint64 imageBase;
    quint32 imageSize;
    quint32 reserved;
};
struct ArkDriverObjArray {
    quint32 count;
    quint32 reserved;
    ArkDriverObjEntry entries[1024];
};
struct ArkDriverDetail {
    wchar_t name[64];
    quint64 driverObject;
    quint32 flags;
    quint32 reserved;
    quint64 driverInit;
    quint64 driverStartIo;
    quint64 driverUnload;
    quint64 imageBase;
    quint32 imageSize;
    quint32 reserved2;
    quint64 majorFunction[28];
    wchar_t imagePath[260];
};
struct ArkModuleEntry {
    wchar_t path[260];
    quint64 base;
    quint64 size;
};
struct ArkModuleArray {
    quint32 count;
    quint32 totalFound;
    ArkModuleEntry entries[512];
};
struct ArkDeviceEntry {
    quint64 deviceObject;
    quint64 driverObject;
    wchar_t driverName[64];
    quint32 deviceType;
    quint32 characteristics;
    quint32 flags;
    quint32 stackSize;
    quint64 attachedDevice;
    quint64 deviceExtension;
};
struct ArkDeviceArray {
    quint32 count;
    quint32 totalFound;
    ArkDeviceEntry entries[1024];
};
struct ArkDeviceDetail {
    quint64 deviceObject;
    quint64 driverObject;
    wchar_t driverName[64];
    wchar_t deviceName[520];
    quint32 deviceType;
    quint32 characteristics;
    quint32 flags;
    quint32 stackSize;
    quint64 attachedDevice;
    quint64 deviceExtension;
};

// ---- SSDT / IDT / �������� ----
struct ArkSsdtInfo {
    quint32 count;
    quint32 reserved;
    quint64 serviceBase;
    quint64 entries[640];
};
struct ArkIdtCpu {
    quint64 base;
    quint32 limit;
    quint32 reserved;
    quint64 handlers[256];
};
struct ArkIdtInfo {
    quint32 cpuCount;
    quint32 reserved;
    ArkIdtCpu cpus[8];
};
struct ArkObjTypeEntry {
    wchar_t name[32];
    quint32 index;
    quint32 totalObjects;
    quint32 totalHandles;
    quint32 reserved;
};
struct ArkObjTypeArray {
    quint32 count;
    quint32 reserved;
    ArkObjTypeEntry entries[96];
};

 // ע�����????�ں�ģʽ?????
namespace RegRoot {
    const QString HKEY_CLASSES_ROOT_KERNELMODE   = QStringLiteral("\\Registry\\Machine\\SOFTWARE\\Classes\\");
    const QString HKEY_LOCAL_MACHINE_KERNELMODE = QStringLiteral("\\Registry\\Machine\\");
    const QString HKEY_USERS_KERNELMODE         = QStringLiteral("\\Registry\\User\\");
    const QString HKEY_CURRENT_USER_KERNELMODE  = QStringLiteral("\\Registry\\User\\<CurrentUserSID>\\");
}

// ������??�࣬��Ӧ��??????EFCHKMD ??// ���� IOCTL ����ʹ�� CTL_CODE(FILE_DEVICE_UNKNOWN=0x22=34, Function, METHOD_BUFFERED, FILE_ANY_ACCESS)
class KernelDriver
{
public:
    KernelDriver();
    ~KernelDriver();

    // ===== ��������/ж�� =====
    // ��??��������????����????�ɹ���sysFilePath ??.sys �ļ�����????
    bool installDriver(const QString& sysFilePath, QString* errMsg = nullptr);
    // ж������
    bool uninstallDriver();
 // ����????�Ѽ�?
bool isLoaded() const { return m_hDevice != INVALID_HANDLE_VALUE; }
    // ̽��ϵͳ????����????����פ��ʵ������??��??��������????����������
    // ������Դ��????sys ��һ��ʱ����????��ж��פ�������ٸ���
    bool probeResidentDriver();
    // ��ȡ��??���
    HANDLE deviceHandle() const { return m_hDevice; }
 // ��������?
QString driverName() const { return m_driverName; }

    // ===== ���̲��� =====
    bool zwKillProcess(quint32 pid);                    // ZwTerminateProcess
    bool apcTerminateProcess(quint32 pid);              // APC ��������
    bool suspendProcess(quint32 pid);                   // PsSuspendProcess
    bool resumeProcess(quint32 pid);                    // PsResumeProcess
    bool hideProcess(quint32 pid);                      // 隐藏进程（摘链）
    bool unHideProcess(quint32 pid);                    // ȡ�����ؽ���
    bool protectProcess(quint32 pid);                   // ��������(ObRegisterCallbacks)
    bool unProtectProcess(quint32 pid);                 // ȡ����������
    bool forceProtectProcess(quint32 pid);              // ǿ����������
    bool unForceProtectProcess(quint32 pid);            // ȡ��ǿ������
    bool memKillProcess(quint32 pid);                   // �ƻ������ڴ�����
    bool setPPL(quint32 pid, quint32 protectType);      // ���� PPL/PP ����
    bool setPID(quint64 pid, quint64 newPid);           // �������ý��� PID
    bool setProcessCritical(quint64 pid, qint32 isCritical); // ����Ϊϵͳ��?????
bool pspTerminateProcess(quint32 pid);              // PspTerminateProcess

    // 枚举进程（通过 EPROCESS 链表遍历，无 PID 参数过滤）
    QVector<quint32> enumProcesses();
    // ��ȡ���� PPL��ʧ�ܷ�??-1
    qint32 getPPL(quint32 pid);
    // ��ȡ���� DosImageName
    QString getProcessDosImageName(quint32 pid);
    // 枚举进程线程（通过 EPROCESS 内核 ThreadListEntry，非 PspCidTable 枚举线程）
    // 返回 ThreadInfo 列表（含 TID 与对应 ETHREAD 地址），失败返回空列表
    QVector<ThreadInfo> enumThreadsByProcess(quint32 pid);
    // ͨ�� ETHREAD �ں˵�ַ��ȡ�߳�??������������????��IOCTL_GETDRIVERPATCHBYETHREAD??    // ʧ�ܷ��ؿ��ַ���
    QString getDriverPathByEThread(quint64 ethread);

    // ===== �̲߳��� =====
    bool suspendThread(quint32 tid);                    // NtSuspendThread
    bool resumeThread(quint32 tid);                     // NtResumeThread
    bool terminateThread(quint32 tid);                  // NtTerminateThread
    bool pspTerminateThreadByPointerEasy(quint32 tid); // ??����??����?
bool pspTerminateThreadByPointer(quint64 tid, qint32 exitCode, bool directTerminate);
    bool apcTerminateThread(quint32 tid);
 // ͨ�� ETHREAD �ں˵�ַ APC ��ʽ??���̣߳�IOCTL_APCTERMINATETHREADBYETHREAD?
bool apcTerminateThreadByEThread(quint64 ethread);

    // ===== �ļ����� =====
    bool zwDeleteFile(const QString& ntFilePath);       // ?????? \??\C:\xxx
    bool occupyFile(const QString& ntFilePath);         // ռ���ļ�
    bool forceDeleteFile(const QString& filePath);      // IRP ����ɾ��
    bool forceOccupyFile(const QString& filePath);      // IRP ����ռ��
    bool forceWriteFile(const QString& filePath, const QByteArray& data); // IRP ����д��?
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
    qint64 irpGetFileSize(const QString& filePath);     // ʧ�ܷ��� -1
    bool irpReadDisk(quint64 startSector, quint64 sectorNumber, QByteArray& outData);
    bool irpQueryFileAndFolders(const QString& filePath, QVector<DataInfoAnsi>& outList);
    qint32 irpGetFileAndFolderQuantity(const QString& filePath);
    bool flushMFT(const QString& driveLetter);

    // ===== DirectNTFS 区块 (直接解析 NTFS, 绕过文件系统, 不依赖挂载) =====
    // ��??��ָ���̷��� NTFS �������ġ�driveLetter ??'C'
    bool ntfsInitVolume(wchar_t driveLetter);
    // ��ȡ�ļ���??��filePath Ϊ��Ծ�����??????"\Windows\System32\drivers\etc\hosts"
    // ??�ȵ�??ntfsInitVolume ��??��??Ӧ��
    bool ntfsReadFile(const QString& filePath, QByteArray& outData);
    // ö��????��dirPath Ϊ��Ծ���·��?? "\Windows\System32"����??????"\"
    bool ntfsEnumDirectory(const QString& dirPath, QVector<NtfsDataInfo>& outList);
    // ͳ??????����??�ļ�������???dirPath ͬ��
    bool ntfsCountDirectory(const QString& dirPath, NtfsFileFolderQuantity& outQty);
    // ͨ��ֱ�ӽ��� NTFS ɾ���ļ���Ŀ??(�ƹ��ļ�ϵͳ, ????ntfsInitVolume)
    // filePath Ϊ����·��?? "C:\Windows\System32\drivers\etc\hosts"
    bool ntfsDeleteFile(const QString& filePath);

    // ===== ���̲��� =====
    bool writeDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data);
    bool forceWriteDisk(quint64 startSector, quint64 sectorNumber, const QByteArray& data);
    bool storDirectReadDisk(quint64 startSector, quint64 sectorNumber, qint16 raidPort, QByteArray& outData);
    bool storDirectWriteDisk(quint64 startSector, quint64 sectorNumber, qint16 raidPort, const QByteArray& data);
    qint16 getVolumeRaidPort(const QString& volumeLetter); // ʧ�ܷ��� -1

    // ===== ϵͳ���� =====
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

    // ???? I/O
    bool sendDataToPort(quint16 port, quint16 type, const QByteArray& data);
    QByteArray readDataInPort(quint16 port, quint16 type); // type: 1=UCHAR 2=USHORT 4=ULONG
    bool portPowerOff();           // ??0x64 ??????0xFE ????
    qint32 portReadCMOS(qint32 address);   // ʧ�ܷ��� -1
    bool portWriteCMOS(qint32 address, qint32 data);
    bool portKillBIOSPassword();
    void portDisEnaKeyboard(bool enable);
    void portDisEnaPS2Mouse(bool enable);

    // ����/��??ö��
    QStringList enumDrivers();
    QStringList enumDevices();
    QStringList enumDriversPath();

 // ϵͳ����???
bool disableLoadDriver(bool disable);
    bool disableCreateProcess(bool disable);
    bool disableRegisterControl(bool disable);
    bool disEnaSystemFunction(SystemFunctionOp op);

    // ===== ע�����??=====
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

    // ===== Hook ���� =====
    bool hookAntiTerminateProcess(quint32 pid);
    bool hookUnAntiTerminateProcess(quint32 pid);
    bool hookProtectThread(quint32 tid);
    bool hookUnProtectThread(quint32 tid);
    bool hookDisableBSOD(qint32 operation); // 0=ȡ����??���� 1=��??����
    bool hookQueryStatus(qint32 operation); // 1=DisableBSOD 2=DisableLoadDriver
    bool hookDisableNtDeviceIoControlFile();

    // ===== ARK: PspCidTable (0x600-0x60F) =====
 // ͨ�� PspCidTable ö�ٽ��̣��ɷ���????/��PID���ؽ���?
bool cidEnumProcesses(ArkCidProcessArray& out);
 // ͨ�� PspCidTable ö��ȫ���̣߳�ownerPid!=0 ʱֻ���ظý�����?
bool cidEnumThreads(ArkCidThreadArray& out, quint32 ownerPid = 0);
    // APC ��ʽ��??�߳�/����
    bool cidTerminateThread(quint32 tid);
    bool cidTerminateProcess(quint32 pid);
 // CID ȫ�� + PsIsSystemThread �ж� + �߳���??��ַ??����??????�η�?
bool cidEnumSystemThreads(ArkSysThreadArray& out);
 // ϵͳ�ص�ö�٣���??�߳�/�������ע��ص� + ģ�����?
bool arkEnumSysCallbacks(ArkCallbackArray& out);
 // ж��������ʷ��MmUnloadedDrivers?
bool arkEnumUnloadedDrivers(ArkUnloadedArray& out);

    // ===== ARK: ????��?? (0x610-0x61F) =====
    bool kbdStart();                                    // ���� kbdclass ������??
    bool kbdStop();                                     // ж�ع�����??
    bool kbdReadLog(quint64 lastSeq, ArkKbdLog& out);   // ������ȡ������־
    bool kbdClearLog();
    bool kbdGetStatus(ArkKbdStatus& out);
    // ö�ٵ�ǰ�Ự??�н���???�� RegisterHotKey ע�����????????����̣�
    bool kbdEnumHotkeys(ArkHotkeyArray& out);
 // ????/ɾ��??����ע��????�������� win32k ????����ԭ�ظ�д/ժ��?
bool kbdModifyHotkey(quint32 pid, quint32 id, quint32 fsModifiers, quint32 vk,
                         quint32 newFsModifiers, quint32 newVk);
    bool kbdDeleteHotkey(quint32 pid, quint32 id, quint32 fsModifiers, quint32 vk);

    // ===== ARK: ������Ϊ��?? (0x620-0x62F) =====
 // ������ȡ��Ϊ�¼���lastSeq ���ϴη��ص� nextSeq��??�δ� 0?
bool behaviorReadLog(quint64 lastSeq, ArkBehaviorLog& out);
    bool behaviorClearLog();
    // ���??�أ�BehaviorMask λ��ϣ�
    bool behaviorSetFilter(quint32 mask);
    // �ļ�/���̼�??������������/ж��
    bool filemonStart();
    bool filemonStop();
    // ������Ϊ��??���ѽ��̼���/�Ƴ��ں� Harden ����
    // (NtRaiseHardError ���� + 0-2047 ����/SCSI/ATA ֱ???��??+ ��??���� .sys)
    // flags ����: ??=�����ɹ� ??=���̹�����λ
    bool hardenAddPid(quint32 pid, quint64 ntdllBase, quint32* flags = nullptr);
    bool hardenRemovePid(quint32 pid);

    // ===== ARK: �����ռ� (0x700-0x70F) =====
    // ö�ٶ���????��path ??L"\\", L"\\BaseNamedObjects", L"\\Device"
    bool nsEnumDir(const QString& path, ArkNsArray& out);
    // ɾ�������ռ����Event/Mutant/Semaphore/SymbolicLink �ȣ�
    bool nsDeleteObject(const QString& typeName, const QString& fullPath);
    // ��??�����ռ��������
    bool nsQueryPath(const QString& path, ArkNsObjectInfo& out);

    // ===== ARK: ע���ȫ�ں˲��� (0x710-0x71F) =====
    // ȫ��??System ����������ִ�У�??????HKLM �ȼ�����??ACL ����??    // ������ʱ UI ���?? QSettings �û���ʵ�֣�RegeditPage ��??����
    bool regEnumSubKeys(const QString& uiPath, ArkRegEnumKeys& out);
    bool regEnumValues(const QString& uiPath, ArkRegEnumValues& out);
    bool regCreateKey(const QString& uiPath);
    bool regDeleteKey(const QString& uiPath);            // �ݹ�ɾ��
    bool regDeleteValue(const QString& uiPath, const QString& valueName);
    bool regSetValue(const QString& uiPath, const QString& valueName,
                     quint32 type, const QByteArray& data);  // data <= 1024 �ֽ�
    bool regRenameKey(const QString& uiPath, const QString& newName);
    bool regRenameValue(const QString& uiPath, const QString& oldName,
                        const QString& newName);

    // ===== ARK: ������չ (0x813-0x85F) =====
    // ö�ٽ��̾��????��ҳ��������????�ɹ���out.nextIndex!=0 ʱ���������
    bool enumProcessHandles(quint32 pid, quint32 startIndex, ArkHandleArray& out);
 // �ر�ָ�����̵�ָ����?
bool closeProcessHandle(quint32 pid, quint64 handle);
 // �����ڴ��д��size <= 64KB?
bool arkReadProcessMemory(quint32 pid, quint64 address, quint32 size, QByteArray& outData);
    bool arkWriteProcessMemory(quint32 pid, quint64 address, const QByteArray& data);
 // ��??��ַ??���ڴ�������?
bool arkQueryVirtualMemory(quint32 pid, quint64 address, ArkVmInfo& out);
 // ����?
bool arkGetPriority(quint32 pid, ArkPriorityInfo& out);
    bool arkSetPriority(quint32 pid, quint32 priorityClass);
 // CPU �׺�?
bool arkGetAffinity(quint32 pid, ArkAffinityInfo& out);
    bool arkSetAffinity(quint32 pid, quint64 affinity);
    // ������Ȩ
    bool arkGetTokenPrivileges(quint32 pid, ArkPrivilegeArray& out);
    bool arkSetTokenPrivilege(quint32 pid, quint64 luid, quint32 enable);
    // ������Ϣ
    bool arkGetTokenInfo(quint32 pid, ArkTokenInfo& out);

    // ===== ARK: �ļ�ռ��???? (0x928) =====
    // ntPath ??NT �ں�????��?? L"\\??\\C:\\Windows\\System32\\ntdll.dll"
    bool scanFileOccupy(const QString& ntPath, ArkOccupyArray& out);

    // ===== ARK: �ں���ͼ (0xB14-0xB1F) =====
    bool arkEnumDriverObjects(ArkDriverObjArray& out);
    bool arkGetDriverDetail(quint64 driverObject, ArkDriverDetail& out);
    bool arkEnumKernelModules(ArkModuleArray& out);
    bool arkEnumDeviceObjects(ArkDeviceArray& out);
    bool arkGetDeviceDetail(quint64 deviceObject, ArkDeviceDetail& out);
    bool arkQuerySSDT(ArkSsdtInfo& out);
    bool arkQueryIDT(ArkIdtInfo& out);
    bool arkEnumObjectTypes(ArkObjTypeArray& out);

    // ===== ͨ�� IOCTL ���� =====
    bool deviceIoControl(quint32 function, LPVOID inBuffer, DWORD inSize,
                         LPVOID outBuffer, DWORD outSize, LPDWORD bytesReturned = nullptr);

    // ===== Hypervisor (Intel VT-x / AMD SVM, 0xB06-0xB08) =====
 // ����??h\Kernel\ArkHv.h ??HV_QUERY_INFO ����???
struct HvQueryInfo {
        quint32 structSize = 0;
        quint32 supported = 0;
        quint32 cpuCount = 0;
        quint32 activeCpus = 0;
        quint32 inHypervisorEnv = 0;
        quint32 options = 0;
        quint32 vendor = 0;          // 0=???? 1=Intel 2=AMD
        quint64 totalExits = 0;
        quint64 exitCount[64] = { 0 };
        quint64 msrVmxBasic = 0;
        quint64 msrVmxPin = 0;
        quint64 msrVmxProc = 0;
        quint64 msrVmxProc2 = 0;
        quint64 msrVmxExit = 0;
        quint64 msrVmxEntry = 0;
        quint64 msrEptVpidCap = 0;
        quint64 msrFeatureControl = 0;
        char cpuidVendor[13] = { 0 };
        quint32 failStage[64] = { 0 };   // 0=�ɹ� 1=vmxon 2=vmclear/vmptrld 3=vmlaunch 4=CET
        quint32 failCode[64] = { 0 };
        quint32 lastStartStatus = 0;     // Last start NTSTATUS (raw)
        quint32 lastStartPartial = 0;    // CPUs that failed in a partial start
        quint32 vmInstrError[64] = { 0 };  // VMfailValid: 6=invalid control fields 7=invalid host state
        quint32 vmWriteBadField[64] = { 0 }; // first failed __vmx_vmwrite field encoding
    };
 // Ring-1 �ڴ��д??x628/0x629��ҳ����??+ ����ӳ�䣻pid=0 ??address Ϊ������???
struct HvMemAccess {
        quint32 pid = 0;
        quint32 size = 0;
        quint64 address = 0;
    };
    bool hvQuery(HvQueryInfo& out);
    bool hvStart(quint32 options);
    bool hvStop();
    bool hvReadMem(quint32 pid, quint64 address, QByteArray& outData);
    bool hvWriteMem(quint32 pid, quint64 address, const QByteArray& data);

    // ===== Hypervisor R-1 进程处置 (0x62A) =====
    // 按 CR3 定位目标地址空间，EPT 拒绝执行关键页：冻结注入 #PF（可撤销），
    // 强杀注入 #UD（由来宾自己走进程退出流程，对驱动级保护进程有效）。
    // 线性地址给 0 时由驱动自动选页（目标线程当前 RIP 页优先，主映像入口页兜底）。
    // 处置安装要求常驻停止，驱动内部自动完成 停止→处置→恢复。
    enum HvProcOp : quint32 {
        HvProcOpQuery = 0,       // 查询处置表
        HvProcOpFreeze = 1,      // 冻结
        HvProcOpTerminate = 2,   // 强杀
        HvProcOpRelease = 3,     // 撤销指定 PID
        HvProcOpReleaseAll = 4,  // 撤销全部
    };
    struct HvProcessRow {
        quint32 pid = 0;
        quint32 disposition = 0;      // 1=冻结 2=强杀 3=已解除
        quint64 directoryBase = 0;    // 目标地址空间 CR3 页帧
        quint64 guestPhysicalAddress = 0; // 被拒绝执行的物理页
        quint64 guestLinearAddress = 0;   // 下达时给的线性地址
        quint64 interceptCount = 0;   // 累计拦截次数
        quint32 hierarchyIndex = 0;   // 占用的受限层次序号
    };
    struct HvProcessResult {
        quint32 status = 0;           // 0=OK 其余见驱动侧 EFCH_ARK_HVM_PROCESS_STATUS_*
        quint32 rowCount = 0;
        quint32 returnedRows = 0;
        quint32 residentActive = 0;   // 操作时常驻是否激活
        qint32 lastStatus = 0;        // 底层 NTSTATUS
        quint64 stateFlags = 0;
        QVector<HvProcessRow> rows;
    };
    bool hvmProcessControl(quint32 operation, quint32 pid, quint64 linearAddress,
                           HvProcessResult& out);
    bool hvmFreezeProcess(quint32 pid, quint64 linearAddress = 0, HvProcessResult* out = nullptr);
    bool hvmTerminateProcess(quint32 pid, quint64 linearAddress = 0, HvProcessResult* out = nullptr);
    bool hvmResumeProcess(quint32 pid, HvProcessResult* out = nullptr);

    // 一键 Parity Error (0x62B)：驱动主动制造三重故障，CPU 立即复位。
    // 调用前必须二次确认——这条路不会返回，虚拟机直接断电重启进 BIOS。
    bool hvParityNow();

    // ??��һ??IOCTL ����??Win32 ��??�루GetLastError??    // ������� Storport ��??дʧ�ܵ�ԭ��
    DWORD lastIoError() const { return m_lastError; }

    // ��??�Ѽ��������İ汾�ţ�IOCTL_DRV_GET_INFO??    // ����ʶ����??���������ľɰ���????ʧ�ܷ��� 0
    quint32 getDriverVersion();

    // ������������????����??hello.cpp ??EFCHKMD_DRV_VERSION ??�£�
 // �����������ش��޸�ʱ��??��ȷ������������????����װ???��????
static constexpr quint32 kExpectedDriverVersion = 20261036;

private:
    // ���� IOCTL ����: CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS)
    static DWORD ioctlCode(quint32 function) {
        return CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS);
    }

 // ����ͨ����������ȡ�Ѽ���������??����?
HANDLE getDriveHandle(const QString& driverName);
    // ͨ�� NtLoadDriver ������������
    HANDLE loadDrive(const QString& path, const QString& serviceName);
 // ͨ�� SCM �����������񣨻�??��???
HANDLE loadDriveViaSCM(const QString& path, const QString& serviceName);
    // ж����������
    bool uninstallDrive(const QString& serviceName, bool stopFirst);

 // ANSI ��??��ת Unicode �ֽڼ���????��?? \0\0?
static QByteArray ansiToUnicodeBytes(const QString& s);
 // Unicode �ֽڼ�ת ANSI ��???
static QString unicodeBytesToAnsi(const QByteArray& bytes);

    HANDLE    m_hDevice = INVALID_HANDLE_VALUE;
    QString   m_driverName = QStringLiteral("EFCHKernelDriver");
    QString   m_driverPath;  // sys �ļ�????
    DWORD     m_lastError = 0;  // ??��һ??DeviceIoControl ??GetLastError
};
