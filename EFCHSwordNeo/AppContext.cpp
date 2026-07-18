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
static QVector<DWORD> EnumProcessThreads(DWORD pid)
{
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
    // 从 Qt 资源释放驱动 sys 文件，然后加载
    // 资源路径: ":/drivers/EFCHKernelDriver.sys"
    QString resPath = QStringLiteral(":/drivers/EFCHKernelDriver.sys");
    if (!QFile::exists(resPath)) {
        qWarning() << "Driver resource not found:" << resPath;
        return false;
    }

    // 释放到 C:\ 根目录（与易语言版本一致，避免中文路径导致 NtLoadDriver 失败）
    QString destPath = QStringLiteral("C:\\EFCHKernelDriver.sys");

    QFile resFile(resPath);
    if (!resFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open driver resource";
        return false;
    }
    QFile destFile(destPath);
    if (!destFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Cannot write driver to:" << destPath << "error:" << destFile.errorString();
        return false;
    }
    destFile.write(resFile.readAll());
    destFile.close();
    resFile.close();

    // 校验释放后的文件哈希，确认资源未被损坏
    QFile hashCheck(destPath);
    if (hashCheck.open(QIODevice::ReadOnly)) {
        QCryptographicHash sha256(QCryptographicHash::Sha256);
        sha256.addData(&hashCheck);
        QByteArray hash = sha256.result().toHex();
        hashCheck.close();
        qInfo() << "Extracted driver SHA256:" << QString::fromLatin1(hash)
                << "size:" << QFileInfo(destPath).size() << "bytes";
    } else {
        qWarning() << "Cannot re-open extracted file for hash check";
    }

    // 加载驱动
    QString errMsg;
    if (!m_driver.installDriver(destPath, &errMsg)) {
        qWarning() << "Install driver failed:" << errMsg;
        m_lastError = errMsg;
        return false;
    }
    return true;
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

        // 6. 启动时自我保护（protectProcess + PPL_Antimalware）
        DWORD myPid = GetCurrentProcessId();
        if (m_driver.protectProcess(myPid)) {
            writeLog(QStringLiteral("Self-protect enabled (IOCTL_PROTECTPROCESS)"));
        }
        if (m_driver.setPPL(myPid, PPL_Antimalware)) {
            writeLog(QStringLiteral("PPL set to Antimalware"));
        }

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
    // 简单的控制台输出（FakeMode 下不输出）
    if (m_fakeMode) return;
    qDebug().noquote() << QStringLiteral("[EFCHSword]") << msg;
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
