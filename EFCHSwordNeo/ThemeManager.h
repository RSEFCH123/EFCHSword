#pragma once

#include <QObject>
#include <QString>

class QApplication;

// 主题管理器
// 检测 Windows 系统深色/浅色模式，自动切换应用主题
// 风格: Win11 Fluent Design + VSCode 专业风格
class ThemeManager : public QObject
{
    Q_OBJECT
public:
    enum Theme {
        Light,
        Dark
    };

    static ThemeManager& instance();

    // 初始化主题，监听系统主题变化
    void initialize(QApplication* app);

    // 当前主题
    Theme currentTheme() const { return m_theme; }
    bool isDark() const { return m_theme == Dark; }

    // 手动切换主题
    void setTheme(Theme theme);

    // 获取样式表
    QString stylesheet(Theme theme) const;

signals:
    void themeChanged(Theme theme);

private:
    ThemeManager();
    ~ThemeManager() override;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    // 检测系统深色模式
    bool detectSystemDarkMode();
    // 监听系统主题变化
    void startThemeWatcher();
    // 应用主题
    void applyTheme(Theme theme);

    Theme m_theme = Dark;
    QApplication* m_app = nullptr;
    class QWinEventNotifier* m_watcher = nullptr;
};
