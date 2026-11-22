#pragma once

#include <QWidget>

class QTableWidget;
class QCheckBox;
class QLabel;
class QTabWidget;
class QComboBox;

// 启动项与服务页
//   启动项 tab: 启动文件夹 + Run/RunOnce(Ex) 注册表项，支持新建（高自定义）、
//               删除、禁用/启用（StartupApproved 机制，禁用项淡红显示）
//   服务 tab:   系统服务列表（禁用项淡红显示），支持新建服务、
//               启动类型调整（自动/手动/禁用）与删除服务
class StartupPage : public QWidget
{
    Q_OBJECT
public:
    explicit StartupPage(QWidget* parent = nullptr);
    ~StartupPage() override;

private slots:
    // 启动项
    void onRefreshStartup();
    void onStartupContextMenu(const QPoint& pos);
    void onCreateStartup();       // 新建启动项（高自定义对话框）
    void onCreateTask();          // 新建登录触发计划任务
    void onDeleteStartup();
    void onToggleStartup();       // 禁用/启用启动项
    void onOpenFileLocation();
    void onCopyPath();
    // 服务
    void onRefreshServices();
    void onServiceContextMenu(const QPoint& pos);
    void onCreateService();       // 新建服务
    void onServiceStartType(int startType); // 修改启动类型(2=自动 3=手动 4=禁用)
    void onServiceSafeBoot(int which);      // 安全模式启动开关(0=Minimal 1=Network)
    void onDeleteService();
    void onCopyServicePath();

private:
    void setupUi();
    // 启动项分类过滤
    void applyCategoryFilter();
    // 启动项辅助
    void enumDirectoryStartup(const QString& dir, const QString& source, bool userScope);
    void enumRegistryStartup(quint32 root, const QString& path, const QString& source,
                             const QString& approvedKey);
    void enumTaskSchedulerStartup();
    // 返回 StartupApproved 中该项是否被禁用（无记录 = 启用）
    bool isStartupApprovedDisabled(quint32 root, const QString& approvedKey,
                                   const QString& name) const;
    void setStartupApprovedDisabled(quint32 root, const QString& approvedKey,
                                    const QString& name, bool disabled);
    // 服务辅助
    void appendServiceRow(const QString& name, const QString& startType,
                          const QString& displayName, const QString& imagePath,
                          bool disabled);

    QTabWidget* m_tabs = nullptr;

    // 启动项 tab
    QTableWidget* m_startupTable = nullptr;
    QCheckBox* m_hideMicrosoft = nullptr;
    QLabel* m_startupStatusLabel = nullptr;
    class QMenu* m_startupMenu = nullptr;
    QComboBox* m_categoryCombo = nullptr;

    // 服务 tab
    QTableWidget* m_serviceTable = nullptr;
    QCheckBox* m_onlyAutoSvc = nullptr;
    QCheckBox* m_hideDriverSvc = nullptr;
    QLabel* m_serviceStatusLabel = nullptr;
    QMenu* m_serviceMenu = nullptr;
};
