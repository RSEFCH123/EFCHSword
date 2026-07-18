#include "MainWindow.h"
#include "AppContext.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>

#include <windows.h>
#include <shellapi.h>

// 检查当前进程是否以管理员权限运行
static bool IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// 通过 UAC 重启自身以管理员权限运行
static void RestartAsAdmin()
{
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

int main(int argc, char* argv[])
{
    // 检查管理员权限，非管理员则 UAC 提权重启
    if (!IsRunningAsAdmin()) {
        RestartAsAdmin();
        return 0;
    }

    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EFCHSwordNeo"));
    QApplication::setOrganizationName(QStringLiteral("EFCH"));

    // 初始化主题管理器（跟随系统深色/浅色模式）
    ThemeManager::instance().initialize(&a);

    // 初始化应用上下文（提权、检测系统版本、加载驱动）
    auto& ctx = AppContext::instance();
    bool driverOk = ctx.initialize();

    // 命令行请求 FakeMode 时，新实例已启动，当前实例直接退出
    if (ctx.wantExit()) {
        return 0;
    }

    if (!driverOk) {
        // 驱动加载失败，显示具体错误并询问是否继续
        QString errDetail = ctx.lastError().isEmpty()
            ? QStringLiteral("未知错误") : ctx.lastError();
        auto ret = QMessageBox::warning(nullptr, QStringLiteral("EFCHSword"),
            QStringLiteral("出现致命错误!\nEFCHKernelDriver 驱动安装失败!\n\n错误详情:\n%1\n\n忽略请按确定,退出程序请按取消").arg(errDetail),
            QMessageBox::Ok | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) {
            ctx.cleanup();
            return 0;
        }
    }

    MainWindow w;
    w.show();
    return a.exec();
}
