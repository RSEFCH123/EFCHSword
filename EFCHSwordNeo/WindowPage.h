#pragma once

#include <QWidget>

class QTableWidget;
class QMenu;
class QCheckBox;
class QLabel;
class QTimer;

// 窗口管理标签页
// 对应易语言: 超级列表框1 (窗口列表) + 窗口操作按钮
class WindowPage : public QWidget
{
    Q_OBJECT
public:
    explicit WindowPage(QWidget* parent = nullptr);
    ~WindowPage() override;

    void refreshWindows();

private slots:
    void onRefreshClicked();
    void onShowHiddenToggled(bool checked);
    void onContextMenu(const QPoint& pos);

    // 窗口操作
    void onCloseWindow();
    void onForceCloseWindow();
    void onShowWindow();
    void onHideWindow();
    void onRestoreWindow();
    void onMinimizeWindow();
    void onMaximizeWindow();
    void onDisableWindow();
    void onEnableWindow();
    void onSetTopmost();
    void onUnsetTopmost();
    void onSetTransparent();
    void onForceShowWindow();
    void onCopyWindowTitle();
    void onCopyWindowHandle();
    void onOpenProcessLocation();

private:
    void setupUi();
    void setupContextMenu();
    HWND currentHwnd() const;

    QTableWidget* m_table = nullptr;
    QCheckBox* m_showHidden = nullptr;
    QCheckBox* m_showNoTitle = nullptr;
    QLabel* m_statusLabel = nullptr;
    QMenu* m_contextMenu = nullptr;
    QTimer* m_autoRefreshTimer = nullptr;
};
