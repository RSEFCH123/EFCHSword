#pragma once

#include <QObject>
#include <QString>
#include "KernelDriver.h"

// 全局应用上下文，管理驱动实例和全局状态
// 对应易语言中的全局变量：DriverLoaded, FakeMode, FakeSafeMode, SystemPartition, WinVer 等
class AppContext : public QObject
{
    Q_OBJECT
public:
    static AppContext& instance();

    // 驱动封装
    KernelDriver& driver() { return m_driver; }

    // 全局状态
    bool driverLoaded() const { return m_driverLoaded; }
    void setDriverLoaded(bool loaded) { m_driverLoaded = loaded; }

    bool fakeMode() const { return m_fakeMode; }
    void setFakeMode(bool v) { m_fakeMode = v; }

    bool fakeSafeMode() const { return m_fakeSafeMode; }
    void setFakeSafeMode(bool v) { m_fakeSafeMode = v; }

    // 系统盘符（如 "C"）
    QString systemPartition() const { return m_systemPartition; }
    void setSystemPartition(const QString& v) { m_systemPartition = v; }

    // 版本信息
    QString version() const { return QStringLiteral("1.0.0 Neo"); }
    QString betaVersion() const { return QStringLiteral("Neo"); }

    // Windows 版本信息
    int winMajor() const { return m_winMajor; }
    int winMinor() const { return m_winMinor; }
    int winBuild() const { return m_winBuild; }
    QString winName() const { return m_winName; }
    void setWinInfo(int major, int minor, int build, const QString& name) {
        m_winMajor = major; m_winMinor = minor; m_winBuild = build; m_winName = name;
    }

    // 初始化：提权、检测系统版本、加载驱动
    // 返回驱动是否加载成功
    bool initialize();

    // 释放资源：卸载驱动等
    void cleanup();

    // 写工作日志
    void writeLog(const QString& msg);

    // 最后一次错误信息
    QString lastError() const { return m_lastError; }

    // 命令行请求 FakeMode 时，激活后应退出当前实例
    bool wantExit() const { return m_wantExit; }

    // FakeMode/FakeSafeMode 相关
    // 应用 FakeMode 保护措施（需在驱动加载后调用）
    void applyFakeModeProtections();
    // 激活 FakeMode：以 -fake/-fakesafe 参数启动新实例
    bool activateFakeMode(bool safeMode);
    // 取消 FakeMode 保护（退出前调用）
    void revokeFakeModeProtections();

private:
    AppContext();
    ~AppContext() override;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    bool enablePrivileges();
    void detectWindowsVersion();
    bool loadDriverFromResource();

    KernelDriver m_driver;
    bool m_driverLoaded = false;
    bool m_fakeMode = false;
    bool m_fakeSafeMode = false;
    QString m_systemPartition = QStringLiteral("C");
    int m_winMajor = 0;
    int m_winMinor = 0;
    int m_winBuild = 0;
    QString m_winName;
    QString m_lastError;
    bool m_wantExit = false;
};
