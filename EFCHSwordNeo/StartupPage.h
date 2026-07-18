#pragma once

#include <QWidget>

class QTableWidget;
class QCheckBox;
class QLabel;

// 启动项标签页
// 对应易语言: 启动项列表 + 枚举目录启动项 + 枚举注册表启动项 + 枚举服务
class StartupPage : public QWidget
{
    Q_OBJECT
public:
    explicit StartupPage(QWidget* parent = nullptr);
    ~StartupPage() override;

private slots:
    void onRefresh();
    void onContextMenu(const QPoint& pos);
    void onDeleteStartup();
    void onOpenFileLocation();
    void onCopyPath();

private:
    void setupUi();
    void setupContextMenu();
    void enumDirectoryStartup(const QString& dir, const QString& source);
    void enumRegistryStartup(quint32 root, const QString& path, const QString& source);
    void enumServices();

    QTableWidget* m_table = nullptr;
    QCheckBox* m_hideSystem = nullptr;
    QCheckBox* m_hideSystemSvc = nullptr;
    QCheckBox* m_onlyAutoSvc = nullptr;
    QLabel* m_statusLabel = nullptr;
    class QMenu* m_contextMenu = nullptr;
};
