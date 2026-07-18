#include "ToolsPage.h"
#include "AppContext.h"
#include "DiskHexDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QColorDialog>
#include <QScrollArea>
#include <QMenu>
#include <QAction>

#include <windows.h>

ToolsPage::ToolsPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupContextMenu();
}

ToolsPage::~ToolsPage() = default;

void ToolsPage::setupUi()
{
    // 使用滚动区域以容纳所有工具
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setSpacing(8);

    // ===== 驱动管理 =====
    auto* driverGroup = new QGroupBox(QStringLiteral("驱动管理"), contentWidget);
    auto* driverLayout = new QGridLayout(driverGroup);

    driverLayout->addWidget(new QLabel(QStringLiteral("驱动路径:")), 0, 0);
    m_driverPathEdit = new QLineEdit(driverGroup);
    driverLayout->addWidget(m_driverPathEdit, 0, 1);
    auto* browseBtn = new QPushButton(QStringLiteral("浏览..."), driverGroup);
    driverLayout->addWidget(browseBtn, 0, 2);
    connect(browseBtn, &QPushButton::clicked, this, &ToolsPage::onBrowseDriver);

    driverLayout->addWidget(new QLabel(QStringLiteral("服务名称:")), 1, 0);
    m_driverNameEdit = new QLineEdit(driverGroup);
    driverLayout->addWidget(m_driverNameEdit, 1, 1);

    auto* installBtn = new QPushButton(QStringLiteral("安装驱动"), driverGroup);
    auto* uninstallBtn = new QPushButton(QStringLiteral("卸载驱动"), driverGroup);
    driverLayout->addWidget(installBtn, 2, 0);
    driverLayout->addWidget(uninstallBtn, 2, 1);
    connect(installBtn, &QPushButton::clicked, this, &ToolsPage::onInstallDriver);
    connect(uninstallBtn, &QPushButton::clicked, this, &ToolsPage::onUninstallDriver);

    mainLayout->addWidget(driverGroup);

    // ===== 系统工具 (危险!) =====
    auto* sysToolGroup = new QGroupBox(QStringLiteral("系统工具 (危险!)"), contentWidget);
    auto* sysToolLayout = new QGridLayout(sysToolGroup);

    auto* bsodBtn = new QPushButton(QStringLiteral("蓝屏 (指定代码)"), sysToolGroup);
    auto* bsodStopCodeBtn = new QPushButton(QStringLiteral("设置蓝屏停止代码"), sysToolGroup);
    auto* bsodAutoBtn = new QPushButton(QStringLiteral("自动蓝屏"), sysToolGroup);
    auto* bsodRainbowBtn = new QPushButton(QStringLiteral("彩虹蓝屏"), sysToolGroup);
    auto* bsodColorBtn = new QPushButton(QStringLiteral("设置蓝屏颜色"), sysToolGroup);
    auto* haltAllCpuBtn = new QPushButton(QStringLiteral("停机 (所有CPU)"), sysToolGroup);
    auto* powerOffBtn = new QPushButton(QStringLiteral("断电"), sysToolGroup);
    auto* forceRebootBtn = new QPushButton(QStringLiteral("断电并重启"), sysToolGroup);

    sysToolLayout->addWidget(bsodBtn, 0, 0);
    sysToolLayout->addWidget(bsodStopCodeBtn, 0, 1);
    sysToolLayout->addWidget(bsodAutoBtn, 1, 0);
    sysToolLayout->addWidget(bsodRainbowBtn, 1, 1);
    sysToolLayout->addWidget(bsodColorBtn, 2, 0);
    sysToolLayout->addWidget(haltAllCpuBtn, 2, 1);
    sysToolLayout->addWidget(powerOffBtn, 3, 0);
    sysToolLayout->addWidget(forceRebootBtn, 3, 1);

    connect(bsodBtn, &QPushButton::clicked, this, &ToolsPage::onBSOD);
    connect(bsodStopCodeBtn, &QPushButton::clicked, this, &ToolsPage::onSetBSODStopCode);
    connect(bsodAutoBtn, &QPushButton::clicked, this, &ToolsPage::onBSODAuto);
    connect(bsodRainbowBtn, &QPushButton::clicked, this, &ToolsPage::onBSODRainbow);
    connect(bsodColorBtn, &QPushButton::clicked, this, &ToolsPage::onSetBSODColor);
    connect(haltAllCpuBtn, &QPushButton::clicked, this, &ToolsPage::onHaltAllCPUs);
    connect(powerOffBtn, &QPushButton::clicked, this, &ToolsPage::onPowerOff);
    connect(forceRebootBtn, &QPushButton::clicked, this, &ToolsPage::onForceReboot);

    mainLayout->addWidget(sysToolGroup);

    // ===== DiskHex 磁盘扇区编辑器（独立窗口）=====
    auto* diskHexGroup = new QGroupBox(QStringLiteral("磁盘扇区编辑器"), contentWidget);
    auto* diskHexLayout = new QHBoxLayout(diskHexGroup);
    auto* diskHexBtn = new QPushButton(QStringLiteral("打开 DiskHex..."), diskHexGroup);
    diskHexBtn->setMinimumHeight(40);
    diskHexLayout->addWidget(diskHexBtn);
    connect(diskHexBtn, &QPushButton::clicked, this, &ToolsPage::onDiskHex);
    mainLayout->addWidget(diskHexGroup);

    // ===== 驱动/设备枚举 (右键支持强制卸载驱动和设置) =====
    auto* enumGroup = new QGroupBox(QStringLiteral("驱动/设备枚举 (右键: 强制卸载/设置)"), contentWidget);
    auto* enumLayout = new QVBoxLayout(enumGroup);

    auto* hintLabel = new QLabel(
        QStringLiteral("提示: 枚举后右键列表项可强制卸载驱动/设备，或打开设置菜单\n"
                       "枚举驱动/驱动路径: 右键只显示\"强制卸载驱动\"\n"
                       "枚举设备: 右键只显示\"强制卸载设备\""), enumGroup);
    hintLabel->setWordWrap(true);
    enumLayout->addWidget(hintLabel);

    auto* enumBtnLayout = new QHBoxLayout();
    auto* enumDrvBtn = new QPushButton(QStringLiteral("枚举驱动"), enumGroup);
    auto* enumDevBtn = new QPushButton(QStringLiteral("枚举设备"), enumGroup);
    auto* enumPathBtn = new QPushButton(QStringLiteral("枚举驱动路径"), enumGroup);
    enumBtnLayout->addWidget(enumDrvBtn);
    enumBtnLayout->addWidget(enumDevBtn);
    enumBtnLayout->addWidget(enumPathBtn);
    enumLayout->addLayout(enumBtnLayout);

    m_driverTable = new QTableWidget(enumGroup);
    m_driverTable->setColumnCount(1);
    m_driverTable->setHorizontalHeaderLabels({QStringLiteral("名称")});
    m_driverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_driverTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_driverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_driverTable->setAlternatingRowColors(true);
    m_driverTable->verticalHeader()->setVisible(false);
    m_driverTable->horizontalHeader()->setStretchLastSection(true);
    m_driverTable->setMinimumHeight(200);
    m_driverTable->setContextMenuPolicy(Qt::CustomContextMenu);
    enumLayout->addWidget(m_driverTable);

    connect(enumDrvBtn, &QPushButton::clicked, this, &ToolsPage::onEnumDrivers);
    connect(enumDevBtn, &QPushButton::clicked, this, &ToolsPage::onEnumDevices);
    connect(enumPathBtn, &QPushButton::clicked, this, &ToolsPage::onEnumDriversPath);
    connect(m_driverTable, &QTableWidget::customContextMenuRequested, this, &ToolsPage::onContextMenu);

    mainLayout->addWidget(enumGroup);

    // ===== 端口 I/O =====
    auto* portGroup = new QGroupBox(QStringLiteral("端口 I/O"), contentWidget);
    auto* portLayout = new QGridLayout(portGroup);

    auto* portPowerOffBtn = new QPushButton(QStringLiteral("端口断电 (0x64=0xFE)"), portGroup);
    auto* portKillBiosBtn = new QPushButton(QStringLiteral("清除 BIOS 密码"), portGroup);
    auto* portKbdBtn = new QPushButton(QStringLiteral("禁用/启用键盘"), portGroup);
    auto* portMouseBtn = new QPushButton(QStringLiteral("禁用/启用鼠标"), portGroup);

    portLayout->addWidget(portPowerOffBtn, 0, 0);
    portLayout->addWidget(portKillBiosBtn, 0, 1);
    portLayout->addWidget(portKbdBtn, 1, 0);
    portLayout->addWidget(portMouseBtn, 1, 1);

    // CMOS 读写
    portLayout->addWidget(new QLabel(QStringLiteral("CMOS 地址:")), 2, 0);
    m_cmosAddr = new QLineEdit(portGroup);
    m_cmosAddr->setPlaceholderText(QStringLiteral("0x00-0xFF"));
    portLayout->addWidget(m_cmosAddr, 2, 1);

    portLayout->addWidget(new QLabel(QStringLiteral("CMOS 数据:")), 3, 0);
    m_cmosData = new QLineEdit(portGroup);
    portLayout->addWidget(m_cmosData, 3, 1);

    auto* cmosReadBtn = new QPushButton(QStringLiteral("读取 CMOS"), portGroup);
    auto* cmosWriteBtn = new QPushButton(QStringLiteral("写入 CMOS"), portGroup);
    portLayout->addWidget(cmosReadBtn, 4, 0);
    portLayout->addWidget(cmosWriteBtn, 4, 1);

    connect(portPowerOffBtn, &QPushButton::clicked, this, &ToolsPage::onPortPowerOff);
    connect(portKillBiosBtn, &QPushButton::clicked, this, &ToolsPage::onPortKillBIOSPwd);
    connect(portKbdBtn, &QPushButton::clicked, this, &ToolsPage::onPortDisEnaKeyboard);
    connect(portMouseBtn, &QPushButton::clicked, this, &ToolsPage::onPortDisEnaMouse);
    connect(cmosReadBtn, &QPushButton::clicked, this, &ToolsPage::onPortReadCMOS);
    connect(cmosWriteBtn, &QPushButton::clicked, this, &ToolsPage::onPortWriteCMOS);

    mainLayout->addWidget(portGroup);

    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void ToolsPage::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    // 强制卸载驱动（枚举驱动/驱动路径时显示）
    m_actUnloadDriver = m_contextMenu->addAction(QStringLiteral("强制卸载驱动"));
    connect(m_actUnloadDriver, &QAction::triggered, this, &ToolsPage::onForceUnloadDriver);

    // 强制卸载设备（仅枚举设备时显示）
    m_actUnloadDevice = m_contextMenu->addAction(QStringLiteral("强制卸载设备"));
    connect(m_actUnloadDevice, &QAction::triggered, this, &ToolsPage::onForceUnloadDevice);

    m_contextMenu->addSeparator();

    // 设置子菜单（系统功能开关）
    auto* settingsMenu = m_contextMenu->addMenu(QStringLiteral("设置"));

    m_actDisLoadDriver = settingsMenu->addAction(QStringLiteral("禁止加载驱动"));
    m_actDisLoadDriver->setCheckable(true);
    connect(m_actDisLoadDriver, &QAction::toggled, this, &ToolsPage::onDisEnaLoadDriver);

    m_actDisCreateProc = settingsMenu->addAction(QStringLiteral("禁止创建进程"));
    m_actDisCreateProc->setCheckable(true);
    connect(m_actDisCreateProc, &QAction::toggled, this, &ToolsPage::onDisEnaCreateProcess);

    m_actDisRegCtrl = settingsMenu->addAction(QStringLiteral("禁止注册表控制"));
    m_actDisRegCtrl->setCheckable(true);
    connect(m_actDisRegCtrl, &QAction::toggled, this, &ToolsPage::onDisEnaRegisterControl);

    m_actDisBSOD = settingsMenu->addAction(QStringLiteral("阻止蓝屏 (Hook)"));
    m_actDisBSOD->setCheckable(true);
    connect(m_actDisBSOD, &QAction::toggled, this, &ToolsPage::onDisEnaBSOD);
}

// ===== 右键菜单触发 =====
void ToolsPage::onContextMenu(const QPoint& pos)
{
    // 根据当前枚举模式显示/隐藏对应的卸载选项
    // 枚举驱动(Drivers): 只显示"强制卸载驱动"
    // 枚举设备(Devices): 只显示"强制卸载设备"
    // 枚举驱动路径(DriversPath): 只显示"强制卸载驱动"（不显示卸载设备）
    switch (m_enumMode) {
        case EnumMode::Drivers:
        case EnumMode::DriversPath:
            m_actUnloadDriver->setVisible(true);
            m_actUnloadDevice->setVisible(false);
            break;
        case EnumMode::Devices:
            m_actUnloadDriver->setVisible(false);
            m_actUnloadDevice->setVisible(true);
            break;
        default:
            // 未枚举时两个都显示
            m_actUnloadDriver->setVisible(true);
            m_actUnloadDevice->setVisible(true);
            break;
    }

    QModelIndex index = m_driverTable->indexAt(pos);
    Q_UNUSED(index);
    m_contextMenu->exec(m_driverTable->viewport()->mapToGlobal(pos));
}

// 当前选中项的名称
QString ToolsPage::currentItemName() const
{
    int row = m_driverTable->currentRow();
    if (row < 0) return QString();
    auto* item = m_driverTable->item(row, 0);
    return item ? item->text() : QString();
}

// ===== 驱动管理 =====
void ToolsPage::onBrowseDriver()
{
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择驱动文件"),
        QString(), QStringLiteral("驱动文件 (*.sys);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        m_driverPathEdit->setText(path);
        if (m_driverNameEdit->text().isEmpty()) {
            QFileInfo fi(path);
            m_driverNameEdit->setText(fi.baseName());
        }
    }
}

void ToolsPage::onInstallDriver()
{
    QString path = m_driverPathEdit->text().trimmed();
    QString name = m_driverNameEdit->text().trimmed();
    if (path.isEmpty() || name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("请填写驱动路径和服务名称"));
        return;
    }

    QString errMsg;
    KernelDriver drv;
    if (drv.installDriver(path, &errMsg)) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动安装成功"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("驱动安装失败: ") + errMsg);
    }
}

void ToolsPage::onUninstallDriver()
{
    QString name = m_driverNameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("请填写服务名称"));
        return;
    }

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded() && ctx.driver().forceUnloadDriver(name)) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动卸载成功"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动卸载失败"));
    }
}

// ===== 系统工具 =====
void ToolsPage::onBSOD()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    bool ok = false;
    // 蓝屏代码以十六进制输入
    QString codeStr = QInputDialog::getText(this, QStringLiteral("蓝屏"),
        QStringLiteral("BugCheck 代码 (十六进制):"),
        QLineEdit::Normal,
        QStringLiteral("DEADDEAD"),
        &ok);
    if (!ok || codeStr.isEmpty()) return;

    // 移除可能的前缀 0x
    codeStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    bool parseOk = false;
    quint32 code = codeStr.toUInt(&parseOk, 16);
    if (!parseOk) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制代码"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要触发蓝屏吗?这将导致系统崩溃!\nBugCheck 代码: 0x%1")
                .arg(code, 8, 16, QChar('0')),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().bsod(code);
}

void ToolsPage::onSetBSODStopCode()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    bool ok = false;
    // 蓝屏停止代码以文本输入（如 CRITICAL_PROCESS_DIED）
    QString stopCode = QInputDialog::getText(this, QStringLiteral("设置蓝屏停止代码"),
        QStringLiteral("停止代码文本 (如 CRITICAL_PROCESS_DIED):"),
        QLineEdit::Normal,
        QStringLiteral("CRITICAL_PROCESS_DIED"),
        &ok);
    if (!ok || stopCode.isEmpty()) return;

    if (!ctx.driver().bsodSetStopCode(stopCode)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("设置停止代码失败"));
    } else {
        QMessageBox::information(this, QStringLiteral("EFCHSword"),
            QStringLiteral("已设置蓝屏停止代码: %1\n下次触发蓝屏时将显示此代码").arg(stopCode));
    }
}

void ToolsPage::onBSODAuto()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要触发自动蓝屏吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().bsodAuto();
}

void ToolsPage::onBSODRainbow()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要触发彩虹蓝屏吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().bsodRainbow();
}

void ToolsPage::onSetBSODColor()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    QColor color = QColorDialog::getColor(Qt::blue, this, QStringLiteral("选择蓝屏颜色"));
    if (!color.isValid()) return;

    BsodColorConfig cfg;
    cfg.backgroundRed = color.red();
    cfg.backgroundGreen = color.green();
    cfg.backgroundBlue = color.blue();

    if (!ctx.driver().bsodSetColor(cfg)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("设置蓝屏颜色失败"));
    }
}

void ToolsPage::onHaltAllCPUs()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要停机(所有CPU)吗?系统将停止响应!"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().haltAllCPUs();
}

void ToolsPage::onPowerOff()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要断电吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().halPowerControl(HalPower_PowerOff);
}

void ToolsPage::onForceReboot()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要断电并重启吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().halPowerControl(HalPower_ForceReboot);
}

// ===== 系统功能开关 (设置子菜单) =====
void ToolsPage::onDisEnaLoadDriver(bool checked)
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().disableLoadDriver(checked);
}

void ToolsPage::onDisEnaCreateProcess(bool checked)
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().disableCreateProcess(checked);
}

void ToolsPage::onDisEnaRegisterControl(bool checked)
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().disableRegisterControl(checked);
}

void ToolsPage::onDisEnaBSOD(bool checked)
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;
    ctx.driver().hookDisableBSOD(checked ? 1 : 0);
}

// ===== 驱动/设备枚举 =====
void ToolsPage::onEnumDrivers()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    m_enumMode = EnumMode::Drivers;
    QStringList drivers = ctx.driver().enumDrivers();
    m_driverTable->setColumnCount(1);
    m_driverTable->setHorizontalHeaderLabels({QStringLiteral("驱动名称")});
    m_driverTable->setRowCount(drivers.size());
    for (int i = 0; i < drivers.size(); ++i) {
        m_driverTable->setItem(i, 0, new QTableWidgetItem(drivers[i]));
    }
}

void ToolsPage::onEnumDevices()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    m_enumMode = EnumMode::Devices;
    QStringList devices = ctx.driver().enumDevices();
    m_driverTable->setColumnCount(1);
    m_driverTable->setHorizontalHeaderLabels({QStringLiteral("设备名称")});
    m_driverTable->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        m_driverTable->setItem(i, 0, new QTableWidgetItem(devices[i]));
    }
}

void ToolsPage::onEnumDriversPath()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    m_enumMode = EnumMode::DriversPath;
    QStringList paths = ctx.driver().enumDriversPath();
    m_driverTable->setColumnCount(1);
    m_driverTable->setHorizontalHeaderLabels({QStringLiteral("驱动路径")});
    m_driverTable->setRowCount(paths.size());
    for (int i = 0; i < paths.size(); ++i) {
        m_driverTable->setItem(i, 0, new QTableWidgetItem(paths[i]));
    }
}

// 强制卸载驱动 - 优先使用列表选中项的名称，否则提示输入
void ToolsPage::onForceUnloadDriver()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    QString name = currentItemName();
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("强制卸载驱动"),
            QStringLiteral("驱动名:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("强制卸载驱动可能导致系统蓝屏!确定卸载: %1 吗?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().forceUnloadDriver(name)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("卸载失败"));
    }
}

// 强制卸载设备 - 优先使用列表选中项的名称，否则提示输入
void ToolsPage::onForceUnloadDevice()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    QString name = currentItemName();
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("强制卸载设备"),
            QStringLiteral("设备名:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;
    }

    QStringList ops = { QStringLiteral("IoDeleteDevice"), QStringLiteral("UnloadDriver") };
    bool ok = false;
    QString opStr = QInputDialog::getItem(this, QStringLiteral("强制卸载设备"),
        QStringLiteral("卸载方式:"), ops, 0, false, &ok);
    if (!ok) return;
    qint16 op = (qint16)(opStr == QStringLiteral("IoDeleteDevice") ? 0 : 1);

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("强制卸载设备可能导致系统蓝屏!确定卸载: %1 吗?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().forceUnloadDevice(name, op)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("卸载失败"));
    }
}

// ===== 端口 I/O =====
void ToolsPage::onPortPowerOff()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要端口断电吗?系统将立即断电!"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    ctx.driver().portPowerOff();
}

void ToolsPage::onPortKillBIOSPwd()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定要清除 BIOS 密码吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().portKillBIOSPassword()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("清除失败"));
    }
}

void ToolsPage::onPortDisEnaKeyboard()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    // 切换状态
    static bool enabled = true;
    enabled = !enabled;
    ctx.driver().portDisEnaKeyboard(enabled);
}

void ToolsPage::onPortDisEnaMouse()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    static bool enabled = true;
    enabled = !enabled;
    ctx.driver().portDisEnaPS2Mouse(enabled);
}

void ToolsPage::onPortReadCMOS()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    // CMOS 地址按十六进制解析（0x00-0xFF）
    QString addrStr = m_cmosAddr->text().trimmed();
    addrStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    bool ok = false;
    int addr = addrStr.toUInt(&ok, 16);
    if (!ok || addr < 0 || addr > 0xFF) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的地址"));
        return;
    }

    qint32 data = ctx.driver().portReadCMOS(addr);
    if (data >= 0) {
        m_cmosData->setText(QStringLiteral("0x%1").arg(data, 2, 16, QChar('0')));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("读取失败"));
    }
}

void ToolsPage::onPortWriteCMOS()
{
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) return;

    // CMOS 地址和数据都按十六进制解析
    QString addrStr = m_cmosAddr->text().trimmed();
    addrStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    QString dataStr = m_cmosData->text().trimmed();
    dataStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);

    bool ok1 = false, ok2 = false;
    int addr = addrStr.toUInt(&ok1, 16);
    int data = dataStr.toUInt(&ok2, 16);
    if (!ok1 || !ok2 || addr < 0 || addr > 0xFF || data < 0 || data > 0xFF) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的地址或数据"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("写入 CMOS 可能导致系统无法启动!确定继续吗?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().portWriteCMOS(addr, data)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("写入失败"));
    }
}

// 打开 DiskHex 磁盘扇区编辑器（独立窗口）
void ToolsPage::onDiskHex()
{
    auto* dlg = new DiskHexDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // 以独立窗口形式显示（有自己的任务栏图标）
    dlg->setWindowFlag(Qt::Window);
    dlg->show();
}
