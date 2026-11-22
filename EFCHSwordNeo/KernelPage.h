#pragma once

#include <QWidget>
#include <QVector>
#include "KernelDriver.h"

class QTabWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;
class QLineEdit;
class QComboBox;
class QMenu;
class QTimer;

// 内核大项页面
// 子项: 驱动管理(\Driver对象) / 驱动设备管理 / 驱动列表(内核模块)
//       PspCidTable / 注册热键(进程RegisterHotKey) / 命名空间管理 / 系统视图(SSDT/IDT/对象类型)
class KernelPage : public QWidget
{
    Q_OBJECT
public:
    explicit KernelPage(QWidget* parent = nullptr);
    ~KernelPage() override;

private slots:
    // 驱动管理
    void onRefreshDrivers();
    void onDriverContextMenu(const QPoint& pos);
    void onDriverDetail();
    void onDriverUnload();          // 强制卸载选中的驱动
    // 设备管理
    void onRefreshDevices();
    void onDeviceContextMenu(const QPoint& pos);
    void onDeviceDetail();
    void onDeviceUnloadDriver();    // 卸载设备所属驱动
    void onDeviceDelete();          // 删除设备对象
    // 驱动列表(模块)
    void onRefreshModules();
    void onModuleContextMenu(const QPoint& pos);
    void onModuleUnload();          // 强制卸载选中的模块驱动
    // PspCidTable
    void onCidModeChanged(int index);
    void onCidRefresh();
    void onCidContextMenu(const QPoint& pos);
    void onCidTerminateProcess();
    void onCidTerminateThread();
    void onCidShowProcessThreads();
    // 注册热键
    void onRefreshHotkeys();
    void onHotkeyContextMenu(const QPoint& pos);
    void onHotkeyModify();          // 修改选中热键
    void onHotkeyDelete();          // 删除选中热键
    // 命名空间
    void onNsGo();
    void onNsUp();
    void onNsItemActivated(QTreeWidgetItem* item, int column);
    void onNsItemExpanded(QTreeWidgetItem* item);
    void onNsContextMenu(const QPoint& pos);
    void onNsQueryObject();
    void onNsDeleteObject();
    // 系统视图
    void onSysViewModeChanged(int index);
    void onSysViewRefresh();

private:
    void setupUi();
    QWidget* createDriverTab();
    QWidget* createDeviceTab();
    QWidget* createModuleTab();
    QWidget* createCidTab();
    QWidget* createHotkeyTab();
    QWidget* createNsTab();
    QWidget* createSysViewTab();

    bool requireDriver();   // 检查驱动加载，失败弹提示

    // 驱动管理
    QTableWidget* m_driverTable = nullptr;
    QLineEdit* m_driverFilterEdit = nullptr;
    QLabel* m_driverStatusLabel = nullptr;
    QLabel* m_driverVersionLabel = nullptr;   // 当前加载的驱动版本（排查旧驱动残留用）
    QVector<ArkDriverObjEntry> m_driverEntries;

    // 设备管理
    QTableWidget* m_deviceTable = nullptr;
    QLabel* m_deviceStatusLabel = nullptr;
    QVector<ArkDeviceEntry> m_deviceEntries;

    // 驱动列表(模块)
    QTableWidget* m_moduleTable = nullptr;
    QLineEdit* m_moduleFilterEdit = nullptr;
    QLabel* m_moduleStatusLabel = nullptr;

    // PspCidTable
    QComboBox* m_cidModeCombo = nullptr;
    QTableWidget* m_cidTable = nullptr;
    QLabel* m_cidStatusLabel = nullptr;
    QVector<ArkCidProcessEntry> m_cidProcesses;
    QVector<ArkCidThreadEntry> m_cidThreads;
    quint32 m_cidThreadFilterPid = 0;   // 0=全部
    QMenu* m_cidContextMenu = nullptr;

    // 注册热键
    QTableWidget* m_hotkeyTable = nullptr;
    QLineEdit* m_hotkeyFilterEdit = nullptr;
    QLabel* m_hotkeyStatusLabel = nullptr;
    QVector<ArkHotkeyEntry> m_hotkeyEntries;   // 当前显示的行对应的原始条目

    // 命名空间
    QLineEdit* m_nsPathEdit = nullptr;
    QTreeWidget* m_nsTree = nullptr;
    QLabel* m_nsStatusLabel = nullptr;
    QString m_nsCurrentPath;
    QMenu* m_nsContextMenu = nullptr;

    // 系统视图
    QComboBox* m_sysViewCombo = nullptr;
    QTableWidget* m_sysViewTable = nullptr;
    QLabel* m_sysViewStatusLabel = nullptr;
};
