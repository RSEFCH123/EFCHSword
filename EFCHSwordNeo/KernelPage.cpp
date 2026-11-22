#include "KernelPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QBrush>
#include <QTimer>
#include <QKeySequenceEdit>
#include <QFileInfo>
#include <memory>

#include <windows.h>
#include <tlhelp32.h>

// ==================== 辅助 ====================

static QString hexAddr(quint64 v)
{
    return v ? QStringLiteral("0x%1").arg(v, 0, 16, QChar('0')) : QStringLiteral("-");
}

static QString nsJoinPath(const QString& parent, const QString& name);
static constexpr int kNsPathRole = Qt::UserRole;
static constexpr int kNsLoadedRole = Qt::UserRole + 1;

// 虚拟键码 -> 可读键名（注册热键显示用）
static QString vkDisplayName(quint32 vk)
{
    if (vk == 0) return QStringLiteral("<无>");
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    wchar_t keyBuf[64] = {0};
    if (sc) {
        LONG lParam = (LONG)((LONG_PTR)sc << 16);
        if (GetKeyNameTextW(lParam, keyBuf, 64) > 0) {
            return QString::fromWCharArray(keyBuf);
        }
    }
    return QStringLiteral("VK 0x%1").arg(vk, 2, 16, QChar('0'));
}

// 修饰键位 -> 显示文本
static QString modifiersDisplayName(quint32 mods)
{
    QStringList parts;
    if (mods & 0x2) parts << QStringLiteral("Ctrl");
    if (mods & 0x1) parts << QStringLiteral("Alt");
    if (mods & 0x4) parts << QStringLiteral("Shift");
    if (mods & 0x8) parts << QStringLiteral("Win");
    if (mods & 0x4000) parts << QStringLiteral("(不重复)");
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" + "));
}

// 设备类型 -> 名称
static QString deviceTypeName(quint32 t)
{
    static const struct { quint32 v; const char* s; } kMap[] = {
        {0x00000001, "BEEP"}, {0x00000002, "CD_ROM"}, {0x00000003, "CD_ROM_FILE_SYSTEM"},
        {0x00000004, "CONTROLLER"}, {0x00000005, "DATALINK"}, {0x00000006, "DFS"},
        {0x00000007, "DISK"}, {0x00000008, "DISK_FILE_SYSTEM"}, {0x00000009, "FILE_SYSTEM"},
        {0x0000000a, "INPORT_PORT"}, {0x0000000b, "KEYBOARD"}, {0x0000000c, "MAILSLOT"},
        {0x0000000d, "MIDI_IN"}, {0x0000000e, "MIDI_OUT"}, {0x0000000f, "MOUSE"},
        {0x00000010, "MULTI_UNC_PROVIDER"}, {0x00000011, "NAMED_PIPE"}, {0x00000012, "NETWORK"},
        {0x00000013, "NETWORK_BROWSER"}, {0x00000014, "NETWORK_FILE_SYSTEM"},
        {0x00000015, "NULL"}, {0x00000016, "PARALLEL_PORT"}, {0x00000017, "PHYSICAL_NETCARD"},
        {0x00000018, "PRINTER"}, {0x00000019, "SCANNER"}, {0x0000001a, "SERIAL_MOUSE_PORT"},
        {0x0000001b, "SERIAL_PORT"}, {0x0000001c, "SCREEN"}, {0x0000001d, "SOUND"},
        {0x0000001e, "STREAMS"}, {0x0000001f, "TAPE"}, {0x00000020, "TAPE_FILE_SYSTEM"},
        {0x00000021, "TRANSPORT"}, {0x00000022, "UNKNOWN"}, {0x00000023, "VIDEO"},
        {0x00000024, "VIRTUAL_DISK"}, {0x00000025, "WAVE_IN"}, {0x00000026, "WAVE_OUT"},
        {0x00000027, "8042_PORT"}, {0x00000028, "NETWORK_REDIRECTOR"},
        {0x00000029, "BATTERY"}, {0x0000002a, "BUS_EXTENDER"}, {0x0000002b, "MODEM"},
        {0x0000002c, "VDM"}, {0x0000002d, "MASS_STORAGE"}, {0x0000002e, "SMB"},
        {0x0000002f, "KS"}, {0x00000030, "CHANGER"}, {0x00000031, "SMARTCARD"},
        {0x00000032, "ACPI"}, {0x00000033, "DVD"}, {0x00000034, "FULLSCREEN_VIDEO"},
        {0x00000035, "FILESYSTEM"}, {0x00000036, "INPUT_PORT"}, {0x00000037, "PRINTER_PORT"},
        {0x00000038, "TERMINAL_SERVER"}, {0x00000039, "MIDI_IN-2"}, {0x0000003a, "MIDI_OUT-2"},
        {0x0000003b, "GPIO"}, {0x0000003c, "USBEX"}, {0x0000003d, "VIDEO-2"},
        {0x0000003e, "VIDEO-3"}, {0x0000003f, "KSEC"}, {0x00000400, "NETWORK-2"},
        {0x00008000, "VM"}, {0x00008001, "VM-2"}, {0x00008002, "VM-3"},
    };
    for (const auto& m : kMap) {
        if (m.v == t) return QString::fromLatin1(m.s);
    }
    return QString::number(t);
}

// IRP MajorFunction 索引 -> 名称
static const char* irpMajorName(ULONG i)
{
    static const char* kNames[28] = {
        "IRP_MJ_CREATE", "IRP_MJ_CREATE_NAMED_PIPE", "IRP_MJ_CLOSE", "IRP_MJ_READ",
        "IRP_MJ_WRITE", "IRP_MJ_QUERY_INFORMATION", "IRP_MJ_SET_INFORMATION",
        "IRP_MJ_QUERY_VOLUME_INFORMATION", "IRP_MJ_SET_VOLUME_INFORMATION",
        "IRP_MJ_DIRECTORY_CONTROL", "IRP_MJ_FILE_SYSTEM_CONTROL", "IRP_MJ_DEVICE_CONTROL",
        "IRP_MJ_INTERNAL_DEVICE_CONTROL", "IRP_MJ_SHUTDOWN", "IRP_MJ_LOCK_CONTROL",
        "IRP_MJ_CLEANUP", "IRP_MJ_CREATE_MAILSLOT", "IRP_MJ_QUERY_SECURITY",
        "IRP_MJ_SET_SECURITY", "IRP_MJ_POWER", "IRP_MJ_SYSTEM_CONTROL",
        "IRP_MJ_DEVICE_CHANGE", "IRP_MJ_QUERY_QUOTA", "IRP_MJ_SET_QUOTA",
        "IRP_MJ_PNP", "IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION",
        "IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION", "IRP_MJ_ACQUIRE_FOR_MOD_WRITE",
    };
    return (i < 28) ? kNames[i] : "?";
}

// 显示文本详情对话框
static void showTextDetail(QWidget* parent, const QString& title, const QString& text)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(640, 480);
    auto* layout = new QVBoxLayout(&dlg);
    auto* edit = new QPlainTextEdit(&dlg);
    edit->setPlainText(text);
    edit->setReadOnly(true);
    layout->addWidget(edit);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);
    dlg.exec();
}

// ==================== 页面 ====================

KernelPage::KernelPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

KernelPage::~KernelPage() = default;

bool KernelPage::requireDriver()
{
    if (AppContext::instance().driverLoaded()) return true;
    QMessageBox::information(this, QStringLiteral("内核"),
        QStringLiteral("驱动未加载, 该功能需要内核驱动"));
    return false;
}

void KernelPage::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(createDriverTab(), QStringLiteral("驱动管理"));
    tabs->addTab(createDeviceTab(), QStringLiteral("驱动设备管理"));
    tabs->addTab(createModuleTab(), QStringLiteral("驱动列表管理"));
    tabs->addTab(createCidTab(), QStringLiteral("PspCidTable"));
    tabs->addTab(createHotkeyTab(), QStringLiteral("注册热键"));
    tabs->addTab(createNsTab(), QStringLiteral("命名空间管理"));
    tabs->addTab(createSysViewTab(), QStringLiteral("系统视图"));
    layout->addWidget(tabs);
}

// ==================== 驱动管理 ====================

QWidget* KernelPage::createDriverTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新驱动对象"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onRefreshDrivers);
    toolRow->addWidget(refreshBtn);
    toolRow->addWidget(new QLabel(QStringLiteral("过滤:"), tab));
    m_driverFilterEdit = new QLineEdit(tab);
    m_driverFilterEdit->setPlaceholderText(QStringLiteral("输入驱动名过滤"));
    m_driverFilterEdit->setMaximumWidth(200);
    connect(m_driverFilterEdit, &QLineEdit::textChanged, this, &KernelPage::onRefreshDrivers);
    toolRow->addWidget(m_driverFilterEdit);
    toolRow->addStretch();
    toolRow->addWidget(m_driverVersionLabel =
        new QLabel(QStringLiteral("驱动版本: -"), tab));
    layout->addLayout(toolRow);

    m_driverTable = new QTableWidget(tab);
    m_driverTable->setColumnCount(4);
    m_driverTable->setHorizontalHeaderLabels({
        QStringLiteral("驱动名称"), QStringLiteral("DRIVER_OBJECT"),
        QStringLiteral("镜像基址"), QStringLiteral("镜像大小")});
    m_driverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_driverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_driverTable->setAlternatingRowColors(true);
    m_driverTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_driverTable->verticalHeader()->setVisible(false);
    m_driverTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_driverTable, &QTableWidget::customContextMenuRequested,
            this, &KernelPage::onDriverContextMenu);
    layout->addWidget(m_driverTable, 1);

    m_driverStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_driverStatusLabel);
    return tab;
}

void KernelPage::onRefreshDrivers()
{
    m_driverTable->setRowCount(0);
    if (!requireDriver()) return;

    ArkDriverObjArray arr;
    auto& drv = AppContext::instance().driver();
    if (m_driverVersionLabel) {
        quint32 ver = drv.getDriverVersion();
        // 0 means the IOCTL is unsupported/failed (typically an old
        // resident driver), not a real version.  Showing "0" made the UI
        // appear to randomly switch between 0 and the current build.
        m_driverVersionLabel->setText(
            ver == 0 ? QStringLiteral("驱动版本: 未知 (旧版/不兼容)")
                     : QStringLiteral("驱动版本: %1").arg(ver));
    }
    if (!drv.arkEnumDriverObjects(arr)) {
        m_driverStatusLabel->setText(QStringLiteral("枚举驱动对象失败 (错误码: %1)")
            .arg(drv.lastIoError()));
        return;
    }
    m_driverEntries.clear();

    QString filter = m_driverFilterEdit ? m_driverFilterEdit->text().trimmed() : QString();
    if (!filter.isEmpty()) filter.prepend('\\');

    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkDriverObjEntry& e = arr.entries[i];
        QString name = QString::fromWCharArray(e.name);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)) continue;
        m_driverEntries.append(e);

        int r = m_driverTable->rowCount();
        m_driverTable->insertRow(r);
        m_driverTable->setItem(r, 0, new QTableWidgetItem(name));
        m_driverTable->setItem(r, 1, new QTableWidgetItem(hexAddr(e.driverObject)));
        m_driverTable->setItem(r, 2, new QTableWidgetItem(hexAddr(e.imageBase)));
        m_driverTable->setItem(r, 3, new QTableWidgetItem(
            QStringLiteral("0x%1 (%2 KB)").arg(e.imageSize, 0, 16).arg(e.imageSize / 1024)));
    }
    m_driverStatusLabel->setText(QStringLiteral("共 %1 个驱动对象").arg(m_driverTable->rowCount()));
}

void KernelPage::onDriverContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_driverTable->indexAt(pos);
    if (!idx.isValid()) return;
    m_driverTable->selectRow(idx.row());

    QMenu menu(this);
    QAction* detailAction = menu.addAction(QStringLiteral("驱动详细信息"));
    connect(detailAction, &QAction::triggered, this, &KernelPage::onDriverDetail);
    QAction* unloadAction = menu.addAction(QStringLiteral("强制卸载该驱动"));
    connect(unloadAction, &QAction::triggered, this, &KernelPage::onDriverUnload);
    menu.addSeparator();
    QAction* copyNameAction = menu.addAction(QStringLiteral("复制驱动名"));
    connect(copyNameAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_driverTable->item(m_driverTable->currentRow(), 0))
            QGuiApplication::clipboard()->setText(it->text());
    });
    QAction* copyAddrAction = menu.addAction(QStringLiteral("复制 DRIVER_OBJECT 地址"));
    connect(copyAddrAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_driverTable->item(m_driverTable->currentRow(), 1))
            QGuiApplication::clipboard()->setText(it->text());
    });
    menu.exec(m_driverTable->viewport()->mapToGlobal(pos));
}

void KernelPage::onDriverUnload()
{
    int row = m_driverTable->currentRow();
    if (row < 0 || row >= m_driverEntries.size()) return;
    if (!requireDriver()) return;

    QString name = QString::fromWCharArray(m_driverEntries[row].name);
    auto ret = QMessageBox::warning(this, QStringLiteral("驱动管理"),
        QStringLiteral("确定强制卸载驱动: %1 吗?\n\n"
                       "警告: 强制卸载内核驱动可导致系统不稳定甚至蓝屏, "
                       "若该驱动没有卸载例程则无法卸载!").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.forceUnloadDriver(name)) {
        m_driverStatusLabel->setText(QStringLiteral("卸载指令已发送: %1").arg(name));
        QTimer::singleShot(1000, this, &KernelPage::onRefreshDrivers);
    } else {
        QMessageBox::warning(this, QStringLiteral("驱动管理"),
            QStringLiteral("卸载失败 (错误码: %1)\n"
                           "驱动可能没有 DriverUnload 例程, 或被系统/其他驱动引用。")
                .arg(drv.lastIoError()));
    }
}

void KernelPage::onDriverDetail()
{
    int row = m_driverTable->currentRow();
    if (row < 0 || row >= m_driverEntries.size()) return;
    if (!requireDriver()) return;

    quint64 drvObj = m_driverEntries[row].driverObject;
    ArkDriverDetail d;
    auto& drv = AppContext::instance().driver();
    if (!drv.arkGetDriverDetail(drvObj, d)) {
        QMessageBox::warning(this, QStringLiteral("驱动管理"),
            QStringLiteral("获取驱动详情失败 (错误码: %1)").arg(drv.lastIoError()));
        return;
    }

    QString text;
    text += QStringLiteral("驱动名称: %1\n").arg(QString::fromWCharArray(d.name));
    text += QStringLiteral("DRIVER_OBJECT: %1\n").arg(hexAddr(d.driverObject));
    text += QStringLiteral("标志: 0x%1\n").arg(d.flags, 8, 16, QChar('0'));
    text += QStringLiteral("镜像路径: %1\n").arg(QString::fromWCharArray(d.imagePath));
    text += QStringLiteral("镜像基址: %1  大小: 0x%2\n").arg(hexAddr(d.imageBase)).arg(d.imageSize, 0, 16);
    text += QStringLiteral("DriverInit: %1\n").arg(hexAddr(d.driverInit));
    text += QStringLiteral("DriverStartIo: %1\n").arg(hexAddr(d.driverStartIo));
    text += QStringLiteral("DriverUnload: %1\n").arg(hexAddr(d.driverUnload));
    text += QStringLiteral("\n===== MajorFunction 分发表 =====\n");
    for (ULONG i = 0; i < 28; ++i) {
        text += QStringLiteral("[%1] %2: %3\n")
            .arg(i, 2, 10, QChar('0'))
            .arg(QString::fromLatin1(irpMajorName(i)), -38)
            .arg(hexAddr(d.majorFunction[i]));
    }
    showTextDetail(this, QStringLiteral("驱动详细信息 - %1")
        .arg(QString::fromWCharArray(d.name)), text);
}

// ==================== 设备管理 ====================

QWidget* KernelPage::createDeviceTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新设备对象"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onRefreshDevices);
    toolRow->addWidget(refreshBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    auto* hint = new QLabel(
        QStringLiteral("枚举内核设备对象 (DEVICE_OBJECT), 含未命名设备; 右键查看设备详情。"), tab);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_deviceTable = new QTableWidget(tab);
    m_deviceTable->setColumnCount(8);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("DEVICE_OBJECT"), QStringLiteral("驱动名称"), QStringLiteral("设备类型"),
        QStringLiteral("特征"), QStringLiteral("标志"), QStringLiteral("栈大小"),
        QStringLiteral("附加设备"), QStringLiteral("设备扩展")});
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_deviceTable, &QTableWidget::customContextMenuRequested,
            this, &KernelPage::onDeviceContextMenu);
    layout->addWidget(m_deviceTable, 1);

    m_deviceStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_deviceStatusLabel);
    return tab;
}

void KernelPage::onRefreshDevices()
{
    m_deviceTable->setRowCount(0);
    if (!requireDriver()) return;

    ArkDeviceArray arr;
    auto& drv = AppContext::instance().driver();
    if (!drv.arkEnumDeviceObjects(arr)) {
        m_deviceStatusLabel->setText(QStringLiteral("枚举设备对象失败 (错误码: %1)")
            .arg(drv.lastIoError()));
        return;
    }
    m_deviceEntries.clear();

    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkDeviceEntry& e = arr.entries[i];
        m_deviceEntries.append(e);
        int r = m_deviceTable->rowCount();
        m_deviceTable->insertRow(r);
        m_deviceTable->setItem(r, 0, new QTableWidgetItem(hexAddr(e.deviceObject)));
        m_deviceTable->setItem(r, 1, new QTableWidgetItem(QString::fromWCharArray(e.driverName)));
        m_deviceTable->setItem(r, 2, new QTableWidgetItem(deviceTypeName(e.deviceType)));
        m_deviceTable->setItem(r, 3, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(e.characteristics, 8, 16, QChar('0'))));
        m_deviceTable->setItem(r, 4, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(e.flags, 8, 16, QChar('0'))));
        m_deviceTable->setItem(r, 5, new QTableWidgetItem(QString::number(e.stackSize)));
        m_deviceTable->setItem(r, 6, new QTableWidgetItem(hexAddr(e.attachedDevice)));
        m_deviceTable->setItem(r, 7, new QTableWidgetItem(hexAddr(e.deviceExtension)));
    }
    m_deviceStatusLabel->setText(
        QStringLiteral("共 %1 个设备对象 (上限 1024, 实际发现 %2)")
            .arg(m_deviceTable->rowCount()).arg(arr.totalFound));
}

void KernelPage::onDeviceContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_deviceTable->indexAt(pos);
    if (!idx.isValid()) return;
    m_deviceTable->selectRow(idx.row());

    QMenu menu(this);
    QAction* detailAction = menu.addAction(QStringLiteral("设备详细信息"));
    connect(detailAction, &QAction::triggered, this, &KernelPage::onDeviceDetail);
    QAction* unloadDrvAction = menu.addAction(QStringLiteral("卸载所属驱动"));
    connect(unloadDrvAction, &QAction::triggered, this, &KernelPage::onDeviceUnloadDriver);
    QAction* deleteDevAction = menu.addAction(QStringLiteral("删除设备 (IoDeleteDevice)"));
    connect(deleteDevAction, &QAction::triggered, this, &KernelPage::onDeviceDelete);
    menu.addSeparator();
    QAction* copyAction = menu.addAction(QStringLiteral("复制 DEVICE_OBJECT 地址"));
    connect(copyAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_deviceTable->item(m_deviceTable->currentRow(), 0))
            QGuiApplication::clipboard()->setText(it->text());
    });
    menu.exec(m_deviceTable->viewport()->mapToGlobal(pos));
}

void KernelPage::onDeviceUnloadDriver()
{
    int row = m_deviceTable->currentRow();
    if (row < 0 || row >= m_deviceEntries.size()) return;
    if (!requireDriver()) return;

    QString driverName = QString::fromWCharArray(m_deviceEntries[row].driverName);
    if (driverName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("无法确定该设备所属驱动的名称"));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
        QStringLiteral("确定强制卸载设备所属驱动: %1 吗?\n\n"
                       "警告: 强制卸载内核驱动可导致系统不稳定甚至蓝屏!").arg(driverName),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.forceUnloadDriver(driverName)) {
        m_deviceStatusLabel->setText(QStringLiteral("卸载指令已发送: %1").arg(driverName));
        QTimer::singleShot(1000, this, &KernelPage::onRefreshDevices);
    } else {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("卸载失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onDeviceDelete()
{
    int row = m_deviceTable->currentRow();
    if (row < 0 || row >= m_deviceEntries.size()) return;
    if (!requireDriver()) return;

    quint64 devObj = m_deviceEntries[row].deviceObject;

    // 删除设备按名字进行 (IoGetDeviceObjectPointer 解析), 未命名设备无法处理
    ArkDeviceDetail d;
    auto& drv = AppContext::instance().driver();
    if (!drv.arkGetDeviceDetail(devObj, d)) {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("查询设备名称失败 (错误码: %1)").arg(drv.lastIoError()));
        return;
    }
    QString deviceName = QString::fromWCharArray(d.deviceName);
    if (deviceName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("该设备是未命名设备 (没有 \\\\Device\\ 路径), 无法按名删除。\n"
                           "只能通过\"卸载所属驱动\"来处理。"));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
        QStringLiteral("确定删除设备: %1 吗?\n\n"
                       "警告: 删除设备对象可导致其属主驱动与系统异常, 甚至蓝屏!")
            .arg(deviceName),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    // operation: 0 = IoDeleteDevice
    if (drv.forceUnloadDevice(deviceName, 0)) {
        m_deviceStatusLabel->setText(QStringLiteral("删除指令已发送: %1").arg(deviceName));
        QTimer::singleShot(1000, this, &KernelPage::onRefreshDevices);
    } else {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("删除失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onDeviceDetail()
{
    int row = m_deviceTable->currentRow();
    if (row < 0 || row >= m_deviceEntries.size()) return;
    if (!requireDriver()) return;

    quint64 devObj = m_deviceEntries[row].deviceObject;
    ArkDeviceDetail d;
    auto& drv = AppContext::instance().driver();
    if (!drv.arkGetDeviceDetail(devObj, d)) {
        QMessageBox::warning(this, QStringLiteral("驱动设备管理"),
            QStringLiteral("获取设备详情失败 (错误码: %1)").arg(drv.lastIoError()));
        return;
    }

    QString text;
    text += QStringLiteral("设备名称: %1\n").arg(QString::fromWCharArray(d.deviceName));
    text += QStringLiteral("所属驱动: %1 (DRIVER_OBJECT: %2)\n")
        .arg(QString::fromWCharArray(d.driverName)).arg(hexAddr(d.driverObject));
    text += QStringLiteral("DEVICE_OBJECT: %1\n").arg(hexAddr(d.deviceObject));
    text += QStringLiteral("设备类型: %1 (0x%2)\n")
        .arg(deviceTypeName(d.deviceType)).arg(d.deviceType, 8, 16, QChar('0'));
    text += QStringLiteral("特征: 0x%1\n").arg(d.characteristics, 8, 16, QChar('0'));
    text += QStringLiteral("标志: 0x%1\n").arg(d.flags, 8, 16, QChar('0'));
    text += QStringLiteral("栈大小: %1\n").arg(d.stackSize);
    text += QStringLiteral("附加设备 (AttachedDevice): %1\n").arg(hexAddr(d.attachedDevice));
    text += QStringLiteral("设备扩展: %1\n").arg(hexAddr(d.deviceExtension));
    showTextDetail(this, QStringLiteral("设备详细信息"), text);
}

// ==================== 驱动列表(内核模块) ====================

QWidget* KernelPage::createModuleTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新内核模块"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onRefreshModules);
    toolRow->addWidget(refreshBtn);
    toolRow->addWidget(new QLabel(QStringLiteral("过滤:"), tab));
    m_moduleFilterEdit = new QLineEdit(tab);
    m_moduleFilterEdit->setPlaceholderText(QStringLiteral("输入路径/名称过滤"));
    m_moduleFilterEdit->setMaximumWidth(260);
    connect(m_moduleFilterEdit, &QLineEdit::textChanged, this, &KernelPage::onRefreshModules);
    toolRow->addWidget(m_moduleFilterEdit);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    m_moduleTable = new QTableWidget(tab);
    m_moduleTable->setColumnCount(3);
    m_moduleTable->setHorizontalHeaderLabels({
        QStringLiteral("模块路径"), QStringLiteral("基址"), QStringLiteral("大小")});
    m_moduleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_moduleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_moduleTable->setAlternatingRowColors(true);
    m_moduleTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_moduleTable->verticalHeader()->setVisible(false);
    m_moduleTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_moduleTable, &QTableWidget::customContextMenuRequested, this,
            &KernelPage::onModuleContextMenu);
    layout->addWidget(m_moduleTable, 1);

    m_moduleStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_moduleStatusLabel);
    return tab;
}

void KernelPage::onModuleContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_moduleTable->indexAt(pos);
    if (!idx.isValid()) return;
    m_moduleTable->selectRow(idx.row());

    QMenu menu(this);
    QAction* unloadAction = menu.addAction(QStringLiteral("强制卸载该模块驱动"));
    connect(unloadAction, &QAction::triggered, this, &KernelPage::onModuleUnload);
    menu.addSeparator();
    QAction* copyAction = menu.addAction(QStringLiteral("复制模块路径"));
    connect(copyAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_moduleTable->item(m_moduleTable->currentRow(), 0))
            QGuiApplication::clipboard()->setText(it->text());
    });
    menu.exec(m_moduleTable->viewport()->mapToGlobal(pos));
}

void KernelPage::onModuleUnload()
{
    int row = m_moduleTable->currentRow();
    if (row < 0 || row >= m_moduleTable->rowCount()) return;
    if (!requireDriver()) return;

    auto* pathItem = m_moduleTable->item(row, 0);
    if (!pathItem) return;

    QString path = pathItem->text();
    QString name = QFileInfo(path).baseName();   // beep.sys -> beep
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("驱动列表管理"),
            QStringLiteral("无法从路径解析模块名: %1").arg(path));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("驱动列表管理"),
        QStringLiteral("确定强制卸载模块: %1 (%2) 吗?\n\n"
                       "警告: ntoskrnl/HAL 等系统核心模块不可卸载; "
                       "强制卸载可导致蓝屏!").arg(name).arg(path),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.forceUnloadDriver(name)) {
        m_moduleStatusLabel->setText(QStringLiteral("卸载指令已发送: %1").arg(name));
    } else {
        QMessageBox::warning(this, QStringLiteral("驱动列表管理"),
            QStringLiteral("卸载失败 (错误码: %1)\n"
                           "注意: 驱动对象名与文件名可能不一致, "
                           "可在\"驱动管理\"页按驱动对象名重试。").arg(drv.lastIoError()));
    }
}

void KernelPage::onRefreshModules()
{
    m_moduleTable->setRowCount(0);
    if (!requireDriver()) return;

    // ArkModuleArray 约 268KB, 堆分配避免逼近 1MB 线程栈上限
    auto arr = std::make_unique<ArkModuleArray>();
    auto& drv = AppContext::instance().driver();
    if (!drv.arkEnumKernelModules(*arr)) {
        m_moduleStatusLabel->setText(QStringLiteral("枚举内核模块失败 (错误码: %1)")
            .arg(drv.lastIoError()));
        return;
    }

    QString filter = m_moduleFilterEdit ? m_moduleFilterEdit->text().trimmed() : QString();
    int shown = 0;
    for (quint32 i = 0; i < arr->count && i < 512; ++i) {
        const ArkModuleEntry& e = arr->entries[i];
        QString path = QString::fromWCharArray(e.path);
        if (!filter.isEmpty() && !path.contains(filter, Qt::CaseInsensitive)) continue;
        int r = m_moduleTable->rowCount();
        m_moduleTable->insertRow(r);
        m_moduleTable->setItem(r, 0, new QTableWidgetItem(path));
        m_moduleTable->setItem(r, 1, new QTableWidgetItem(hexAddr(e.base)));
        m_moduleTable->setItem(r, 2, new QTableWidgetItem(
            QStringLiteral("0x%1 (%2 KB)").arg(e.size, 0, 16).arg(e.size / 1024)));
        ++shown;
    }
    m_moduleStatusLabel->setText(
        QStringLiteral("显示 %1 / %2 个内核模块").arg(shown).arg(arr->totalFound));
}

// ==================== PspCidTable ====================

QWidget* KernelPage::createCidTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    m_cidModeCombo = new QComboBox(tab);
    m_cidModeCombo->addItem(QStringLiteral("进程视图 (PspCidTable)"));
    m_cidModeCombo->addItem(QStringLiteral("线程视图 (PspCidTable)"));
    connect(m_cidModeCombo, &QComboBox::currentIndexChanged,
            this, &KernelPage::onCidModeChanged);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onCidRefresh);
    toolRow->addWidget(m_cidModeCombo);
    toolRow->addWidget(refreshBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    auto* hint = new QLabel(
        QStringLiteral("通过 PspCidTable 枚举系统全部进程/线程, 可发现断链/摘除 PID 隐藏的对象。\n"
                       "与常规枚举对比后标记 [隐藏]; 终止操作全部使用 APC 注入方式。"), tab);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_cidTable = new QTableWidget(tab);
    m_cidTable->setColumnCount(5);
    m_cidTable->setHorizontalHeaderLabels({
        QStringLiteral("PID"), QStringLiteral("镜像名"), QStringLiteral("线程数"),
        QStringLiteral("EPROCESS"), QStringLiteral("状态")});
    m_cidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_cidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cidTable->setAlternatingRowColors(true);
    m_cidTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_cidTable->verticalHeader()->setVisible(false);
    m_cidTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_cidTable, &QTableWidget::customContextMenuRequested,
            this, &KernelPage::onCidContextMenu);
    layout->addWidget(m_cidTable, 1);

    m_cidStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_cidStatusLabel);

    // 右键菜单在 onCidContextMenu 中按视图模式动态构建
    return tab;
}

void KernelPage::onCidModeChanged(int index)
{
    m_cidThreadFilterPid = 0;
    // 列结构
    if (index == 0) {
        m_cidTable->setColumnCount(5);
        m_cidTable->setHorizontalHeaderLabels({
            QStringLiteral("PID"), QStringLiteral("镜像名"), QStringLiteral("线程数"),
            QStringLiteral("EPROCESS"), QStringLiteral("状态")});
    } else {
        m_cidTable->setColumnCount(6);
        m_cidTable->setHorizontalHeaderLabels({
            QStringLiteral("TID"), QStringLiteral("所属PID"), QStringLiteral("ETHREAD"),
            QStringLiteral("起始地址"), QStringLiteral("系统线程"), QStringLiteral("状态")});
    }
    m_cidTable->setRowCount(0);
    onCidRefresh();
}

void KernelPage::onCidRefresh()
{
    m_cidTable->setRowCount(0);
    m_cidTable->setUpdatesEnabled(false);
    if (!requireDriver()) { m_cidTable->setUpdatesEnabled(true); return; }

    auto& drv = AppContext::instance().driver();
    const bool procMode = (m_cidModeCombo->currentIndex() == 0);

    // 常规枚举集合（用于隐藏检测）
    QSet<quint32> visible;
    if (procMode) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do { visible.insert(pe.th32ProcessID); } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    } else {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te;
            te.dwSize = sizeof(te);
            if (Thread32First(hSnap, &te)) {
                do { visible.insert(te.th32ThreadID); } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }
    }

    if (procMode) {
        // ArkCidProcessArray 约 160KB / ArkCidThreadArray 约 256KB, 堆分配
        auto arr = std::make_unique<ArkCidProcessArray>();
        if (!drv.cidEnumProcesses(*arr)) {
            m_cidTable->setUpdatesEnabled(true);
            m_cidStatusLabel->setText(QStringLiteral("PspCidTable 枚举失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        m_cidProcesses.clear();
        for (quint32 i = 0; i < arr->count && i < 2048; ++i) {
            const ArkCidProcessEntry& e = arr->entries[i];
            m_cidProcesses.append(e);
            int r = m_cidTable->rowCount();
            m_cidTable->insertRow(r);
            m_cidTable->setItem(r, 0, new QTableWidgetItem(QString::number(e.pid)));
            m_cidTable->setItem(r, 1, new QTableWidgetItem(QString::fromWCharArray(e.imageName)));
            m_cidTable->setItem(r, 2, new QTableWidgetItem(QString::number(e.threadCount)));
            m_cidTable->setItem(r, 3, new QTableWidgetItem(hexAddr(e.eprocess)));
            // PID 0 = System Idle, 不在常规列表里但正常
            bool hidden = !visible.contains(e.pid) && e.pid != 0;
            auto* stateItem = new QTableWidgetItem(
                hidden ? QStringLiteral("隐藏") : QStringLiteral("正常"));
            stateItem->setForeground(hidden ? QBrush(Qt::red) : QBrush(Qt::darkGreen));
            m_cidTable->setItem(r, 4, stateItem);
        }
        m_cidStatusLabel->setText(QStringLiteral("PspCidTable 进程: %1 个").arg(m_cidProcesses.size()));
    } else {
        auto arr = std::make_unique<ArkCidThreadArray>();
        if (!drv.cidEnumThreads(*arr)) {
            m_cidTable->setUpdatesEnabled(true);
            m_cidStatusLabel->setText(QStringLiteral("PspCidTable 枚举失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        m_cidThreads.clear();
        for (quint32 i = 0; i < arr->count && i < 8192; ++i) {
            const ArkCidThreadEntry& e = arr->entries[i];
            if (m_cidThreadFilterPid != 0 && e.ownerPid != m_cidThreadFilterPid) continue;
            m_cidThreads.append(e);
            int r = m_cidTable->rowCount();
            m_cidTable->insertRow(r);
            m_cidTable->setItem(r, 0, new QTableWidgetItem(QString::number(e.tid)));
            m_cidTable->setItem(r, 1, new QTableWidgetItem(QString::number(e.ownerPid)));
            m_cidTable->setItem(r, 2, new QTableWidgetItem(hexAddr(e.ethread)));
            m_cidTable->setItem(r, 3, new QTableWidgetItem(hexAddr(e.startAddress)));
            m_cidTable->setItem(r, 4, new QTableWidgetItem(
                (e.flags & 1) ? QStringLiteral("是") : QStringLiteral("否")));
            bool hidden = !visible.contains(e.tid);
            auto* stateItem = new QTableWidgetItem(
                hidden ? QStringLiteral("隐藏") : QStringLiteral("正常"));
            stateItem->setForeground(hidden ? QBrush(Qt::red) : QBrush(Qt::darkGreen));
            m_cidTable->setItem(r, 5, stateItem);
        }
        m_cidStatusLabel->setText(
            m_cidThreadFilterPid
                ? QStringLiteral("进程 %1 的线程: %2 个").arg(m_cidThreadFilterPid).arg(m_cidThreads.size())
                : QStringLiteral("PspCidTable 线程: %1 个").arg(m_cidThreads.size()));
    }
    m_cidTable->setUpdatesEnabled(true);
}

void KernelPage::onCidContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_cidTable->indexAt(pos);
    if (!idx.isValid()) return;
    m_cidTable->selectRow(idx.row());

    // 按当前视图模式只提供有意义的操作：
    //   进程视图 -> 终止进程(APC) / 查看该进程的线程
    //   线程视图 -> 终止线程(APC)
    const bool procMode = (m_cidModeCombo->currentIndex() == 0);
    QMenu menu(this);

    if (procMode) {
        QAction* termProcAction = menu.addAction(QStringLiteral("终止进程 (APC)"));
        connect(termProcAction, &QAction::triggered, this, &KernelPage::onCidTerminateProcess);
        QAction* showThreadsAction = menu.addAction(QStringLiteral("查看该进程的线程"));
        connect(showThreadsAction, &QAction::triggered, this, &KernelPage::onCidShowProcessThreads);
    } else {
        QAction* termThreadAction = menu.addAction(QStringLiteral("终止线程 (APC)"));
        connect(termThreadAction, &QAction::triggered, this, &KernelPage::onCidTerminateThread);
    }
    menu.exec(m_cidTable->viewport()->mapToGlobal(pos));
}

void KernelPage::onCidTerminateProcess()
{
    if (m_cidModeCombo->currentIndex() != 0) return;
    int row = m_cidTable->currentRow();
    if (row < 0 || row >= m_cidProcesses.size()) return;
    quint32 pid = m_cidProcesses[row].pid;

    auto ret = QMessageBox::warning(this, QStringLiteral("PspCidTable"),
        QStringLiteral("确定要 APC 终止进程 PID=%1 吗?\n\n"
                       "警告: 终止系统关键进程会导致蓝屏!").arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.cidTerminateProcess(pid)) {
        m_cidStatusLabel->setText(QStringLiteral("APC 终止指令已发送 (PID=%1)").arg(pid));
        onCidRefresh();
    } else {
        QMessageBox::warning(this, QStringLiteral("PspCidTable"),
            QStringLiteral("终止失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onCidTerminateThread()
{
    if (m_cidModeCombo->currentIndex() != 1) return;
    int row = m_cidTable->currentRow();
    if (row < 0 || row >= m_cidThreads.size()) return;
    quint32 tid = m_cidThreads[row].tid;
    quint32 ownerPid = m_cidThreads[row].ownerPid;

    auto ret = QMessageBox::warning(this, QStringLiteral("PspCidTable"),
        QStringLiteral("确定要 APC 终止线程 TID=%1 (PID=%2) 吗?\n\n"
                       "警告: 终止关键线程可能导致蓝屏!").arg(tid).arg(ownerPid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.cidTerminateThread(tid)) {
        m_cidStatusLabel->setText(QStringLiteral("APC 终止指令已发送 (TID=%1)").arg(tid));
        onCidRefresh();
    } else {
        QMessageBox::warning(this, QStringLiteral("PspCidTable"),
            QStringLiteral("终止失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onCidShowProcessThreads()
{
    if (m_cidModeCombo->currentIndex() != 0) return;
    int row = m_cidTable->currentRow();
    if (row < 0 || row >= m_cidProcesses.size()) return;
    quint32 pid = m_cidProcesses[row].pid;
    m_cidThreadFilterPid = pid;
    m_cidModeCombo->setCurrentIndex(1);   // 触发 onCidModeChanged -> 刷新
}

// ==================== 注册热键 ====================
//
// 显示当前会话内所有进程通过 RegisterHotKey 注册的全局热键（读取 win32k
// 的 gphkHashTable），并解析出注册进程。取代旧的内核键盘 Hook 记录页。

QWidget* KernelPage::createHotkeyTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新热键"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onRefreshHotkeys);
    toolRow->addWidget(refreshBtn);
    toolRow->addWidget(new QLabel(QStringLiteral("过滤:"), tab));
    m_hotkeyFilterEdit = new QLineEdit(tab);
    m_hotkeyFilterEdit->setPlaceholderText(QStringLiteral("进程名 / 按键 / 热键组合"));
    m_hotkeyFilterEdit->setMaximumWidth(260);
    connect(m_hotkeyFilterEdit, &QLineEdit::textChanged, this, &KernelPage::onRefreshHotkeys);
    toolRow->addWidget(m_hotkeyFilterEdit);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    auto* hint = new QLabel(
        QStringLiteral("枚举当前会话内所有进程通过 RegisterHotKey 注册的全局热键 (读取 win32k 热键表)。\n"
                       "提示: 恶意程序会大量注册单键热键来 Hook 键盘输入, 若发现大量无修饰键的单键热键需警惕。"), tab);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_hotkeyTable = new QTableWidget(tab);
    m_hotkeyTable->setColumnCount(6);
    m_hotkeyTable->setHorizontalHeaderLabels({
        QStringLiteral("热键组合"), QStringLiteral("按键"), QStringLiteral("虚拟键"),
        QStringLiteral("修饰键"), QStringLiteral("ID"), QStringLiteral("注册进程")});
    m_hotkeyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hotkeyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hotkeyTable->setAlternatingRowColors(true);
    m_hotkeyTable->verticalHeader()->setVisible(false);
    m_hotkeyTable->horizontalHeader()->setStretchLastSection(true);
    m_hotkeyTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hotkeyTable, &QTableWidget::customContextMenuRequested,
            this, &KernelPage::onHotkeyContextMenu);
    layout->addWidget(m_hotkeyTable, 1);

    m_hotkeyStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_hotkeyStatusLabel);

    // 创建后自动加载一次
    QTimer::singleShot(0, this, &KernelPage::onRefreshHotkeys);
    return tab;
}

// Qt::Key -> Win32 虚拟键码（RegisterHotKey 使用）
static quint32 qtKeyToVk(int key)
{
    key &= ~Qt::KeyboardModifierMask;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return (quint32)key;          // 与 VK_A..VK_Z 相同
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return (quint32)key;          // 与 VK_0..VK_9 相同
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)                            // F1..F24 -> 0x70..
        return (quint32)(key - Qt::Key_F1 + 0x70);
    switch (key) {
    case Qt::Key_Backspace:  return 0x08;
    case Qt::Key_Tab:        return 0x09;
    case Qt::Key_Return:     return 0x0D;
    case Qt::Key_Pause:      return 0x13;
    case Qt::Key_CapsLock:   return 0x14;
    case Qt::Key_Escape:     return 0x1B;
    case Qt::Key_Space:      return 0x20;
    case Qt::Key_PageUp:     return 0x21;
    case Qt::Key_PageDown:   return 0x22;
    case Qt::Key_End:        return 0x23;
    case Qt::Key_Home:       return 0x24;
    case Qt::Key_Left:       return 0x25;
    case Qt::Key_Up:         return 0x26;
    case Qt::Key_Right:      return 0x27;
    case Qt::Key_Down:       return 0x28;
    case Qt::Key_Print:      return 0x2A;
    case Qt::Key_Insert:     return 0x2D;
    case Qt::Key_Delete:     return 0x2E;
    case Qt::Key_Help:       return 0x2F;
    case Qt::Key_Meta:       return 0x5B;   // 左 Win 键
    default: break;
    }
    // OEM 标点（Qt 与 VK 基本同值: ;=0xBA =0xBB ,=0xBC -=0xBD .=0xBE /=0xBF `=0xC0 [0xDB \0xDC ]0xDD '0xDE）
    if (key > 0x20 && key < 0x100) return (quint32)key;
    return 0;
}

static quint32 qtModsToModFlags(Qt::KeyboardModifiers mods)
{
    quint32 f = 0;
    if (mods & Qt::AltModifier)     f |= 0x1;   // MOD_ALT
    if (mods & Qt::ControlModifier) f |= 0x2;   // MOD_CONTROL
    if (mods & Qt::ShiftModifier)   f |= 0x4;   // MOD_SHIFT
    if (mods & Qt::MetaModifier)    f |= 0x8;   // MOD_WIN
    return f;
}

void KernelPage::onHotkeyContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_hotkeyTable->indexAt(pos);
    if (!idx.isValid()) return;
    m_hotkeyTable->selectRow(idx.row());

    QMenu menu(this);
    QAction* modifyAction = menu.addAction(QStringLiteral("修改热键"));
    connect(modifyAction, &QAction::triggered, this, &KernelPage::onHotkeyModify);
    QAction* deleteAction = menu.addAction(QStringLiteral("删除热键"));
    connect(deleteAction, &QAction::triggered, this, &KernelPage::onHotkeyDelete);
    menu.addSeparator();
    QAction* copyAction = menu.addAction(QStringLiteral("复制热键组合"));
    connect(copyAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_hotkeyTable->item(m_hotkeyTable->currentRow(), 0))
            QGuiApplication::clipboard()->setText(it->text());
    });
    QAction* copyProcAction = menu.addAction(QStringLiteral("复制注册进程"));
    connect(copyProcAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_hotkeyTable->item(m_hotkeyTable->currentRow(), 5))
            QGuiApplication::clipboard()->setText(it->text());
    });
    menu.exec(m_hotkeyTable->viewport()->mapToGlobal(pos));
}

void KernelPage::onHotkeyModify()
{
    int row = m_hotkeyTable->currentRow();
    if (row < 0 || row >= m_hotkeyEntries.size()) return;
    if (!requireDriver()) return;

    const ArkHotkeyEntry e = m_hotkeyEntries[row];

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("修改热键"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(
        QStringLiteral("为进程 %1 的热键 (ID=%2) 设置新的按键组合:")
            .arg(QString::fromWCharArray(e.imageName)).arg(e.id), &dlg));
    auto* editor = new QKeySequenceEdit(&dlg);
    lay->addWidget(editor);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;
    QKeySequence seq = editor->keySequence();
    if (seq.isEmpty() || seq.count() < 1) return;

    QKeyCombination comb = seq[0];
    quint32 newVk = qtKeyToVk(comb.key());
    quint32 newMods = qtModsToModFlags(comb.keyboardModifiers());
    if (newVk == 0) {
        QMessageBox::warning(this, QStringLiteral("修改热键"),
            QStringLiteral("无法识别该按键组合对应的虚拟键码"));
        return;
    }

    auto& drv = AppContext::instance().driver();
    if (drv.kbdModifyHotkey(e.pid, e.id, e.fsModifiers, e.vk, newMods, newVk)) {
        m_hotkeyStatusLabel->setText(QStringLiteral("热键已修改: %1 -> %2")
            .arg(vkDisplayName(e.vk)).arg(vkDisplayName(newVk)));
        onRefreshHotkeys();
    } else {
        QMessageBox::warning(this, QStringLiteral("修改热键"),
            QStringLiteral("修改失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onHotkeyDelete()
{
    int row = m_hotkeyTable->currentRow();
    if (row < 0 || row >= m_hotkeyEntries.size()) return;
    if (!requireDriver()) return;

    const ArkHotkeyEntry e = m_hotkeyEntries[row];
    auto ret = QMessageBox::warning(this, QStringLiteral("删除热键"),
        QStringLiteral("确定删除进程 %1 注册的热键 (ID=%2, %3) 吗?\n\n"
                       "说明: 热键将从系统热键表摘除并立即失效, "
                       "其内核内存会在注册进程注销或退出时释放。")
            .arg(QString::fromWCharArray(e.imageName))
            .arg(e.id)
            .arg(vkDisplayName(e.vk)),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.kbdDeleteHotkey(e.pid, e.id, e.fsModifiers, e.vk)) {
        m_hotkeyStatusLabel->setText(QStringLiteral("热键已删除"));
        onRefreshHotkeys();
    } else {
        QMessageBox::warning(this, QStringLiteral("删除热键"),
            QStringLiteral("删除失败 (错误码: %1)").arg(drv.lastIoError()));
    }
}

void KernelPage::onRefreshHotkeys()
{
    if (!m_hotkeyTable) return;
    m_hotkeyTable->setRowCount(0);
    if (!requireDriver()) return;

    ArkHotkeyArray arr;
    auto& drv = AppContext::instance().driver();
    if (!drv.kbdEnumHotkeys(arr)) {
        m_hotkeyStatusLabel->setText(QStringLiteral("热键枚举失败 (错误码: %1)")
            .arg(drv.lastIoError()));
        return;
    }

    QString filter = m_hotkeyFilterEdit ? m_hotkeyFilterEdit->text().trimmed() : QString();
    m_hotkeyEntries.clear();
    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkHotkeyEntry& e = arr.entries[i];
        QString keyName = vkDisplayName(e.vk);
        QString combo = modifiersDisplayName(e.fsModifiers);
        QString comboText = (combo == QStringLiteral("-"))
            ? keyName : combo + QStringLiteral(" + ") + keyName;

        QString proc = QString::fromWCharArray(e.imageName);
        if (proc.isEmpty()) {
            proc = e.pid ? QStringLiteral("(PID %1)").arg(e.pid) : QStringLiteral("(未知)");
        } else {
            proc += QStringLiteral(" (PID %1)").arg(e.pid);
        }

        if (!filter.isEmpty()) {
            if (!comboText.contains(filter, Qt::CaseInsensitive) &&
                !proc.contains(filter, Qt::CaseInsensitive) &&
                !QString::number(e.vk, 16).contains(filter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        m_hotkeyEntries.append(e);
        int r = m_hotkeyTable->rowCount();
        m_hotkeyTable->insertRow(r);
        m_hotkeyTable->setItem(r, 0, new QTableWidgetItem(comboText));
        m_hotkeyTable->setItem(r, 1, new QTableWidgetItem(keyName));
        m_hotkeyTable->setItem(r, 2, new QTableWidgetItem(
            QStringLiteral("0x%1").arg(e.vk, 2, 16, QChar('0'))));
        m_hotkeyTable->setItem(r, 3, new QTableWidgetItem(combo));
        m_hotkeyTable->setItem(r, 4, new QTableWidgetItem(QString::number(e.id)));
        m_hotkeyTable->setItem(r, 5, new QTableWidgetItem(proc));
    }
    m_hotkeyStatusLabel->setText(
        QStringLiteral("共 %1 个注册热键, 显示 %2 个").arg(arr.count).arg(m_hotkeyTable->rowCount()));
}

// ==================== 命名空间管理 ====================

QWidget* KernelPage::createNsTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    // 快捷入口
    auto* quickRow = new QHBoxLayout();
    auto addQuick = [&](const QString& label, const QString& path) {
        auto* btn = new QPushButton(label, tab);
        connect(btn, &QPushButton::clicked, this, [this, path]() {
            m_nsPathEdit->setText(path);
            onNsGo();
        });
        quickRow->addWidget(btn);
    };
    addQuick(QStringLiteral("\\"), QStringLiteral("\\"));
    addQuick(QStringLiteral("\\Device"), QStringLiteral("\\Device"));
    addQuick(QStringLiteral("\\Driver"), QStringLiteral("\\Driver"));
    addQuick(QStringLiteral("\\BaseNamedObjects"), QStringLiteral("\\BaseNamedObjects"));
    addQuick(QStringLiteral("\\Callback"), QStringLiteral("\\Callback"));
    addQuick(QStringLiteral("\\KnownDlls"), QStringLiteral("\\KnownDlls"));
    addQuick(QStringLiteral("\\Global??"), QStringLiteral("\\Global??"));
    addQuick(QStringLiteral("\\ObjectTypes"), QStringLiteral("\\ObjectTypes"));
    addQuick(QStringLiteral("\\FileSystem"), QStringLiteral("\\FileSystem"));
    addQuick(QStringLiteral("\\KernelObjects"), QStringLiteral("\\KernelObjects"));
    quickRow->addStretch();
    layout->addLayout(quickRow);

    // 路径行
    auto* pathRow = new QHBoxLayout();
    m_nsPathEdit = new QLineEdit(QStringLiteral("\\"), tab);
    connect(m_nsPathEdit, &QLineEdit::returnPressed, this, &KernelPage::onNsGo);
    auto* goBtn = new QPushButton(QStringLiteral("转到"), tab);
    connect(goBtn, &QPushButton::clicked, this, &KernelPage::onNsGo);
    auto* upBtn = new QPushButton(QStringLiteral("上一级"), tab);
    connect(upBtn, &QPushButton::clicked, this, &KernelPage::onNsUp);
    pathRow->addWidget(m_nsPathEdit, 1);
    pathRow->addWidget(goBtn);
    pathRow->addWidget(upBtn);
    layout->addLayout(pathRow);

    auto* hint = new QLabel(
        QStringLiteral("内核对象目录浏览器。双击 [Directory] 类型进入子目录; 右键可查询对象详情或删除对象。\n"
                       "可删除类型: Event / Mutant / Semaphore / SymbolicLink / Section / Key / Directory。"), tab);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_nsTree = new QTreeWidget(tab);
    m_nsTree->setColumnCount(3);
    m_nsTree->setHeaderLabels({
        QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("对象地址")});
    m_nsTree->setRootIsDecorated(true);
    m_nsTree->setUniformRowHeights(true);
    m_nsTree->setAlternatingRowColors(true);
    m_nsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_nsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nsTree->header()->setStretchLastSection(true);
    connect(m_nsTree, &QTreeWidget::itemDoubleClicked,
            this, &KernelPage::onNsItemActivated);
    connect(m_nsTree, &QTreeWidget::itemExpanded,
            this, &KernelPage::onNsItemExpanded);
    connect(m_nsTree, &QTreeWidget::customContextMenuRequested,
            this, &KernelPage::onNsContextMenu);
    layout->addWidget(m_nsTree, 1);

    m_nsStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_nsStatusLabel);

    m_nsContextMenu = new QMenu(this);
    QAction* queryAction = m_nsContextMenu->addAction(QStringLiteral("对象详情"));
    connect(queryAction, &QAction::triggered, this, &KernelPage::onNsQueryObject);
    QAction* deleteAction = m_nsContextMenu->addAction(QStringLiteral("删除对象"));
    connect(deleteAction, &QAction::triggered, this, &KernelPage::onNsDeleteObject);
    m_nsContextMenu->addSeparator();
    QAction* copyNameAction = m_nsContextMenu->addAction(QStringLiteral("复制名称"));
    connect(copyNameAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_nsTree->currentItem())
            QGuiApplication::clipboard()->setText(it->text(0));
    });
    QAction* copyPathAction = m_nsContextMenu->addAction(QStringLiteral("复制完整路径"));
    connect(copyPathAction, &QAction::triggered, this, [this]() {
        if (auto* it = m_nsTree->currentItem())
            QGuiApplication::clipboard()->setText(it->data(0, kNsPathRole).toString());
    });
    return tab;
}

void KernelPage::onNsGo()
{
    if (!requireDriver()) return;
    QString path = m_nsPathEdit->text().trimmed();
    if (path.isEmpty()) path = QStringLiteral("\\");
    // 统一根路径
    if (path.length() > 1 && path.endsWith(QLatin1Char('\\'))) path.chop(1);
    if (path.isEmpty()) path = QStringLiteral("\\");

    ArkNsArray arr;
    auto& drv = AppContext::instance().driver();
    if (!drv.nsEnumDir(path, arr)) {
        m_nsStatusLabel->setText(QStringLiteral("枚举 %1 失败 (错误码: %2)")
            .arg(path).arg(drv.lastIoError()));
        return;
    }
    m_nsCurrentPath = path;
    m_nsTree->clear();
    m_nsTree->setUpdatesEnabled(false);
    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkNsEntry& e = arr.entries[i];
        QString name = QString::fromWCharArray(e.name);
        QString typeName = QString::fromWCharArray(e.typeName);
        auto* item = new QTreeWidgetItem(m_nsTree);
        item->setText(0, name);
        item->setText(1, typeName);
        item->setText(2, e.object ? hexAddr(e.object) : QStringLiteral("-"));
        item->setData(0, kNsPathRole, nsJoinPath(path, name));
        item->setData(0, kNsLoadedRole, false);
        if (typeName.compare(QStringLiteral("Directory"), Qt::CaseInsensitive) == 0) {
            item->setForeground(0, QBrush(Qt::blue));
            item->addChild(new QTreeWidgetItem());
        }
    }
    m_nsTree->setUpdatesEnabled(true);
    m_nsStatusLabel->setText(QStringLiteral("%1: 共 %2 个对象").arg(path).arg(m_nsTree->topLevelItemCount()));
}

void KernelPage::onNsUp()
{
    if (m_nsCurrentPath.isEmpty() || m_nsCurrentPath == QStringLiteral("\\")) return;
    int idx = m_nsCurrentPath.lastIndexOf(QLatin1Char('\\'));
    QString parent = (idx <= 0) ? QStringLiteral("\\") : m_nsCurrentPath.left(idx);
    m_nsPathEdit->setText(parent);
    onNsGo();
}

void KernelPage::onNsItemActivated(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item) return;
    if (item->text(1).compare(QStringLiteral("Directory"), Qt::CaseInsensitive) != 0) return;
    item->setExpanded(!item->isExpanded());
}

void KernelPage::onNsItemExpanded(QTreeWidgetItem* item)
{
    if (!item || item->text(1).compare(QStringLiteral("Directory"), Qt::CaseInsensitive) != 0 ||
        item->data(0, kNsLoadedRole).toBool()) {
        return;
    }

    const QString path = item->data(0, kNsPathRole).toString();
    if (path.isEmpty() || !requireDriver()) {
        return;
    }

    ArkNsArray arr;
    auto& drv = AppContext::instance().driver();
    if (!drv.nsEnumDir(path, arr)) {
        m_nsStatusLabel->setText(QStringLiteral("%1: 枚举失败 (错误码 %2)")
            .arg(path).arg(drv.lastIoError()));
        return;
    }

    while (item->childCount() > 0) {
        delete item->takeChild(0);
    }
    for (quint32 i = 0; i < arr.count && i < 1024; ++i) {
        const ArkNsEntry& e = arr.entries[i];
        const QString name = QString::fromWCharArray(e.name);
        const QString typeName = QString::fromWCharArray(e.typeName);
        auto* child = new QTreeWidgetItem(item);
        child->setText(0, name);
        child->setText(1, typeName);
        child->setText(2, e.object ? hexAddr(e.object) : QStringLiteral("-"));
        child->setData(0, kNsPathRole, nsJoinPath(path, name));
        child->setData(0, kNsLoadedRole, false);
        if (typeName.compare(QStringLiteral("Directory"), Qt::CaseInsensitive) == 0) {
            child->setForeground(0, QBrush(Qt::blue));
            child->addChild(new QTreeWidgetItem());
        }
    }
    item->setData(0, kNsLoadedRole, true);
}

void KernelPage::onNsContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_nsTree->indexAt(pos);
    if (!idx.isValid()) return;
    m_nsTree->setCurrentIndex(idx);
    m_nsContextMenu->exec(m_nsTree->viewport()->mapToGlobal(pos));
}

static QString nsJoinPath(const QString& parent, const QString& name)
{
    return (parent == QStringLiteral("\\"))
        ? QStringLiteral("\\") + name
        : parent + QStringLiteral("\\") + name;
}

void KernelPage::onNsQueryObject()
{
    auto* item = m_nsTree->currentItem();
    if (!item) return;
    QString name = item->text(0);
    if (!requireDriver()) return;

    QString fullPath = item->data(0, kNsPathRole).toString();
    if (fullPath.isEmpty()) fullPath = nsJoinPath(m_nsCurrentPath, name);
    ArkNsObjectInfo info;
    auto& drv = AppContext::instance().driver();
    if (!drv.nsQueryPath(fullPath, info)) {
        m_nsStatusLabel->setText(QStringLiteral("查询 %1 失败 (错误码: %2)")
            .arg(fullPath).arg(drv.lastIoError()));
        return;
    }
    QString text;
    text += QStringLiteral("路径: %1\n").arg(fullPath);
    text += QStringLiteral("类型: %1\n").arg(QString::fromWCharArray(info.typeName));
    text += QStringLiteral("对象地址: %1\n").arg(hexAddr(info.object));
    text += QStringLiteral("指针引用数: %1\n").arg(info.pointerCount);
    text += QStringLiteral("句柄数: %1\n").arg(info.handleCount);
    showTextDetail(this, QStringLiteral("对象详情 - %1").arg(name), text);
}

void KernelPage::onNsDeleteObject()
{
    auto* item = m_nsTree->currentItem();
    if (!item) return;
    QString name = item->text(0);
    QString type = item->text(1);
    if (!requireDriver()) return;

    QString fullPath = item->data(0, kNsPathRole).toString();
    if (fullPath.isEmpty()) fullPath = nsJoinPath(m_nsCurrentPath, name);
    auto ret = QMessageBox::warning(this, QStringLiteral("命名空间管理"),
        QStringLiteral("确定要删除对象 %1 (类型: %2) 吗?\n\n"
                       "警告: 删除系统关键对象可能导致蓝屏!")
            .arg(fullPath).arg(type),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& drv = AppContext::instance().driver();
    if (drv.nsDeleteObject(type, fullPath)) {
        m_nsStatusLabel->setText(QStringLiteral("删除指令已发送: %1").arg(fullPath));
        onNsGo();
    } else {
        QMessageBox::warning(this, QStringLiteral("命名空间管理"),
            QStringLiteral("删除失败 (错误码: %1, 可能类型不支持删除)").arg(drv.lastIoError()));
    }
}

// ==================== 系统视图 ====================

QWidget* KernelPage::createSysViewTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    m_sysViewCombo = new QComboBox(tab);
    m_sysViewCombo->addItem(QStringLiteral("SSDT (系统服务表)"));
    m_sysViewCombo->addItem(QStringLiteral("IDT (中断描述符表)"));
    m_sysViewCombo->addItem(QStringLiteral("对象类型统计"));
    m_sysViewCombo->addItem(QStringLiteral("系统回调 (进程/线程/镜像)"));
    m_sysViewCombo->addItem(QStringLiteral("卸载驱动记录 (MmUnloadedDrivers)"));
    connect(m_sysViewCombo, &QComboBox::currentIndexChanged,
            this, &KernelPage::onSysViewModeChanged);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &KernelPage::onSysViewRefresh);
    toolRow->addWidget(new QLabel(QStringLiteral("视图:"), tab));
    toolRow->addWidget(m_sysViewCombo);
    toolRow->addWidget(refreshBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    m_sysViewTable = new QTableWidget(tab);
    m_sysViewTable->setColumnCount(3);
    m_sysViewTable->setHorizontalHeaderLabels({
        QStringLiteral("索引"), QStringLiteral("当前地址"), QStringLiteral("模块")});
    m_sysViewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sysViewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sysViewTable->setAlternatingRowColors(true);
    m_sysViewTable->verticalHeader()->setVisible(false);
    m_sysViewTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_sysViewTable, 1);

    m_sysViewStatusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_sysViewStatusLabel);
    return tab;
}

void KernelPage::onSysViewModeChanged(int index)
{
    m_sysViewTable->setRowCount(0);
    switch (index) {
    case 0:
        m_sysViewTable->setColumnCount(3);
        m_sysViewTable->setHorizontalHeaderLabels({
            QStringLiteral("索引"), QStringLiteral("SSDT 地址"), QStringLiteral("所属模块")});
        break;
    case 1:
        m_sysViewTable->setColumnCount(4);
        m_sysViewTable->setHorizontalHeaderLabels({
            QStringLiteral("CPU"), QStringLiteral("中断号"), QStringLiteral("处理函数"), QStringLiteral("说明")});
        break;
    case 2:
        m_sysViewTable->setColumnCount(5);
        m_sysViewTable->setHorizontalHeaderLabels({
            QStringLiteral("类型名"), QStringLiteral("索引"), QStringLiteral("对象总数"), QStringLiteral("句柄总数"), QStringLiteral("备注")});
        break;
    case 3:
        m_sysViewTable->setColumnCount(4);
        m_sysViewTable->setHorizontalHeaderLabels({
            QStringLiteral("类型"), QStringLiteral("回调函数"), QStringLiteral("所属模块"), QStringLiteral("风险")});
        break;
    case 4:
        m_sysViewTable->setColumnCount(4);
        m_sysViewTable->setHorizontalHeaderLabels({
            QStringLiteral("驱动名"), QStringLiteral("起始地址"), QStringLiteral("结束地址"), QStringLiteral("备注")});
        break;
    }
    onSysViewRefresh();
}

void KernelPage::onSysViewRefresh()
{
    m_sysViewTable->setRowCount(0);
    if (!requireDriver()) return;
    auto& drv = AppContext::instance().driver();

    switch (m_sysViewCombo->currentIndex()) {
    case 0: {
        ArkSsdtInfo info;
        if (!drv.arkQuerySSDT(info)) {
            m_sysViewStatusLabel->setText(QStringLiteral("SSDT 查询失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        // 建立内核模块区间表, 用于归属
        auto mods = std::make_unique<ArkModuleArray>();
        struct Range { quint64 lo, hi; QString path; };
        QVector<Range> ranges;
        if (drv.arkEnumKernelModules(*mods)) {
            for (quint32 i = 0; i < mods->count && i < 512; ++i) {
                Range r;
                r.lo = mods->entries[i].base;
                r.hi = r.lo + mods->entries[i].size;
                r.path = QString::fromWCharArray(mods->entries[i].path);
                ranges.append(r);
            }
        }
        for (quint32 i = 0; i < info.count && i < 640; ++i) {
            quint64 addr = info.entries[i];
            int r = m_sysViewTable->rowCount();
            m_sysViewTable->insertRow(r);
            m_sysViewTable->setItem(r, 0, new QTableWidgetItem(QString::number(i)));
            m_sysViewTable->setItem(r, 1, new QTableWidgetItem(hexAddr(addr)));
            // 归属模块
            QString owner = QStringLiteral("未知");
            for (const Range& rg : ranges) {
                if (addr >= rg.lo && addr < rg.hi) {
                    owner = rg.path;
                    break;
                }
            }
            m_sysViewTable->setItem(r, 2, new QTableWidgetItem(owner));
        }
        m_sysViewStatusLabel->setText(
            QStringLiteral("SSDT 基址: %1, 共 %2 项").arg(hexAddr(info.serviceBase)).arg(info.count));
        break;
    }
    case 1: {
        ArkIdtInfo info;
        if (!drv.arkQueryIDT(info)) {
            m_sysViewStatusLabel->setText(QStringLiteral("IDT 查询失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        // 常见中断说明
        auto irqDesc = [](quint32 v) -> QString {
            switch (v) {
            case 0: return QStringLiteral("定时器 / #DE");
            case 1: return QStringLiteral("键盘");
            case 2: return QStringLiteral("级联");
            case 3: return QStringLiteral("串口 / #BP");
            case 4: return QStringLiteral("串口 / #OF");
            case 6: return QStringLiteral("软盘");
            case 8: return QStringLiteral("RTC");
            case 12: return QStringLiteral("PS/2 鼠标");
            case 13: return QStringLiteral("协处理器");
            case 14: case 15: return QStringLiteral("ATA / #PF");
            default:
                if (v == 0x80) return QStringLiteral("APIC Spurious");
                if (v >= 0x20 && v <= 0x3F) return QStringLiteral("设备/MSI");
                if (v >= 0x40) return QStringLiteral("IPI/其他");
                return QStringLiteral("异常/保留");
            }
        };
        for (quint32 c = 0; c < info.cpuCount && c < 8; ++c) {
            for (quint32 i = 0; i < 256; ++i) {
                int r = m_sysViewTable->rowCount();
                m_sysViewTable->insertRow(r);
                m_sysViewTable->setItem(r, 0, new QTableWidgetItem(QString::number(c)));
                m_sysViewTable->setItem(r, 1, new QTableWidgetItem(
                    QStringLiteral("%1 (0x%2)").arg(i).arg(i, 2, 16, QChar('0'))));
                m_sysViewTable->setItem(r, 2, new QTableWidgetItem(hexAddr(info.cpus[c].handlers[i])));
                m_sysViewTable->setItem(r, 3, new QTableWidgetItem(irqDesc(i)));
            }
        }
        m_sysViewStatusLabel->setText(
            QStringLiteral("IDT: %1 个 CPU, 每核 256 项").arg(info.cpuCount));
        break;
    }
    case 2: {
        ArkObjTypeArray arr;
        if (!drv.arkEnumObjectTypes(arr)) {
            m_sysViewStatusLabel->setText(QStringLiteral("对象类型查询失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        for (quint32 i = 0; i < arr.count && i < 96; ++i) {
            const ArkObjTypeEntry& e = arr.entries[i];
            int r = m_sysViewTable->rowCount();
            m_sysViewTable->insertRow(r);
            m_sysViewTable->setItem(r, 0, new QTableWidgetItem(QString::fromWCharArray(e.name)));
            m_sysViewTable->setItem(r, 1, new QTableWidgetItem(QString::number(e.index)));
            m_sysViewTable->setItem(r, 2, new QTableWidgetItem(QString::number(e.totalObjects)));
            m_sysViewTable->setItem(r, 3, new QTableWidgetItem(QString::number(e.totalHandles)));
            m_sysViewTable->setItem(r, 4, new QTableWidgetItem(QString()));
        }
        m_sysViewStatusLabel->setText(QStringLiteral("共 %1 个对象类型").arg(arr.count));
        break;
    }
    case 3: {
        ArkCallbackArray arr;
        if (!drv.arkEnumSysCallbacks(arr)) {
            m_sysViewStatusLabel->setText(QStringLiteral("系统回调查询失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        auto kindName = [](quint32 k) -> QString {
            switch (k) {
            case 0: return QStringLiteral("进程创建");
            case 1: return QStringLiteral("线程创建");
            case 2: return QStringLiteral("镜像加载");
            default: return QStringLiteral("未知");
            }
        };
        for (quint32 i = 0; i < arr.count && i < 128; ++i) {
            const ArkCallbackEntry& e = arr.entries[i];
            int r = m_sysViewTable->rowCount();
            m_sysViewTable->insertRow(r);
            m_sysViewTable->setItem(r, 0, new QTableWidgetItem(kindName(e.kind)));
            m_sysViewTable->setItem(r, 1, new QTableWidgetItem(hexAddr(e.callback)));
            QString owner = QString::fromWCharArray(e.ownerPath);
            if (owner.isEmpty()) owner = QStringLiteral("(未归属模块)");
            m_sysViewTable->setItem(r, 2, new QTableWidgetItem(owner));
            // 不属于 nt 自身的回调（来自第三方驱动）标为关注
            if (owner.compare(QLatin1String("ntoskrnl.exe"), Qt::CaseInsensitive) != 0 &&
                !owner.isEmpty() && !owner.startsWith(QLatin1Char('('))) {
                auto* risk = new QTableWidgetItem(QStringLiteral("第三方驱动"));
                risk->setForeground(QBrush(QColor(200, 120, 0)));
                m_sysViewTable->setItem(r, 3, risk);
            } else {
                m_sysViewTable->setItem(r, 3, new QTableWidgetItem(QString()));
            }
        }
        m_sysViewStatusLabel->setText(arr.count == 0
            ? QStringLiteral("未定位到系统回调表（当前系统可能受 HVCI/内核布局保护）")
            : QStringLiteral("共 %1 个系统回调").arg(arr.count));
        break;
    }
    case 4: {
        ArkUnloadedArray arr;
        if (!drv.arkEnumUnloadedDrivers(arr)) {
            m_sysViewStatusLabel->setText(QStringLiteral("卸载驱动记录查询失败 (错误码: %1)")
                .arg(drv.lastIoError()));
            return;
        }
        for (quint32 i = 0; i < arr.count && i < 64; ++i) {
            const ArkUnloadedEntry& e = arr.entries[i];
            int r = m_sysViewTable->rowCount();
            m_sysViewTable->insertRow(r);
            m_sysViewTable->setItem(r, 0, new QTableWidgetItem(QString::fromWCharArray(e.name)));
            m_sysViewTable->setItem(r, 1, new QTableWidgetItem(hexAddr(e.startAddress)));
            m_sysViewTable->setItem(r, 2, new QTableWidgetItem(hexAddr(e.endAddress)));
            m_sysViewTable->setItem(r, 3, new QTableWidgetItem(QString()));
        }
        m_sysViewStatusLabel->setText(arr.count == 0
            ? QStringLiteral("未定位到 MmUnloadedDrivers（当前系统可能不提供该表）")
            : QStringLiteral("共 %1 条卸载记录").arg(arr.count));
        break;
    }
    }
}
