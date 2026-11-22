#pragma once

#include <QWidget>

class QGroupBox;
class QCheckBox;
class QTableWidget;
class QLineEdit;
class QComboBox;
class QMenu;
class QAction;

// 当前枚举模式（决定右键菜单显示哪些卸载选项）
enum class EnumMode {
    None,         // 未枚举
    Drivers,      // 枚举驱动：只显示"强制卸载驱动"
    Devices,      // 枚举设备：只显示"强制卸载设备"
    DriversPath   // 枚举驱动路径：只显示"强制卸载驱动"（不显示卸载设备）
};

// 工具标签页
// 对应易语言: Tools_Window (驱动安装/卸载 + 系统工具 + 驱动设备枚举)
class ToolsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ToolsPage(QWidget* parent = nullptr);
    ~ToolsPage() override;

private slots:
    // 驱动管理
    void onInstallDriver();
    void onUninstallDriver();
    void onBrowseDriver();

    // 系统工具
    void onBSOD();
    void onBSODAuto();
    void onBSODRainbow();
    void onSetBSODStopCode();  // 设置蓝屏停止代码文本
    void onHaltAllCPUs();
    void onPowerOff();
    void onForceReboot();

    // 系统功能开关 (右键菜单设置子菜单触发)
    void onDisEnaLoadDriver(bool checked);
    void onDisEnaCreateProcess(bool checked);
    void onDisEnaRegisterControl(bool checked);
    void onDisEnaBSOD(bool checked);

    // 驱动/设备枚举
    void onEnumDrivers();
    void onEnumDevices();
    void onEnumDriversPath();

    // 右键菜单: 强制卸载驱动/设备
    void onContextMenu(const QPoint& pos);
    void onForceUnloadDriver();
    void onForceUnloadDevice();

    // 端口 I/O
    void onPortPowerOff();
    void onPortKillBIOSPwd();
    void onPortReadCMOS();
    void onPortWriteCMOS();
    void onPortDisEnaKeyboard();
    void onPortDisEnaMouse();

    // BSOD 颜色
    void onSetBSODColor();

    // DiskHex 磁盘扇区编辑器（独立窗口）
    void onDiskHex();

private:
    void setupUi();
    void setupContextMenu();
    QString currentItemName() const;

    // 驱动管理
    QLineEdit* m_driverPathEdit = nullptr;
    QLineEdit* m_driverNameEdit = nullptr;

    // 驱动/设备列表
    QTableWidget* m_driverTable = nullptr;

    // 右键菜单
    QMenu* m_contextMenu = nullptr;
    QAction* m_actUnloadDriver = nullptr;   // 强制卸载驱动
    QAction* m_actUnloadDevice = nullptr;   // 强制卸载设备
    QAction* m_actDisLoadDriver = nullptr;
    QAction* m_actDisCreateProc = nullptr;
    QAction* m_actDisRegCtrl = nullptr;
    QAction* m_actDisBSOD = nullptr;

    // 当前枚举模式
    EnumMode m_enumMode = EnumMode::None;

    // 端口 I/O
    QLineEdit* m_cmosAddr = nullptr;
    QLineEdit* m_cmosData = nullptr;
};
