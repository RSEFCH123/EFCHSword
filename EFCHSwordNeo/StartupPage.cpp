#include "StartupPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QGuiApplication>
#include <QClipboard>

#include <windows.h>
#include <shlobj.h>

enum StartupColumn {
    SC_Name = 0,
    SC_Type,
    SC_Source,
    SC_Path,
    SC_Publisher,
    SC_Count
};

StartupPage::StartupPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupContextMenu();
    onRefresh();
}

StartupPage::~StartupPage() = default;

void StartupPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* toolLayout = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    m_hideSystem = new QCheckBox(QStringLiteral("隐藏系统启动项"), this);
    m_hideSystemSvc = new QCheckBox(QStringLiteral("隐藏系统服务"), this);
    m_onlyAutoSvc = new QCheckBox(QStringLiteral("只显示自动服务"), this);
    m_statusLabel = new QLabel(QStringLiteral("启动项: 0"), this);

    connect(refreshBtn, &QPushButton::clicked, this, &StartupPage::onRefresh);
    connect(m_hideSystem, &QCheckBox::toggled, this, &StartupPage::onRefresh);
    connect(m_hideSystemSvc, &QCheckBox::toggled, this, &StartupPage::onRefresh);
    connect(m_onlyAutoSvc, &QCheckBox::toggled, this, &StartupPage::onRefresh);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addWidget(m_hideSystem);
    toolLayout->addWidget(m_hideSystemSvc);
    toolLayout->addWidget(m_onlyAutoSvc);
    toolLayout->addStretch();
    toolLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(toolLayout);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(SC_Count);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("类型"),
        QStringLiteral("来源"),
        QStringLiteral("路径"),
        QStringLiteral("发布者")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setColumnWidth(SC_Name, 180);
    m_table->setColumnWidth(SC_Type, 100);
    m_table->setColumnWidth(SC_Source, 150);
    m_table->setColumnWidth(SC_Path, 300);

    connect(m_table, &QTableWidget::customContextMenuRequested, this, &StartupPage::onContextMenu);

    mainLayout->addWidget(m_table);
}

void StartupPage::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    auto* deleteAction = m_contextMenu->addAction(QStringLiteral("删除启动项"));
    connect(deleteAction, &QAction::triggered, this, &StartupPage::onDeleteStartup);

    auto* openLocationAction = m_contextMenu->addAction(QStringLiteral("打开文件位置"));
    connect(openLocationAction, &QAction::triggered, this, &StartupPage::onOpenFileLocation);

    auto* copyPathAction = m_contextMenu->addAction(QStringLiteral("复制路径"));
    connect(copyPathAction, &QAction::triggered, this, &StartupPage::onCopyPath);
}

void StartupPage::onRefresh()
{
    m_table->setRowCount(0);

    // 枚举启动文件夹
    wchar_t commonStartup[MAX_PATH] = {0};
    wchar_t userStartup[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTUP, nullptr, 0, commonStartup);
    SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, userStartup);

    enumDirectoryStartup(QString::fromWCharArray(commonStartup), QStringLiteral("所有用户启动文件夹"));
    enumDirectoryStartup(QString::fromWCharArray(userStartup), QStringLiteral("当前用户启动文件夹"));

    // 枚举注册表 Run 键
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QStringLiteral("HKCU Run"));
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"), QStringLiteral("HKCU RunOnce"));
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx"), QStringLiteral("HKCU RunOnceEx"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"), QStringLiteral("HKLM Run"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce"), QStringLiteral("HKLM RunOnce"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx"), QStringLiteral("HKLM RunOnceEx"));

    // Winlogon
    QSettings winlogon(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"),
                       QSettings::NativeFormat);
    QString shell = winlogon.value(QStringLiteral("Shell")).toString();
    if (!shell.isEmpty() && shell.toLower() != QStringLiteral("explorer.exe")) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, SC_Name, new QTableWidgetItem(shell));
        m_table->setItem(row, SC_Type, new QTableWidgetItem(QStringLiteral("Winlogon")));
        m_table->setItem(row, SC_Source, new QTableWidgetItem(QStringLiteral("Winlogon\\Shell")));
        m_table->setItem(row, SC_Path, new QTableWidgetItem(shell));
    }

    // 枚举服务
    if (!m_hideSystemSvc->isChecked()) {
        enumServices();
    }

    m_statusLabel->setText(QStringLiteral("启动项: %1").arg(m_table->rowCount()));
}

void StartupPage::enumDirectoryStartup(const QString& dir, const QString& source)
{
    QDir d(dir);
    if (!d.exists()) return;

    QFileInfoList entries = d.entryInfoList(QDir::Files);
    for (const auto& fi : entries) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, SC_Name, new QTableWidgetItem(fi.fileName()));
        m_table->setItem(row, SC_Type, new QTableWidgetItem(QStringLiteral("快捷方式/文件")));
        m_table->setItem(row, SC_Source, new QTableWidgetItem(source));
        m_table->setItem(row, SC_Path, new QTableWidgetItem(fi.absoluteFilePath()));
    }
}

void StartupPage::enumRegistryStartup(quint32 root, const QString& path, const QString& source)
{
    QString regPath;
    if (root == 1) regPath = QStringLiteral("HKEY_LOCAL_MACHINE\\") + path;
    else if (root == 2) regPath = QStringLiteral("HKEY_CURRENT_USER\\") + path;
    else return;

    QSettings settings(regPath, QSettings::NativeFormat);
    QStringList keys = settings.childKeys();
    for (const auto& key : keys) {
        QString value = settings.value(key).toString();
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, SC_Name, new QTableWidgetItem(key));
        m_table->setItem(row, SC_Type, new QTableWidgetItem(QStringLiteral("注册表")));
        m_table->setItem(row, SC_Source, new QTableWidgetItem(source));
        m_table->setItem(row, SC_Path, new QTableWidgetItem(value));
    }
}

void StartupPage::enumServices()
{
    QSettings services(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services"),
                       QSettings::NativeFormat);
    QStringList groups = services.childGroups();

    for (const auto& svcName : groups) {
        if (svcName.startsWith(QStringLiteral("Type")) || svcName.isEmpty()) continue;

        services.beginGroup(svcName);
        int start = services.value(QStringLiteral("Start"), -1).toInt();
        QString imagePath = services.value(QStringLiteral("ImagePath")).toString();
        int type = services.value(QStringLiteral("Type"), -1).toInt();
        services.endGroup();

        // 只显示驱动和服务
        if (type == 1 || type == 2) {
            // 驱动类型，跳过
            continue;
        }

        if (m_onlyAutoSvc->isChecked() && start != 2) continue;

        if (imagePath.isEmpty()) continue;

        // 清理路径
        QString cleanPath = imagePath;
        cleanPath.remove(QStringLiteral("\\??\\"));
        cleanPath.remove(QStringLiteral("\""));
        cleanPath = cleanPath.section(' ', 0, 0);

        QString startType;
        switch (start) {
            case 0: startType = QStringLiteral("服务(启动)"); break;
            case 1: startType = QStringLiteral("服务(系统)"); break;
            case 2: startType = QStringLiteral("服务(自动)"); break;
            case 3: startType = QStringLiteral("服务(手动)"); break;
            case 4: startType = QStringLiteral("服务(禁用)"); break;
            default: startType = QStringLiteral("服务"); break;
        }

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, SC_Name, new QTableWidgetItem(svcName));
        m_table->setItem(row, SC_Type, new QTableWidgetItem(startType));
        m_table->setItem(row, SC_Source, new QTableWidgetItem(QStringLiteral("系统服务")));
        m_table->setItem(row, SC_Path, new QTableWidgetItem(imagePath));
    }
}

void StartupPage::onContextMenu(const QPoint& pos)
{
    QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid()) return;
    m_contextMenu->exec(m_table->viewport()->mapToGlobal(pos));
}

void StartupPage::onDeleteStartup()
{
    int row = m_table->currentRow();
    if (row < 0) return;

    auto* nameItem = m_table->item(row, SC_Name);
    auto* sourceItem = m_table->item(row, SC_Source);
    auto* pathItem = m_table->item(row, SC_Path);
    if (!nameItem || !sourceItem) return;

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定删除启动项: %1 吗?").arg(nameItem->text()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QString source = sourceItem->text();
    QString name = nameItem->text();
    QString path = pathItem ? pathItem->text() : QString();

    if (source.contains(QStringLiteral("启动文件夹"))) {
        QFile::remove(path);
    } else if (source.contains(QStringLiteral("Run")) || source.contains(QStringLiteral("RunOnce"))) {
        // 删除注册表项
        QString regPath;
        if (source.startsWith(QStringLiteral("HKCU"))) {
            regPath = QStringLiteral("HKEY_CURRENT_USER\\") +
                (source.contains(QStringLiteral("OnceEx")) ? QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx") :
                 source.contains(QStringLiteral("Once")) ? QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce") :
                 QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run"));
        } else {
            regPath = QStringLiteral("HKEY_LOCAL_MACHINE\\") +
                (source.contains(QStringLiteral("OnceEx")) ? QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx") :
                 source.contains(QStringLiteral("Once")) ? QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce") :
                 QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"));
        }
        QSettings settings(regPath, QSettings::NativeFormat);
        settings.remove(name);
    }

    m_table->removeRow(row);
}

void StartupPage::onOpenFileLocation()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* pathItem = m_table->item(row, SC_Path);
    if (!pathItem) return;
    QString path = pathItem->text();
    if (path.isEmpty()) return;

    // 清理路径
    path.remove(QStringLiteral("\\??\\"));
    path.remove(QStringLiteral("\""));
    path = path.section(' ', 0, 0);

    QProcess::startDetached(QStringLiteral("explorer.exe /select,\"%1\"").arg(path));
}

void StartupPage::onCopyPath()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* item = m_table->item(row, SC_Path);
    if (item) QGuiApplication::clipboard()->setText(item->text());
}
