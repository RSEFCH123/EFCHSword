#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

class WelcomePage;
class ProcessPage;
class WindowPage;
class FilePage;
class ToolsPage;
class StartupPage;
class RegeditPage;
class WinNTPage;
class AboutPage;
class SystemThreadPage;

// 主窗口
// 对应易语言: _启动窗口 (高级选择夹1 + 标签页)
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNavigateChanged(int index);
    void onThemeChanged(int theme);

private:
    void setupUi();
    void setupStatusBar();
    void updateWindowTitle();

    // 左侧导航
    QListWidget* m_navList = nullptr;
    // 右侧内容区
    QStackedWidget* m_stack = nullptr;

    // 各标签页
    WelcomePage* m_welcomePage = nullptr;
    ProcessPage* m_processPage = nullptr;
    WindowPage* m_windowPage = nullptr;
    FilePage* m_filePage = nullptr;
    StartupPage* m_startupPage = nullptr;
    ToolsPage* m_toolsPage = nullptr;
    RegeditPage* m_regeditPage = nullptr;
    WinNTPage* m_winntPage = nullptr;
    AboutPage* m_aboutPage = nullptr;
    SystemThreadPage* m_systemThreadPage = nullptr;

    // 状态栏
    QLabel* m_statusVersion = nullptr;
    QLabel* m_statusProcessCount = nullptr;
    QLabel* m_statusCpu = nullptr;
    QLabel* m_statusMemory = nullptr;
};
