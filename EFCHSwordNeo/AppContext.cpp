#include "AppContext.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QMessageBox>
#include <QDebug>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QSettings>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

// 枚举指定进程的所有线程 ID（用于 FakeMode 线程保护）
// 优先使用驱动 IOCTL_ENUMTHREADBYPROCESS，驱动不可用时回退到 ToolHelp32
static QVector<DWORD> EnumProcessThreads(DWORD pid)
{
    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        QVector<ThreadInfo> threads = ctx.driver().enumThreadsByProcess(pid);
        if (!threads.isEmpty()) {
            QVector<DWORD> result;
            result.reserve(threads.size());
            for (const auto& ti : threads) result.append(ti.tid);
            return result;
        }
    }

    QVector<DWORD> tids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return tids;
    THREADENTRY32 te = {0};
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

AppContext& AppContext::instance()
{
    static AppContext inst;
    return inst;
}

AppContext::AppContext() : QObject(nullptr) {}

AppContext::~AppContext()
{
    cleanup();
}

bool AppContext::enablePrivileges()
{
    // 对应易语言: RtlAdjustPrivilege 提权
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    const wchar_t* privileges[] = {
        SE_DEBUG_NAME, SE_BACKUP_NAME, SE_RESTORE_NAME,
        SE_TAKE_OWNERSHIP_NAME, SE_LOAD_DRIVER_NAME,
        SE_SECURITY_NAME, SE_MANAGE_VOLUME_NAME
    };

    bool allOk = true;
    for (const wchar_t* priv : privileges) {
        LUID luid;
        if (!LookupPrivilegeValueW(nullptr, priv, &luid)) {
            allOk = false;
            continue;
        }
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(hToken);
    return allOk;
}

void AppContext::detectWindowsVersion()
{
    // 对应易语言: RtlGetNtVersionNumbers + GetWinVer_Auto_Text
    typedef void (WINAPI* RtlGetNtVersionNumbers_t)(DWORD*, DWORD*, DWORD*);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pRtlGetNtVersionNumbers = reinterpret_cast<RtlGetNtVersionNumbers_t>(
            GetProcAddress(hNtdll, "RtlGetNtVersionNumbers"));
        if (pRtlGetNtVersionNumbers) {
            DWORD major, minor, build;
            pRtlGetNtVersionNumbers(&major, &minor, &build);
            m_winMajor = major;
            m_winMinor = minor;
            m_winBuild = build & 0xFFFF;
        }
    }

    // 生成 Windows 名称
    if (m_winMajor == 10) {
        if (m_winBuild >= 22000) m_winName = QStringLiteral("Windows 11");
        else m_winName = QStringLiteral("Windows 10");
    } else if (m_winMajor == 6) {
        switch (m_winMinor) {
            case 3: m_winName = QStringLiteral("Windows 8.1"); break;
            case 2: m_winName = QStringLiteral("Windows 8"); break;
            case 1: m_winName = QStringLiteral("Windows 7"); break;
            case 0: m_winName = QStringLiteral("Windows Vista"); break;
            default: m_winName = QStringLiteral("Windows NT"); break;
        }
    } else {
        m_winName = QStringLiteral("Windows NT");
    }

    // 系统盘符
    wchar_t sysDir[MAX_PATH] = {0};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    if (sysDir[0]) {
        m_systemPartition = QChar((char)sysDir[0]);
    }
}

bool AppContext::loadDriverFromResource()
{
    // 加载策略（陈旧驱动防御）:
    // 1. 若设备已打开 (本进程已加载过) -> 直接复用
    // 2. 读取资源内驱动并计算 SHA256, 与硬盘 C:\EFCHKernelDriver.sys 比对
    // 3. 磁盘与资源【不一致】时: 若系统中有驻留驱动实例 (即使版本号相同,
    //    也可能是"版本号没变但代码已更新"的旧构建) -> 先无条件卸载驻留实例,
    //    释放对 sys 的占用, 然后覆盖部署新文件再加载。
    //    这修复了旧问题: 版本号相同时直接复用内核里的旧驱动,
    //    导致已修复的缺陷 (SSDT 1168 / 命名空间 0 对象等) 依旧复现。
    // 4. 磁盘与资源一致时: 直接加载 (installDriver 内部还有版本校验,
    //    版本不符会卸载旧驱动并重试)。
    QString errMsg;
    if (m_driver.isLoaded()) {
        qInfo() << "Driver device handle already opened, reuse it";
        return true;
    }

    const QString destPath = QStringLiteral("C:\\EFCHKernelDriver.sys");
    const QString resPath = QStringLiteral(":/drivers/EFCHKernelDriver.sys");

    // 读取资源中的驱动
    QFile resFile(resPath);
    if (!resFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open driver resource";
        m_lastError = QStringLiteral("无法打开驱动资源");
        return false;
    }
    QByteArray resData = resFile.readAll();
    resFile.close();

    if (resData.isEmpty()) {
        m_lastError = QStringLiteral("驱动资源为空");
        return false;
    }

    QByteArray resHash = QCryptographicHash::hash(resData, QCryptographicHash::Sha256);

    // 比对硬盘上的 sys 文件
    bool diskMatches = false;
    if (QFile::exists(destPath)) {
        QFile destFile(destPath);
        if (destFile.open(QIODevice::ReadOnly)) {
            QByteArray destData = destFile.readAll();
            destFile.close();
            if (!destData.isEmpty()) {
                QByteArray destHash = QCryptographicHash::hash(destData, QCryptographicHash::Sha256);
                diskMatches = (destHash == resHash);
            }
        }
    }

    // 驱动关键节点都写入日志（下次崩溃可用 EFCHSword_log.txt 定位阶段）
    qInfo() << "Deploy check: resource SHA" << QString::fromLatin1(resHash.toHex()).left(16);

    bool residentUnloadFailed = false;
    if (diskMatches) {
        qInfo() << "On-disk driver matches resource, SHA256:"
                << QString::fromLatin1(resHash.toHex());
    } else {
        qInfo() << "On-disk driver differs from resource (or missing), will redeploy.\n"
                << "  res  :" << QString::fromLatin1(resHash.toHex());

        // 资源与磁盘不一致: 若有驻留驱动实例, 无条件卸载 (不比较版本号,
        // 防止版本号相同的旧构建被复用), 释放 sys 占用后覆盖
        if (m_driver.probeResidentDriver()) {
            qInfo() << "Resident driver instance detected, unloading before redeploy";
            if (m_driver.uninstallDriver()) {
                qInfo() << "Resident driver unloaded";
            } else {
                // 卸载失败: 驱动仍占用 sys。常见原因是上次崩溃留下的
                // 受保护/僵尸 EFCHSwordNeo 实例还握着设备句柄——
                // 用驱动强杀所有残留实例后重试卸载（自愈，免重启）。
                qWarning() << "Resident driver unload failed, killing stale instances";
                residentUnloadFailed = !killStaleInstancesAndRetryUnload();
            }
        }
    }

    // 一致时的快速路径: 直接安装 (复用驻留实例或从磁盘加载)
    if (diskMatches) {
        if (m_driver.installDriver(destPath, &errMsg)) {
            qInfo() << "Driver connected:" << errMsg;
            return true;
        }
        // installDriver 可能在版本不符时卸载了旧驱动后返回 false (磁盘文件未变),
        // 此时直接重试一次加载
        if (m_driver.installDriver(destPath, &errMsg)) {
            qInfo() << "Driver installed after stale unload:" << errMsg;
            return true;
        }
        qWarning() << "Install driver failed:" << errMsg;
        m_lastError = errMsg;
        return false;
    }

    // 覆盖部署 (用 MoveFileEx 原子替换, 先写临时文件再替换, 避免半写状态)
    QString loadPath = destPath;
    {
        const QString tempPath = destPath + QStringLiteral(".tmp");
        QFile::remove(tempPath);
        QFile tempFile(tempPath);
        if (tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            tempFile.write(resData);
            tempFile.close();

            // 尝试原子替换 (失败则直接用原文件)
            if (MoveFileExW(reinterpret_cast<LPCWSTR>(tempPath.utf16()),
                            reinterpret_cast<LPCWSTR>(destPath.utf16()),
                            MOVEFILE_REPLACE_EXISTING)) {
                qInfo() << "Driver overwritten successfully";
            } else {
                DWORD err = GetLastError();
                qWarning() << "MoveFileEx failed, error:" << err
                           << "- will try direct write, or load existing on-disk file";
                // 原子替换失败, 尝试直接写
                QFile::remove(tempPath);
                QFile directFile(destPath);
                if (directFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    directFile.write(resData);
                    directFile.close();
                    qInfo() << "Driver overwritten via direct write";
                } else {
                    // 覆盖不了: 若旧驱动还驻留(卸载失败), 绝不能静默加载
                    // 硬盘上的旧版本——那会让用户以为新驱动已生效。
                    // 明确报错让用户重启系统。
                    if (residentUnloadFailed) {
                        qWarning() << "Cannot overwrite driver and resident driver unload failed";
                        m_lastError = QStringLiteral(
                            "检测到旧版驻留驱动且无法卸载/覆盖（Sys 文件被占用）。"
                            "当前所有功能仍在使用旧驱动，请关闭本程序后重启系统，再次运行即可加载新驱动。");
                        return false;
                    }
                    qWarning() << "Cannot overwrite driver, loading existing on-disk file:"
                               << directFile.errorString();
                    if (!QFile::exists(destPath)) {
                        m_lastError = QStringLiteral("无法写入驱动文件且硬盘上无可用驱动: ")
                                    + directFile.errorString();
                        return false;
                    }
                    // loadPath 保持为 destPath, 加载硬盘上现有版本
                }
            }
        } else {
            // 临时文件都写不了, 但硬盘上可能有旧版本
            qWarning() << "Cannot create temp file for overwrite:" << tempFile.errorString();
            if (!QFile::exists(destPath)) {
                m_lastError = QStringLiteral("无法创建临时驱动文件: ") + tempFile.errorString();
                return false;
            }
        }
    }

    // 打印最终加载文件的哈希 (调试用)
    {
        QFile loadFile(loadPath);
        if (loadFile.open(QIODevice::ReadOnly)) {
            QByteArray loadData = loadFile.readAll();
            loadFile.close();
            qInfo() << "Loading driver from:" << loadPath
                    << "size:" << loadData.size() << "bytes"
                    << "SHA256:" << QString::fromLatin1(
                          QCryptographicHash::hash(loadData, QCryptographicHash::Sha256).toHex());
        } else {
            qInfo() << "Loading driver from:" << loadPath
                    << "size:" << QFileInfo(loadPath).size() << "bytes";
        }
    }

    if (!m_driver.installDriver(loadPath, &errMsg)) {
        qWarning() << "Install driver failed:" << errMsg;
        m_lastError = errMsg;
        return false;
    }
    return true;
}

// 强杀残留的 EFCHSwordNeo 实例后重试卸载驱动（自愈路径，免重启）。
// 旧版本启动时对自己 protectProcess+PPL，崩溃后留下的僵尸进程普通手段
// 杀不掉（驱动还在替它剥句柄），但驱动自己可以 ZwTerminateProcess 强杀。
// 返回 true 表示卸载最终成功。
bool AppContext::killStaleInstancesAndRetryUnload()
{
    const QString selfName = QStringLiteral("EFCHSwordNeo.exe");
    const DWORD selfPid = GetCurrentProcessId();
    QVector<DWORD> stalePids;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, (const wchar_t*)selfName.utf16()) == 0 &&
                    pe.th32ProcessID != selfPid) {
                    stalePids.append(pe.th32ProcessID);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    for (DWORD pid : stalePids) {
        qInfo() << "Killing stale EFCHSwordNeo instance, PID:" << pid;
        // 依次尝试: 驱动强杀（可杀受保护进程）-> 后备 TerminateProcess
        if (!m_driver.zwKillProcess(pid)) {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) {
                TerminateProcess(h, 1);
                CloseHandle(h);
            }
        }
    }
    if (!stalePids.isEmpty()) {
        Sleep(800);   // 等句柄释放
    }

    if (m_driver.uninstallDriver()) {
        qInfo() << "Resident driver unloaded after killing stale instances";
        return true;
    }
    return false;
}

bool AppContext::initialize()
{
    // 0. 命令行参数检测：-fake/-fakesafe 激活 FakeMode
    //    不区分大小写，直接进入 FakeMode 不再伪造 svchost/lsass 命令行
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                QString arg = QString::fromWCharArray(argv[i]).toLower();
                if (arg == QStringLiteral("-fakesafe") || arg == QStringLiteral("/fakesafe")) {
                    m_fakeMode = true;
                    m_fakeSafeMode = true;
                    break;
                }
                if (arg == QStringLiteral("-fake") || arg == QStringLiteral("/fake")) {
                    m_fakeMode = true;
                    m_fakeSafeMode = false;
                    break;
                }
            }
            LocalFree(argv);
        }
    }

    // 1. 提权
    enablePrivileges();

    // 2. 检测系统版本
    detectWindowsVersion();

    // 3. 检查是否 64 位系统
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        QMessageBox::warning(nullptr, QStringLiteral("EFCHSword"),
            QStringLiteral("请使用64位操作系统!"));
        return false;
    }

    // 4. 检查系统版本最低要求 (Win10)
    if (m_winMajor < 10) {
        QMessageBox::warning(nullptr, QStringLiteral("EFCHKernelDriver"),
            QStringLiteral("EFCHKernelDriver仅支持Windows10或以上操作系统,您的系统低于win10,因此不能加载EFCHKernelDriver,很多功能将会受限!"));
        // 不返回 false，继续运行但驱动不加载
        return false;
    }

    // 5. 从资源释放并加载驱动
    if (loadDriverFromResource()) {
        m_driverLoaded = true;
        writeLog(QStringLiteral("Driver is ready"));

        // 注意：【不要】对自身进程做 protectProcess / setPPL 自保！
        // 旧版本在这里把自己标记为 PPL_Antimalware，后果：
        //   1) WER 无法读取/转储受保护进程（崩溃日志 "ReadProcessMemory
        //      failed ... PebBaseAddress" 就是它），崩溃后无法分析；
        //   2) PPL 之后进程内任何非微软签名 DLL 都无法加载，懒加载失败
        //      重试导致 CPU/内存狂飙直至崩溃（间歇性复现）；
        //   3) 崩溃时 cleanup 未执行，受保护的僵尸进程杀不掉，占住
        //      C:\EFCHKernelDriver.sys 与设备句柄，之后每次启动都崩，
        //      只能重启系统。
        // 自保收益为零（ARK 工具无需防杀），已彻底移除。
        // FakeMode（-fake 显式启用）的保护仍然保留。

        // 7. 如果是 FakeMode，应用保护措施
        applyFakeModeProtections();
        return true;
    } else {
        writeLog(QStringLiteral("Driver is not ready"));
        return false;
    }
}

void AppContext::cleanup()
{
    // 先撤销 FakeMode 保护
    revokeFakeModeProtections();

    // 取消启动时的自我保护
    if (m_driverLoaded) {
        DWORD myPid = GetCurrentProcessId();
        m_driver.setPPL(myPid, PPL_None);
        m_driver.unProtectProcess(myPid);
    }

    if (m_driverLoaded) {
        m_driver.uninstallDriver();
        m_driverLoaded = false;
    }
}

void AppContext::writeLog(const QString& msg)
{
    // FakeMode 下不输出
    if (m_fakeMode) return;
    qDebug().noquote() << QStringLiteral("[EFCHSword]") << msg;

    // 同时落文件（exe 同目录），崩溃后可据此还原启动流程定位问题
    QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/EFCHSword_log.txt");
    QFile f(path);
    if (f.size() > 1024 * 1024) {
        // 超过 1MB 轮转
        QFile::remove(path + QStringLiteral(".old"));
        QFile::rename(path, path + QStringLiteral(".old"));
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        f.write(QString(QStringLiteral("[%1] %2\r\n"))
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            .arg(msg).toUtf8());
        f.close();
    }
}

// ===== FakeMode 辅助函数 =====
// 注意：不再伪造 svchost/lsass 命令行，仅通过 -fake/-fakesafe 参数进入 FakeMode

void AppContext::applyFakeModeProtections()
{
    // 对应易语言命令行启动窗口中的 FakeMode 保护逻辑
    if (!m_fakeMode || !m_driverLoaded) return;

    DWORD myPid = GetCurrentProcessId();

    // 1. 设置为系统关键进程（除非注册表禁用）
    QSettings fmSettings(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\EFCH\\EFCHSword"),
                         QSettings::NativeFormat);
    if (fmSettings.value(QStringLiteral("fm_nocriticalme")).toInt() != 1) {
        m_driver.setProcessCritical(myPid, 1);
    }

    // 2. 强制删除 C:\EFCHKernelDriver.sys（清理痕迹）
    m_driver.forceDeleteFile(QStringLiteral("C:\\EFCHKernelDriver.sys"));

    // 3. 保护自身进程
    m_driver.protectProcess(myPid);

    // 4. Hook 反终止（FakeSafeMode 不需要，因为 lsass 本身就是关键进程）
    if (!m_fakeSafeMode) {
        m_driver.hookAntiTerminateProcess(myPid);
    }

    // 5. 设置 PPL 为 WinSystem
    m_driver.setPPL(myPid, PPL_WinSystem);

    // 6. 隐藏自身进程（除非注册表禁用）
    if (fmSettings.value(QStringLiteral("fm_nohideme")).toInt() != 1) {
        m_driver.hideProcess(myPid);
    }

    // 7. 强制占用自身 exe 文件
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    m_driver.forceOccupyFile(QString::fromWCharArray(exePath));

    // 8. 保护所有线程
    QVector<DWORD> tids = EnumProcessThreads(myPid);
    for (DWORD tid : tids) {
        m_driver.hookProtectThread(tid);
    }

    // 9. 强力保护进程（易语言中调用两次）
    m_driver.forceProtectProcess(myPid);
    m_driver.forceProtectProcess(myPid);
}

bool AppContext::activateFakeMode(bool safeMode)
{
    // 不再伪造 svchost/lsass 命令行
    // 直接以 -fake/-fakesafe 参数启动新实例
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    QString cmdLine = QString::fromWCharArray(exePath) +
                      (safeMode ? QStringLiteral(" -fakesafe") : QStringLiteral(" -fake"));

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    std::wstring fullCmd = cmdLine.toStdWString();
    BOOL ok = CreateProcessW(exePath, const_cast<LPWSTR>(fullCmd.c_str()),
                             nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    if (!ok) {
        QMessageBox::warning(nullptr, QStringLiteral("EFCHSword"),
            QStringLiteral("创建 FakeMode 进程失败，错误码: %1").arg(GetLastError()));
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void AppContext::revokeFakeModeProtections()
{
    // 对应易语言 结束EFCHSword 中的 FakeMode 清理
    if (!m_fakeMode) return;

    DWORD myPid = GetCurrentProcessId();

    // 取消 PPL
    m_driver.setPPL(myPid, PPL_None);
    // 取消反终止 Hook
    m_driver.hookUnAntiTerminateProcess(myPid);
    // 取消线程保护
    QVector<DWORD> tids = EnumProcessThreads(myPid);
    for (DWORD tid : tids) {
        m_driver.hookUnProtectThread(tid);
    }
    // 取消进程保护
    m_driver.unForceProtectProcess(myPid);
    m_driver.unProtectProcess(myPid);
    // 取消关键进程
    m_driver.setProcessCritical(myPid, 0);
}
