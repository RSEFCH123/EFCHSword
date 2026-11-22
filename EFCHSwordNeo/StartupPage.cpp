#include "StartupPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QGuiApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QDialog>
#include <QDialogButtonBox>
#include <QColor>

#include <windows.h>
#include <shlobj.h>

// 启动项列
enum StartupColumn {
    SS_Name = 0,
    SS_Type,
    SS_Source,
    SS_State,
    SS_Path,
    SS_Count
};

// 服务列
enum ServiceColumn {
    SV_Name = 0,
    SV_StartType,
    SV_DisplayName,
    SV_State,
    SV_Path,
    SV_Count
};

// 淡红色（禁用项）
static const QColor kDisabledColor(0xE0, 0x8A, 0x8A);

// ==================== 启动项分类 ====================
// 每行的分类标签存在 SS_Name item 的 Qt::UserRole 上，工具栏下拉框按其过滤。
static const char* kStartupCategories[] = {
    "全部",
    "启动文件夹",
    "注册表Run类",
    "计划任务",
    "IFEO映像劫持",
    "Winlogon/系统登录",
    "Shell扩展与浏览器插件",
    "系统服务提供者",
    "驱动与输入法",
};

// 根据来源字符串推断分类
static QString classifyStartupSource(const QString& source)
{
    if (source.contains(QStringLiteral("Run")) || source.contains(QStringLiteral("WOW64启动项")))
        return QStringLiteral("注册表Run类");
    if (source.contains(QStringLiteral("Shell")) || source.contains(QStringLiteral("BHO")) ||
        source.contains(QStringLiteral("IE")) || source.contains(QStringLiteral("程序初始化")) ||
        source.contains(QStringLiteral("AppInit")))
        return QStringLiteral("Shell扩展与浏览器插件");
    if (source.contains(QStringLiteral("Winlogon")))
        return QStringLiteral("Winlogon/系统登录");
    if (source.contains(QStringLiteral("SessionManager")) || source.contains(QStringLiteral("LSA")) ||
        source.contains(QStringLiteral("Winsock")))
        return QStringLiteral("系统服务提供者");
    if (source.contains(QStringLiteral("Drivers32")) || source.contains(QStringLiteral("编解码器")) ||
        source.contains(QStringLiteral("输入法")) || source.contains(QStringLiteral("CTF")))
        return QStringLiteral("驱动与输入法");
    return QStringLiteral("注册表Run类");
}

static void tagStartupRow(QTableWidget* table, int row, const QString& category)
{
    if (auto* it = table->item(row, SS_Name)) it->setData(Qt::UserRole, category);
}

StartupPage::StartupPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    onRefreshStartup();
    onRefreshServices();
}

StartupPage::~StartupPage() = default;

void StartupPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);

    // ==================== 启动项 tab ====================
    {
        auto* tab = new QWidget(this);
        auto* layout = new QVBoxLayout(tab);

        auto* toolRow = new QHBoxLayout();
        auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
        m_hideMicrosoft = new QCheckBox(QStringLiteral("隐藏微软自带启动项"), tab);
        auto* createBtn = new QPushButton(QStringLiteral("新建启动项"), tab);
        auto* taskBtn = new QPushButton(QStringLiteral("新建计划任务"), tab);
        m_startupStatusLabel = new QLabel(QStringLiteral("启动项: 0"), tab);
        m_categoryCombo = new QComboBox(tab);
        for (const char* c : kStartupCategories) m_categoryCombo->addItem(QString::fromUtf8(c));
        m_categoryCombo->setToolTip(QStringLiteral("按启动项分类过滤"));

        connect(refreshBtn, &QPushButton::clicked, this, &StartupPage::onRefreshStartup);
        connect(m_hideMicrosoft, &QCheckBox::toggled, this, &StartupPage::onRefreshStartup);
        connect(createBtn, &QPushButton::clicked, this, &StartupPage::onCreateStartup);
        connect(taskBtn, &QPushButton::clicked, this, &StartupPage::onCreateTask);
        connect(m_categoryCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyCategoryFilter(); });

        toolRow->addWidget(refreshBtn);
        toolRow->addWidget(createBtn);
        toolRow->addWidget(taskBtn);
        toolRow->addWidget(m_hideMicrosoft);
        toolRow->addStretch();
        toolRow->addWidget(new QLabel(QStringLiteral("分类:"), tab));
        toolRow->addWidget(m_categoryCombo);
        toolRow->addWidget(m_startupStatusLabel);
        layout->addLayout(toolRow);

        m_startupTable = new QTableWidget(tab);
        m_startupTable->setColumnCount(SS_Count);
        m_startupTable->setHorizontalHeaderLabels({
            QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("来源"),
            QStringLiteral("状态"), QStringLiteral("路径/命令")});
        m_startupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_startupTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_startupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_startupTable->setContextMenuPolicy(Qt::CustomContextMenu);
        m_startupTable->setAlternatingRowColors(true);
        m_startupTable->verticalHeader()->setVisible(false);
        m_startupTable->horizontalHeader()->setStretchLastSection(true);
        m_startupTable->setColumnWidth(SS_Name, 160);
        m_startupTable->setColumnWidth(SS_Type, 90);
        m_startupTable->setColumnWidth(SS_Source, 150);
        m_startupTable->setColumnWidth(SS_State, 70);
        connect(m_startupTable, &QTableWidget::customContextMenuRequested,
                this, &StartupPage::onStartupContextMenu);
        layout->addWidget(m_startupTable);

        m_startupMenu = new QMenu(this);
        m_startupMenu->addAction(QStringLiteral("新建启动项"), this, &StartupPage::onCreateStartup);
        m_startupMenu->addAction(QStringLiteral("禁用/启用启动项"), this, &StartupPage::onToggleStartup);
        m_startupMenu->addAction(QStringLiteral("删除启动项"), this, &StartupPage::onDeleteStartup);
        m_startupMenu->addSeparator();
        m_startupMenu->addAction(QStringLiteral("打开文件位置"), this, &StartupPage::onOpenFileLocation);
        m_startupMenu->addAction(QStringLiteral("复制路径"), this, &StartupPage::onCopyPath);

        m_tabs->addTab(tab, QStringLiteral("启动项"));
    }

    // ==================== 服务 tab ====================
    {
        auto* tab = new QWidget(this);
        auto* layout = new QVBoxLayout(tab);

        auto* toolRow = new QHBoxLayout();
        auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
        auto* createBtn = new QPushButton(QStringLiteral("新建服务"), tab);
        m_onlyAutoSvc = new QCheckBox(QStringLiteral("只显示自动启动"), tab);
        m_hideDriverSvc = new QCheckBox(QStringLiteral("隐藏内核驱动服务"), tab);
        m_hideDriverSvc->setChecked(true);
        m_serviceStatusLabel = new QLabel(QStringLiteral("服务: 0"), tab);

        connect(refreshBtn, &QPushButton::clicked, this, &StartupPage::onRefreshServices);
        connect(m_onlyAutoSvc, &QCheckBox::toggled, this, &StartupPage::onRefreshServices);
        connect(m_hideDriverSvc, &QCheckBox::toggled, this, &StartupPage::onRefreshServices);
        connect(createBtn, &QPushButton::clicked, this, &StartupPage::onCreateService);

        toolRow->addWidget(refreshBtn);
        toolRow->addWidget(createBtn);
        toolRow->addWidget(m_onlyAutoSvc);
        toolRow->addWidget(m_hideDriverSvc);
        toolRow->addStretch();
        toolRow->addWidget(m_serviceStatusLabel);
        layout->addLayout(toolRow);

        m_serviceTable = new QTableWidget(tab);
        m_serviceTable->setColumnCount(SV_Count);
        m_serviceTable->setHorizontalHeaderLabels({
            QStringLiteral("服务名"), QStringLiteral("启动类型"), QStringLiteral("显示名"),
            QStringLiteral("状态"), QStringLiteral("二进制路径")});
        m_serviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_serviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_serviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_serviceTable->setContextMenuPolicy(Qt::CustomContextMenu);
        m_serviceTable->setAlternatingRowColors(true);
        m_serviceTable->verticalHeader()->setVisible(false);
        m_serviceTable->horizontalHeader()->setStretchLastSection(true);
        m_serviceTable->setColumnWidth(SV_Name, 170);
        m_serviceTable->setColumnWidth(SV_StartType, 90);
        m_serviceTable->setColumnWidth(SV_DisplayName, 200);
        m_serviceTable->setColumnWidth(SV_State, 60);
        connect(m_serviceTable, &QTableWidget::customContextMenuRequested,
                this, &StartupPage::onServiceContextMenu);
        layout->addWidget(m_serviceTable);

        m_serviceMenu = new QMenu(this);
        m_serviceMenu->addAction(QStringLiteral("新建服务"), this, &StartupPage::onCreateService);
        m_serviceMenu->addSeparator();
        m_serviceMenu->addAction(QStringLiteral("设为自动启动"), this, [this]() {
            onServiceStartType(2);
        });
        m_serviceMenu->addAction(QStringLiteral("设为手动启动"), this, [this]() {
            onServiceStartType(3);
        });
        m_serviceMenu->addAction(QStringLiteral("禁用服务"), this, [this]() {
            onServiceStartType(4);
        });
        m_serviceMenu->addSeparator();
        m_serviceMenu->addAction(QStringLiteral("允许安全模式启动 (最小化)"), this, [this]() {
            onServiceSafeBoot(0);
        });
        m_serviceMenu->addAction(QStringLiteral("允许安全模式启动 (带网络)"), this, [this]() {
            onServiceSafeBoot(1);
        });
        m_serviceMenu->addSeparator();
        m_serviceMenu->addAction(QStringLiteral("删除服务"), this, &StartupPage::onDeleteService);
        m_serviceMenu->addAction(QStringLiteral("复制二进制路径"), this, &StartupPage::onCopyServicePath);

        m_tabs->addTab(tab, QStringLiteral("服务"));
    }

    mainLayout->addWidget(m_tabs);
}

// ==================== 启动项 ====================

void StartupPage::applyCategoryFilter()
{
    if (!m_startupTable || !m_categoryCombo) return;
    const QString sel = m_categoryCombo->currentText();
    if (sel.isEmpty() || sel == QStringLiteral("全部")) {
        for (int r = 0; r < m_startupTable->rowCount(); ++r)
            m_startupTable->setRowHidden(r, false);
        return;
    }
    int visible = 0;
    for (int r = 0; r < m_startupTable->rowCount(); ++r) {
        auto* it = m_startupTable->item(r, SS_Name);
        const QString cat = it ? it->data(Qt::UserRole).toString() : QString();
        const bool show = cat == sel;
        m_startupTable->setRowHidden(r, !show);
        if (show) ++visible;
    }
}

void StartupPage::onRefreshStartup()
{
    m_startupTable->setRowCount(0);

    wchar_t commonStartup[MAX_PATH] = {0};
    wchar_t userStartup[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTUP, nullptr, 0, commonStartup);
    SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, userStartup);

    enumDirectoryStartup(QString::fromWCharArray(commonStartup),
                         QStringLiteral("所有用户启动文件夹"), false);
    enumDirectoryStartup(QString::fromWCharArray(userStartup),
                         QStringLiteral("当前用户启动文件夹"), true);

    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("HKCU Run"), QStringLiteral("Run"));
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"),
                        QStringLiteral("HKCU RunOnce"), QStringLiteral("RunOnce"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"),
                        QStringLiteral("HKLM Run"), QStringLiteral("Run"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce"),
                        QStringLiteral("HKLM RunOnce"), QStringLiteral("RunOnce"));
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx"),
                        QStringLiteral("HKCU RunOnceEx"), QStringLiteral("RunOnceEx"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx"),
                        QStringLiteral("HKLM RunOnceEx"), QStringLiteral("RunOnceEx"));
    enumRegistryStartup(2, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run"),
                        QStringLiteral("HKCU Policies\\Explorer\\Run"), QStringLiteral("Run"));
    enumRegistryStartup(1, QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run"),
                        QStringLiteral("HKLM Policies\\Explorer\\Run"), QStringLiteral("Run"));

    // Extended persistence locations.  They are displayed as separate
    // categories so analysts can quickly distinguish shell/IE, logon,
    // loader, network provider and input method entries.
    const struct { quint32 root; const wchar_t* key; const wchar_t* type; } extra[] = {
        {1, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks", L"资源管理器加载项"},
        {2, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks", L"资源管理器加载项"},
        {1, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Extensions\\Approved", L"Shell扩展"},
        {2, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Extensions\\Approved", L"Shell扩展"},
        {1, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects", L"IE插件/BHO"},
        {2, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects", L"IE插件/BHO"},
        {1, L"SOFTWARE\\Microsoft\\Internet Explorer\\Extensions", L"IE启动项"},
        {2, L"Software\\Microsoft\\Internet Explorer\\Extensions", L"IE启动项"},
        {1, L"SOFTWARE\\Microsoft\\Internet Explorer\\Toolbar", L"IE插件"},
        {2, L"Software\\Microsoft\\Internet Explorer\\Toolbar", L"IE插件"},
        {1, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"程序初始化/AppInit"},
        {2, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"程序初始化"},
        {1, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\Notify", L"Winlogon"},
        {1, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\GPExtensions", L"Winlogon扩展"},
        {1, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", L"SessionManager"},
        {1, L"SYSTEM\\CurrentControlSet\\Control\\Lsa", L"LSA提供者"},
        {1, L"SYSTEM\\CurrentControlSet\\Services\\WinSock2\\Parameters\\Protocol_Catalog9", L"Winsock提供者"},
        {1, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", L"编解码器"},
        {1, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", L"编解码器(WOW64)"},
        {2, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", L"编解码器"},
        {1, L"SOFTWARE\\Microsoft\\CTF\\TIP", L"输入法/文本服务"},
        {2, L"Software\\Microsoft\\CTF\\TIP", L"输入法/文本服务"},
        {1, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", L"WOW64启动项"},
        {1, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"WOW64启动项"}
    };
    for (const auto& e : extra)
        enumRegistryStartup(e.root, QString::fromWCharArray(e.key), QString::fromWCharArray(e.type), QStringLiteral("Extended"));
    // Image File Execution Options 的 Debugger 是常见持久化/劫持入口。
    for (quint32 root : {1u, 2u}) {
        const QString base = root == 1
            ? QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options")
            : QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options");
        QSettings ifeo(base, QSettings::NativeFormat);
        for (const QString& exe : ifeo.childGroups()) {
            QSettings sub(base + QStringLiteral("\\") + exe, QSettings::NativeFormat);
            const QString debugger = sub.value(QStringLiteral("Debugger")).toString();
            if (debugger.isEmpty()) continue;
            int row = m_startupTable->rowCount();
            m_startupTable->insertRow(row);
            m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(exe));
            m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("映像劫持")));
            m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(root == 1 ? QStringLiteral("HKLM IFEO") : QStringLiteral("HKCU IFEO")));
            m_startupTable->setItem(row, SS_State, new QTableWidgetItem(QStringLiteral("启用")));
            m_startupTable->setItem(row, SS_Path, new QTableWidgetItem(debugger));
            tagStartupRow(m_startupTable, row, QStringLiteral("IFEO映像劫持"));
        }
    }

    // Scheduled tasks with a logon/boot trigger are another common
    // persistence location.  schtasks is available on all supported Windows
    // versions and avoids depending on the Task Scheduler COM headers.
    enumTaskSchedulerStartup();

    // Winlogon Shell（不可禁用，仅展示）
    QSettings winlogon(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"),
                       QSettings::NativeFormat);
    QString shell = winlogon.value(QStringLiteral("Shell")).toString();
    if (!shell.isEmpty() && shell.toLower() != QStringLiteral("explorer.exe")) {
        int row = m_startupTable->rowCount();
        m_startupTable->insertRow(row);
        m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(shell));
        m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("Winlogon")));
        m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(QStringLiteral("Winlogon\\Shell")));
        m_startupTable->setItem(row, SS_State, new QTableWidgetItem(QStringLiteral("-")));
        m_startupTable->setItem(row, SS_Path, new QTableWidgetItem(shell));
        tagStartupRow(m_startupTable, row, QStringLiteral("Winlogon/系统登录"));
    }

    m_startupStatusLabel->setText(QStringLiteral("启动项: %1").arg(m_startupTable->rowCount()));
    applyCategoryFilter();
}

void StartupPage::enumTaskSchedulerStartup()
{
    QProcess p;
    p.start(QStringLiteral("schtasks.exe"), {
        QStringLiteral("/Query"), QStringLiteral("/FO"), QStringLiteral("CSV"),
        QStringLiteral("/NH")});
    if (!p.waitForFinished(1500)) {
        p.kill();
        p.waitForFinished(200);
        return;
    }
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    const QStringList lines = out.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (!line.startsWith('"')) continue;
        QStringList cols;
        QString cur; bool quoted = false;
        for (int i = 0; i < line.size(); ++i) {
            QChar c = line.at(i);
            if (c == '"') { if (quoted && i + 1 < line.size() && line.at(i + 1) == '"') { cur += '"'; ++i; } else quoted = !quoted; }
            else if (c == ',' && !quoted) { cols << cur; cur.clear(); }
            else cur += c;
        }
        cols << cur;
        // /FO CSV without /V is fast and stable: TaskName, Next Run Time,
        // Status.  The verbose form can take seconds while querying every
        // task and its localized trigger details.
        if (cols.size() < 3) continue;
        QString taskName = cols.value(0).trimmed();
        QString status = cols.value(2).trimmed();
        if (taskName.isEmpty()) continue;
        if (m_hideMicrosoft->isChecked() && taskName.startsWith(QStringLiteral("\\Microsoft\\"), Qt::CaseInsensitive)) continue;
        int row = m_startupTable->rowCount();
        m_startupTable->insertRow(row);
        m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(taskName));
        m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("计划任务")));
        m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(QStringLiteral("Task Scheduler")));
        bool disabled = status.contains(QStringLiteral("Disabled"), Qt::CaseInsensitive) ||
                        status.contains(QStringLiteral("已禁用"));
        auto* stateItem = new QTableWidgetItem(disabled ? QStringLiteral("已禁用") : QStringLiteral("启用"));
        if (disabled) stateItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_State, stateItem);
        auto* pathItem = new QTableWidgetItem(QStringLiteral("任务计划: %1").arg(taskName));
        if (disabled) pathItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_Path, pathItem);
        tagStartupRow(m_startupTable, row, QStringLiteral("计划任务"));
        if (disabled) {
            for (int c = SS_Name; c <= SS_Source; ++c)
                m_startupTable->item(row, c)->setForeground(kDisabledColor);
        }
        m_startupTable->item(row, SS_Path)->setToolTip(taskName);
    }
}

// StartupApproved 键路径（root: 1=HKLM 2=HKCU）
static QString approvedPath(quint32 root, const QString& approvedKey)
{
    return (root == 1
        ? QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\")
        : QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\"))
        + approvedKey;
}

static QString startupRegPath(quint32 root, const QString& source)
{
    const QString hive = root == 1 ? QStringLiteral("HKEY_LOCAL_MACHINE\\")
                                   : QStringLiteral("HKEY_CURRENT_USER\\");
    if (source.contains(QStringLiteral("Policies\\Explorer\\Run"))) {
        return hive + (root == 1 ? QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run")
                                 : QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run"));
    }
    const QString base = root == 1
        ? QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\")
        : QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\");
    if (source.contains(QStringLiteral("OnceEx"))) return base + QStringLiteral("RunOnceEx");
    if (source.contains(QStringLiteral("Once"))) return base + QStringLiteral("RunOnce");
    return base + QStringLiteral("Run");
}

static QString disabledStartupRegPath(quint32 root, const QString& approvedKey)
{
    return (root == 1
        ? QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\EFCHSwordNeo\\DisabledStartup\\")
        : QStringLiteral("HKEY_CURRENT_USER\\Software\\EFCHSwordNeo\\DisabledStartup\\")) + approvedKey;
}

bool StartupPage::isStartupApprovedDisabled(quint32 root, const QString& approvedKey,
                                            const QString& name) const
{
    QSettings s(approvedPath(root, approvedKey), QSettings::NativeFormat);
    QByteArray data = s.value(name).toByteArray();
    if (data.size() < 4) return false;      // 无记录 = 启用
    return (quint8)data[0] & 0x01;          // bit0=1 为禁用（03 00 00 00）
}

void StartupPage::setStartupApprovedDisabled(quint32 root, const QString& approvedKey,
                                             const QString& name, bool disabled)
{
    // 12 字节: 状态(4) + FILETIME(8)。启用=02 00 00 00, 禁用=03 00 00 00+时间戳
    QByteArray data(12, '\0');
    data[0] = disabled ? 0x03 : 0x02;
    if (disabled) {
        FILETIME ftRaw{};
        GetSystemTimeAsFileTime(&ftRaw);
        ULONGLONG ft = (static_cast<ULONGLONG>(ftRaw.dwHighDateTime) << 32) | ftRaw.dwLowDateTime;
        memcpy(data.data() + 4, &ft, sizeof(ft));
    }
    QSettings s(approvedPath(root, approvedKey), QSettings::NativeFormat);
    s.setValue(name, data);
}

void StartupPage::enumDirectoryStartup(const QString& dir, const QString& source, bool userScope)
{
    QDir d(dir);
    if (!d.exists()) return;

    QFileInfoList entries = d.entryInfoList(QDir::Files);
    for (const auto& fi : entries) {
        // 隐藏微软自带的 OneNote/OneDrive 等占位项
        if (m_hideMicrosoft->isChecked() &&
            fi.fileName().contains(QStringLiteral("OneNote"), Qt::CaseInsensitive)) {
            continue;
        }

        bool disabled = isStartupApprovedDisabled(
            userScope ? 2 : 1, QStringLiteral("StartupFolder"), fi.fileName());

        int row = m_startupTable->rowCount();
        m_startupTable->insertRow(row);
        m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(fi.fileName()));
        m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("快捷方式/脚本")));
        m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(source));
        auto* stateItem = new QTableWidgetItem(disabled ? QStringLiteral("已禁用") : QStringLiteral("启用"));
        if (disabled) stateItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_State, stateItem);
        auto* pathItem = new QTableWidgetItem(fi.absoluteFilePath());
        if (disabled) pathItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_Path, pathItem);
        tagStartupRow(m_startupTable, row, QStringLiteral("启动文件夹"));
        if (disabled) {
            m_startupTable->item(row, SS_Name)->setForeground(kDisabledColor);
            m_startupTable->item(row, SS_Type)->setForeground(kDisabledColor);
            m_startupTable->item(row, SS_Source)->setForeground(kDisabledColor);
        }
    }

    // 真正禁用的启动文件会移动到 .EFCHDisabled，避免仅依赖
    // StartupApproved（部分系统/启动器会忽略该标志）而再次执行。
    QDir disabledDir(dir + QStringLiteral("/.EFCHDisabled"));
    if (disabledDir.exists()) {
        const QFileInfoList disabledEntries = disabledDir.entryInfoList(QDir::Files | QDir::Hidden);
        for (const QFileInfo& fi : disabledEntries) {
            if (m_hideMicrosoft->isChecked() &&
                fi.fileName().contains(QStringLiteral("OneNote"), Qt::CaseInsensitive)) continue;
            const QString originalPath = QDir(dir).absoluteFilePath(fi.fileName());
            int row = m_startupTable->rowCount();
            m_startupTable->insertRow(row);
            m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(fi.fileName()));
            m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("快捷方式/脚本")));
            m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(source));
            auto* stateItem = new QTableWidgetItem(QStringLiteral("已禁用"));
            stateItem->setForeground(kDisabledColor);
            m_startupTable->setItem(row, SS_State, stateItem);
            auto* pathItem = new QTableWidgetItem(originalPath);
            pathItem->setData(Qt::UserRole, fi.absoluteFilePath());
            pathItem->setForeground(kDisabledColor);
            m_startupTable->setItem(row, SS_Path, pathItem);
            tagStartupRow(m_startupTable, row, QStringLiteral("启动文件夹"));
            for (int c = SS_Name; c <= SS_Source; ++c) m_startupTable->item(row, c)->setForeground(kDisabledColor);
        }
    }
}

void StartupPage::enumRegistryStartup(quint32 root, const QString& path, const QString& source,
                                      const QString& approvedKey)
{
    QString regPath;
    if (root == 1) regPath = QStringLiteral("HKEY_LOCAL_MACHINE\\") + path;
    else if (root == 2) regPath = QStringLiteral("HKEY_CURRENT_USER\\") + path;
    else return;

    QSettings settings(regPath, QSettings::NativeFormat);
    QStringList keys = settings.childKeys();
    // Many persistence locations (BHO, Shell extensions, Winlogon Notify,
    // LSA packages) are represented as subkeys rather than value names.
    // Include those groups as startup records as well.
    for (const QString& group : settings.childGroups()) {
        if (!keys.contains(group)) keys.push_back(group);
    }
    for (const auto& key : keys) {
        QString value = settings.value(key).toString();
        if (value.isEmpty()) {
            QSettings sub(regPath + QStringLiteral("\\") + key, QSettings::NativeFormat);
            const QStringList subKeys = sub.childKeys();
            if (!subKeys.isEmpty()) value = sub.value(subKeys.first()).toString();
            if (value.isEmpty()) value = QStringLiteral("(子项: %1)").arg(key);
        }

        // 隐藏微软自带项（按命令行关键字的粗略过滤）
        if (m_hideMicrosoft->isChecked()) {
            QString lower = value.toLower();
            if (lower.contains(QStringLiteral("\\windows\\")) ||
                lower.contains(QStringLiteral("onedrive")) ||
                lower.contains(QStringLiteral("microsoft\\coffceloader"))) {
                continue;
            }
        }

        bool disabled = isStartupApprovedDisabled(root, approvedKey, key);

        int row = m_startupTable->rowCount();
        m_startupTable->insertRow(row);
        m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(key));
        m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("注册表")));
        m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(source));

        auto* stateItem = new QTableWidgetItem(disabled ? QStringLiteral("已禁用") : QStringLiteral("启用"));
        if (disabled) stateItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_State, stateItem);

        auto* pathItem = new QTableWidgetItem(value);
        if (disabled) pathItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_Path, pathItem);
        tagStartupRow(m_startupTable, row, classifyStartupSource(source));
        if (disabled) {
            m_startupTable->item(row, SS_Name)->setForeground(kDisabledColor);
            m_startupTable->item(row, SS_Type)->setForeground(kDisabledColor);
            m_startupTable->item(row, SS_Source)->setForeground(kDisabledColor);
        }
    }

    // 展示已移动到备份键的条目，确保用户可恢复并明确看到禁用状态。
    if (source.contains(QStringLiteral("Policies\\Explorer\\Run"), Qt::CaseInsensitive)) {
        return;
    }
    QSettings backup(disabledStartupRegPath(root, approvedKey), QSettings::NativeFormat);
    for (const QString& key : backup.childKeys()) {
        if (settings.contains(key)) continue;
        int row = m_startupTable->rowCount();
        m_startupTable->insertRow(row);
        m_startupTable->setItem(row, SS_Name, new QTableWidgetItem(key));
        m_startupTable->setItem(row, SS_Type, new QTableWidgetItem(QStringLiteral("注册表")));
        m_startupTable->setItem(row, SS_Source, new QTableWidgetItem(source));
        auto* stateItem = new QTableWidgetItem(QStringLiteral("已禁用"));
        stateItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_State, stateItem);
        auto* pathItem = new QTableWidgetItem(backup.value(key).toString());
        pathItem->setForeground(kDisabledColor);
        m_startupTable->setItem(row, SS_Path, pathItem);
        tagStartupRow(m_startupTable, row, classifyStartupSource(source));
        for (int c = SS_Name; c <= SS_Source; ++c) m_startupTable->item(row, c)->setForeground(kDisabledColor);
    }
}

void StartupPage::onCreateStartup()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建启动项"));
    dlg.setMinimumWidth(460);
    auto* form = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QStringLiteral("显示在启动列表里的名称"));
    form->addRow(QStringLiteral("名称:"), nameEdit);

    auto* locationCombo = new QComboBox(&dlg);
    locationCombo->addItems({
        QStringLiteral("HKCU Run (当前用户, 每次登录)"),
        QStringLiteral("HKCU RunOnce (当前用户, 仅下次登录)"),
        QStringLiteral("HKLM Run (所有用户, 每次登录)"),
        QStringLiteral("HKLM RunOnce (所有用户, 仅下次登录)"),
        QStringLiteral("当前用户启动文件夹"),
        QStringLiteral("所有用户启动文件夹")});
    form->addRow(QStringLiteral("位置:"), locationCombo);

    auto* valueTypeCombo = new QComboBox(&dlg);
    valueTypeCombo->addItems({
        QStringLiteral("REG_SZ (普通字符串)"),
        QStringLiteral("REG_EXPAND_SZ (可扩充字符串, 支持 %变量%)")});
    form->addRow(QStringLiteral("值类型:"), valueTypeCombo);

    auto* commandEdit = new QLineEdit(&dlg);
    commandEdit->setPlaceholderText(QStringLiteral("程序完整路径, 如 C:\\Tools\\svc.exe"));
    form->addRow(QStringLiteral("命令/程序:"), commandEdit);

    auto* argsEdit = new QLineEdit(&dlg);
    argsEdit->setPlaceholderText(QStringLiteral("可选, 命令行参数"));
    form->addRow(QStringLiteral("参数:"), argsEdit);

    auto* workdirEdit = new QLineEdit(&dlg);
    workdirEdit->setPlaceholderText(QStringLiteral("可选, 仅启动文件夹脚本生效"));
    form->addRow(QStringLiteral("工作目录:"), workdirEdit);

    auto* createDisabled = new QCheckBox(QStringLiteral("创建为禁用状态"), &dlg);
    form->addRow(QString(), createDisabled);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    QString name = nameEdit->text().trimmed();
    QString command = commandEdit->text().trimmed();
    if (name.isEmpty() || command.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新建启动项"), QStringLiteral("名称和命令不能为空"));
        return;
    }
    QString args = argsEdit->text().trimmed();
    QString fullCommand = args.isEmpty()
        ? (command.contains(' ') ? QStringLiteral("\"%1\"").arg(command) : command)
        : QStringLiteral("\"%1\" %2").arg(command, args);

    int loc = locationCombo->currentIndex();
    bool success = false;

    if (loc <= 3) {
        // 注册表 Run / RunOnce
        bool hklm = (loc >= 2);
        QString path;
        if (loc == 0) path = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        if (loc == 1) path = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce");
        if (loc == 2) path = QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
        if (loc == 3) path = QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce");
        QString regPath = (hklm ? QStringLiteral("HKEY_LOCAL_MACHINE\\") : QStringLiteral("HKEY_CURRENT_USER\\"))
            + path;

        HKEY hKey = nullptr;
        HKEY hRoot = hklm ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        LONG rc = RegCreateKeyExW(hRoot, (const wchar_t*)path.utf16(), 0, nullptr,
                                  0, KEY_SET_VALUE, nullptr, &hKey, nullptr);
        if (rc == ERROR_SUCCESS) {
            DWORD type = (valueTypeCombo->currentIndex() == 1) ? REG_EXPAND_SZ : REG_SZ;
            QByteArray bytes((const char*)fullCommand.utf16(),
                             (int)(fullCommand.size() + 1) * (int)sizeof(wchar_t));
            rc = RegSetValueExW(hKey, (const wchar_t*)name.utf16(), 0, type,
                                (const BYTE*)bytes.constData(), (DWORD)bytes.size());
            RegCloseKey(hKey);
            success = (rc == ERROR_SUCCESS);
        }
        if (success) {
            QString approvedKey = (loc == 1) ? QStringLiteral("RunOnce") : QStringLiteral("Run");
            setStartupApprovedDisabled(hklm ? 1 : 2, approvedKey, name, createDisabled->isChecked());
        } else {
            QMessageBox::warning(this, QStringLiteral("新建启动项"),
                QStringLiteral("写入注册表失败 (错误码: %1), 请以管理员身份运行").arg(rc));
            return;
        }
    } else {
        // 启动文件夹: 生成 .cmd 脚本
        wchar_t dirBuf[MAX_PATH] = {0};
        SHGetFolderPathW(nullptr,
                         (loc == 5) ? CSIDL_COMMON_STARTUP : CSIDL_STARTUP,
                         nullptr, 0, dirBuf);
        QString dir = QString::fromWCharArray(dirBuf);
        QString scriptPath = QDir(dir).filePath(name + QStringLiteral(".cmd"));

        QString workdir = workdirEdit->text().trimmed();
        QString content;
        if (!workdir.isEmpty()) {
            content += QStringLiteral("@cd /d \"%1\"\r\n").arg(workdir);
        }
        content += QStringLiteral("@start \"\" %1\r\n").arg(fullCommand);

        QFile f(scriptPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(content.toLocal8Bit());
            f.close();
            success = true;
            setStartupApprovedDisabled(loc == 5 ? 1 : 2, QStringLiteral("StartupFolder"),
                                       name + QStringLiteral(".cmd"), createDisabled->isChecked());
        } else {
            QMessageBox::warning(this, QStringLiteral("新建启动项"),
                QStringLiteral("写入脚本失败: %1").arg(scriptPath));
            return;
        }
    }

    if (success) {
        QMessageBox::information(this, QStringLiteral("新建启动项"),
            QStringLiteral("启动项已创建: %1").arg(name));
        onRefreshStartup();
    }
}

void StartupPage::onCreateTask()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建计划任务"),
        QStringLiteral("任务名称:"), QLineEdit::Normal, QStringLiteral("EFCHSwordTask"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString command = QInputDialog::getText(this, QStringLiteral("新建计划任务"),
        QStringLiteral("程序或命令行:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || command.isEmpty()) return;
    QProcess p;
    p.start(QStringLiteral("schtasks.exe"), {QStringLiteral("/Create"), QStringLiteral("/TN"), name,
        QStringLiteral("/TR"), command, QStringLiteral("/SC"), QStringLiteral("ONLOGON"),
        QStringLiteral("/F")});
    if (!p.waitForFinished(3000) || p.exitCode() != 0) {
        QMessageBox::warning(this, QStringLiteral("计划任务"),
            QStringLiteral("创建失败: %1").arg(QString::fromLocal8Bit(p.readAllStandardError())));
    }
    onRefreshStartup();
}

void StartupPage::onStartupContextMenu(const QPoint& pos)
{
    QModelIndex index = m_startupTable->indexAt(pos);
    if (!index.isValid()) return;
    m_startupTable->selectRow(index.row());
    m_startupMenu->exec(m_startupTable->viewport()->mapToGlobal(pos));
}

void StartupPage::onToggleStartup()
{
    int row = m_startupTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_startupTable->item(row, SS_Name);
    auto* sourceItem = m_startupTable->item(row, SS_Source);
    auto* stateItem = m_startupTable->item(row, SS_State);
    if (!nameItem || !sourceItem || !stateItem) return;

    QString source = sourceItem->text();
    QString name = nameItem->text();
    bool currentlyDisabled = (stateItem->text() == QStringLiteral("已禁用"));
    QString displayedPath = m_startupTable->item(row, SS_Path)
        ? m_startupTable->item(row, SS_Path)->text() : QString();
    QString actualPath = m_startupTable->item(row, SS_Path)
        ? m_startupTable->item(row, SS_Path)->data(Qt::UserRole).toString() : QString();
    if (actualPath.isEmpty()) actualPath = displayedPath;

    if (source.contains(QStringLiteral("Winlogon"))) {
        QMessageBox::information(this, QStringLiteral("EFCHSword"),
            QStringLiteral("Winlogon 项不支持禁用"));
        return;
    }

    if (source.startsWith(QStringLiteral("Task Scheduler"))) {
        QProcess p;
        QStringList args{QStringLiteral("/Change"), QStringLiteral("/TN"), name,
                         currentlyDisabled ? QStringLiteral("/ENABLE") : QStringLiteral("/DISABLE")};
        p.start(QStringLiteral("schtasks.exe"), args);
        p.waitForFinished(2500);
        if (p.exitCode() != 0) {
            QMessageBox::warning(this, QStringLiteral("启动项"),
                QStringLiteral("计划任务修改失败 (错误码: %1)\n%2")
                    .arg(p.exitCode()).arg(QString::fromLocal8Bit(p.readAllStandardError())));
        }
        if (p.state() == QProcess::NotRunning) onRefreshStartup();
        return;
    }

    quint32 root = source.startsWith(QStringLiteral("HKLM")) ? 1 : 2;
    if (source.contains(QStringLiteral("启动文件夹"))) {
        QFileInfo fi(actualPath);
        QString dir = fi.absolutePath();
        if (!currentlyDisabled) {
            QDir disabledDir(dir + QStringLiteral("/.EFCHDisabled"));
            if (!disabledDir.exists() && !QDir().mkpath(disabledDir.absolutePath())) {
                QMessageBox::warning(this, QStringLiteral("启动项"), QStringLiteral("无法创建禁用备份目录"));
                return;
            }
            QString dest = disabledDir.absoluteFilePath(fi.fileName());
            if (!QFile::rename(actualPath, dest)) {
                QMessageBox::warning(this, QStringLiteral("启动项"), QStringLiteral("无法移动启动文件，可能需要管理员权限"));
                return;
            }
            setStartupApprovedDisabled(root, QStringLiteral("StartupFolder"), name, true);
        } else {
            QString originalDir = QFileInfo(displayedPath).absolutePath();
            QString dest = QDir(originalDir).absoluteFilePath(name);
            if (!QFile::rename(actualPath, dest)) {
                QMessageBox::warning(this, QStringLiteral("启动项"), QStringLiteral("无法恢复启动文件，可能需要管理员权限"));
                return;
            }
            setStartupApprovedDisabled(root, QStringLiteral("StartupFolder"), name, false);
        }
    } else if (source.contains(QStringLiteral("RunOnce"))) {
        const QString approvedKey = source.contains(QStringLiteral("OnceEx")) ? QStringLiteral("RunOnceEx") : QStringLiteral("RunOnce");
        QSettings settings(startupRegPath(root, source), QSettings::NativeFormat);
        QSettings backup(disabledStartupRegPath(root, approvedKey), QSettings::NativeFormat);
        if (!currentlyDisabled) {
            if (settings.contains(name)) {
                backup.setValue(name, settings.value(name));
                settings.remove(name);
            }
            setStartupApprovedDisabled(root, approvedKey, name, true);
        } else {
            if (backup.contains(name)) {
                settings.setValue(name, backup.value(name));
                backup.remove(name);
            }
            setStartupApprovedDisabled(root, approvedKey, name, false);
        }
    } else {
        const QString approvedKey = QStringLiteral("Run");
        QSettings settings(startupRegPath(root, source), QSettings::NativeFormat);
        QSettings backup(disabledStartupRegPath(root, approvedKey), QSettings::NativeFormat);
        if (!currentlyDisabled) {
            if (settings.contains(name)) {
                backup.setValue(name, settings.value(name));
                settings.remove(name);
            }
            setStartupApprovedDisabled(root, approvedKey, name, true);
        } else {
            if (backup.contains(name)) {
                settings.setValue(name, backup.value(name));
                backup.remove(name);
            }
            setStartupApprovedDisabled(root, approvedKey, name, false);
        }
    }

    onRefreshStartup();
}

void StartupPage::onDeleteStartup()
{
    int row = m_startupTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_startupTable->item(row, SS_Name);
    auto* sourceItem = m_startupTable->item(row, SS_Source);
    auto* pathItem = m_startupTable->item(row, SS_Path);
    if (!nameItem || !sourceItem) return;

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定删除启动项: %1 吗?").arg(nameItem->text()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QString source = sourceItem->text();
    QString name = nameItem->text();
    QString path = pathItem ? pathItem->text() : QString();

    if (source.startsWith(QStringLiteral("Task Scheduler"))) {
        QProcess p;
        p.start(QStringLiteral("schtasks.exe"), {QStringLiteral("/Delete"), QStringLiteral("/TN"), name, QStringLiteral("/F")});
        if (!p.waitForFinished(3000) || p.exitCode() != 0) {
            QMessageBox::warning(this, QStringLiteral("启动项"),
                QStringLiteral("计划任务删除失败: %1").arg(QString::fromLocal8Bit(p.readAllStandardError())));
        }
    } else if (source.contains(QStringLiteral("启动文件夹"))) {
        QString actualPath = pathItem ? pathItem->data(Qt::UserRole).toString() : QString();
        if (actualPath.isEmpty()) actualPath = path;
        QFile::remove(actualPath);
    } else if (source.contains(QStringLiteral("Run")) || source.contains(QStringLiteral("RunOnce"))) {
        quint32 root = source.startsWith(QStringLiteral("HKLM")) ? 1 : 2;
        QString regPath = startupRegPath(root, source);
        QSettings settings(regPath, QSettings::NativeFormat);
        settings.remove(name);

        // 清理 StartupApproved 记录
        QString approvedKey = source.contains(QStringLiteral("Once")) && !source.contains(QStringLiteral("OnceEx"))
            ? QStringLiteral("RunOnce") : QStringLiteral("Run");
        QSettings approved(approvedPath(source.startsWith(QStringLiteral("HKLM")) ? 1 : 2, approvedKey),
                           QSettings::NativeFormat);
        approved.remove(name);
        QSettings backup(disabledStartupRegPath(root, approvedKey), QSettings::NativeFormat);
        backup.remove(name);
    }

    onRefreshStartup();
}

void StartupPage::onOpenFileLocation()
{
    int row = m_startupTable->currentRow();
    if (row < 0) return;
    auto* pathItem = m_startupTable->item(row, SS_Path);
    if (!pathItem) return;
    QString path = pathItem->text();
    if (path.isEmpty()) return;

    path.remove(QStringLiteral("\\??\\"));
    path.remove(QStringLiteral("\""));
    path = path.section(' ', 0, 0);

    QProcess::startDetached(QStringLiteral("explorer.exe /select,\"%1\"").arg(path));
}

void StartupPage::onCopyPath()
{
    int row = m_startupTable->currentRow();
    if (row < 0) return;
    auto* item = m_startupTable->item(row, SS_Path);
    if (item) QGuiApplication::clipboard()->setText(item->text());
}

// ==================== 服务 ====================

void StartupPage::onRefreshServices()
{
    m_serviceTable->setRowCount(0);

    QSettings services(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services"),
                       QSettings::NativeFormat);
    QStringList groups = services.childGroups();

    int shown = 0;
    for (const auto& svcName : groups) {
        if (svcName.isEmpty()) continue;

        services.beginGroup(svcName);
        int start = services.value(QStringLiteral("Start"), -1).toInt();
        QString imagePath = services.value(QStringLiteral("ImagePath")).toString();
        int type = services.value(QStringLiteral("Type"), -1).toInt();
        QString displayName = services.value(QStringLiteral("DisplayName")).toString();
        services.endGroup();

        if (imagePath.isEmpty()) continue;

        bool isDriver = (type == 1 || type == 2);
        if (m_hideDriverSvc->isChecked() && isDriver) continue;
        if (m_onlyAutoSvc->isChecked() && start != 2) continue;

        QString startType;
        bool disabled = false;
        switch (start) {
            case 0: startType = QStringLiteral("引导启动"); break;
            case 1: startType = QStringLiteral("系统启动"); break;
            case 2: startType = QStringLiteral("自动"); break;
            case 3: startType = QStringLiteral("手动"); break;
            case 4: startType = QStringLiteral("禁用"); disabled = true; break;
            default: startType = QStringLiteral("未知"); break;
        }

        appendServiceRow(svcName, startType, displayName, imagePath, disabled);
        ++shown;
    }

    m_serviceStatusLabel->setText(QStringLiteral("服务: %1").arg(shown));
}

void StartupPage::appendServiceRow(const QString& name, const QString& startType,
                                   const QString& displayName, const QString& imagePath,
                                   bool disabled)
{
    int row = m_serviceTable->rowCount();
    m_serviceTable->insertRow(row);
    m_serviceTable->setItem(row, SV_Name, new QTableWidgetItem(name));
    m_serviceTable->setItem(row, SV_StartType, new QTableWidgetItem(startType));
    m_serviceTable->setItem(row, SV_DisplayName, new QTableWidgetItem(displayName));

    auto* stateItem = new QTableWidgetItem(disabled ? QStringLiteral("已禁用") : QStringLiteral("-"));
    if (disabled) {
        stateItem->setForeground(kDisabledColor);
        m_serviceTable->setItem(row, SV_State, stateItem);
        for (int c : { SV_Name, SV_StartType, SV_DisplayName }) {
            m_serviceTable->item(row, c)->setForeground(kDisabledColor);
        }
    } else {
        m_serviceTable->setItem(row, SV_State, stateItem);
    }

    auto* pathItem = new QTableWidgetItem(imagePath);
    if (disabled) pathItem->setForeground(kDisabledColor);
    m_serviceTable->setItem(row, SV_Path, pathItem);
}

void StartupPage::onServiceContextMenu(const QPoint& pos)
{
    QModelIndex index = m_serviceTable->indexAt(pos);
    if (!index.isValid()) return;
    m_serviceTable->selectRow(index.row());
    m_serviceMenu->exec(m_serviceTable->viewport()->mapToGlobal(pos));
}

void StartupPage::onCreateService()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建服务"));
    dlg.setMinimumWidth(460);
    auto* form = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QStringLiteral("服务内部名称 (注册表键名), 如 MySvc"));
    form->addRow(QStringLiteral("服务名:"), nameEdit);

    auto* displayEdit = new QLineEdit(&dlg);
    displayEdit->setPlaceholderText(QStringLiteral("可选, 服务管理器中显示的名称"));
    form->addRow(QStringLiteral("显示名:"), displayEdit);

    auto* descEdit = new QLineEdit(&dlg);
    descEdit->setPlaceholderText(QStringLiteral("可选, 服务描述"));
    form->addRow(QStringLiteral("描述:"), descEdit);

    auto* pathEdit = new QLineEdit(&dlg);
    pathEdit->setPlaceholderText(QStringLiteral("二进制完整路径 (exe 或 sys)"));
    auto* browseBtn = new QPushButton(QStringLiteral("浏览..."), &dlg);
    connect(browseBtn, &QPushButton::clicked, this, [this, pathEdit]() {
        QString file = QFileDialog::getOpenFileName(this,
            QStringLiteral("选择服务二进制"), QString(),
            QStringLiteral("程序 (*.exe *.sys);;所有文件 (*.*)"));
        if (!file.isEmpty()) pathEdit->setText(file);
    });
    {
        auto* pathRow = new QHBoxLayout();
        pathRow->addWidget(pathEdit, 1);
        pathRow->addWidget(browseBtn);
        form->addRow(QStringLiteral("二进制路径:"), pathRow);
    }

    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItem(QStringLiteral("Win32 独立进程 (SERVICE_WIN32_OWN_PROCESS)"), (quint32)0x10);
    typeCombo->addItem(QStringLiteral("Win32 共享进程 (SERVICE_WIN32_SHARE_PROCESS)"), (quint32)0x20);
    typeCombo->addItem(QStringLiteral("内核驱动 (SERVICE_KERNEL_DRIVER)"), (quint32)0x1);
    typeCombo->addItem(QStringLiteral("文件系统驱动 (SERVICE_FILE_SYSTEM_DRIVER)"), (quint32)0x2);
    typeCombo->addItem(QStringLiteral("自己进程+交互 (OWN_PROCESS + INTERACTIVE)"), (quint32)0x110);
    form->addRow(QStringLiteral("类型:"), typeCombo);

    auto* startCombo = new QComboBox(&dlg);
    startCombo->addItem(QStringLiteral("自动 (SERVICE_AUTO_START)"), (quint32)2);
    startCombo->addItem(QStringLiteral("手动 (SERVICE_DEMAND_START)"), (quint32)3);
    startCombo->addItem(QStringLiteral("禁用 (SERVICE_DISABLED)"), (quint32)4);
    startCombo->addItem(QStringLiteral("引导启动 (SERVICE_BOOT_START, 仅驱动)"), (quint32)0);
    startCombo->addItem(QStringLiteral("系统启动 (SERVICE_SYSTEM_START, 仅驱动)"), (quint32)1);
    startCombo->setCurrentIndex(1);
    form->addRow(QStringLiteral("启动类型:"), startCombo);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    QString name = nameEdit->text().trimmed();
    QString path = pathEdit->text().trimmed();
    if (name.isEmpty() || path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新建服务"),
            QStringLiteral("服务名和二进制路径不能为空"));
        return;
    }

    SC_HANDLE hMgr = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hMgr) {
        QMessageBox::warning(this, QStringLiteral("新建服务"),
            QStringLiteral("打开 SCM 失败 (错误码: %1), 请以管理员身份运行").arg(GetLastError()));
        return;
    }

    QString display = displayEdit->text().trimmed().isEmpty()
        ? name : displayEdit->text().trimmed();
    SC_HANDLE hSvc = CreateServiceW(
        hMgr,
        (const wchar_t*)name.utf16(),
        (const wchar_t*)display.utf16(),
        SERVICE_ALL_ACCESS,
        (DWORD)typeCombo->currentData().toUInt(),
        (DWORD)startCombo->currentData().toUInt(),
        SERVICE_ERROR_NORMAL,
        (const wchar_t*)QDir::toNativeSeparators(path).utf16(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hMgr);
        QMessageBox::warning(this, QStringLiteral("新建服务"),
            QStringLiteral("创建服务失败 (错误码: %1)%2")
                .arg(err)
                .arg(err == ERROR_SERVICE_EXISTS ? QStringLiteral("\n服务已存在") : QString()));
        return;
    }

    // 描述
    QString desc = descEdit->text().trimmed();
    if (!desc.isEmpty()) {
        SERVICE_DESCRIPTIONW sd;
        sd.lpDescription = (LPWSTR)(const wchar_t*)desc.utf16();
        ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hMgr);

    QMessageBox::information(this, QStringLiteral("新建服务"),
        QStringLiteral("服务已创建: %1").arg(name));
    onRefreshServices();
}

void StartupPage::onServiceStartType(int startType)
{
    int row = m_serviceTable->currentRow();
    if (row < 0) return;
    auto* nameItem = m_serviceTable->item(row, SV_Name);
    if (!nameItem) return;

    SC_HANDLE hMgr = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hMgr) {
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("打开 SCM 失败 (错误码: %1)").arg(GetLastError()));
        return;
    }
    SC_HANDLE hSvc = OpenServiceW(hMgr, (const wchar_t*)nameItem->text().utf16(),
                                  SERVICE_CHANGE_CONFIG);
    if (!hSvc) {
        CloseServiceHandle(hMgr);
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("打开服务失败 (错误码: %1)").arg(GetLastError()));
        return;
    }

    BOOL ok = ChangeServiceConfigW(hSvc, SERVICE_NO_CHANGE, (DWORD)startType,
                                   SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hMgr);

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("修改启动类型失败 (错误码: %1)").arg(GetLastError()));
    }
    onRefreshServices();
}

void StartupPage::onServiceSafeBoot(int which)
{
    // 原理: SafeBoot\Minimal\<服务名> / SafeBoot\Network\<服务名> 键存在且
    // 默认值为 "Service" 时, 该服务才会在对应模式的安全模式下启动。
    int row = m_serviceTable->currentRow();
    if (row < 0) return;
    auto* nameItem = m_serviceTable->item(row, SV_Name);
    if (!nameItem) return;

    QString name = nameItem->text();
    QString subKey = QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\%1\\%2")
        .arg(which == 0 ? QStringLiteral("Minimal") : QStringLiteral("Network"), name);

    HKEY hKey = nullptr;
    bool exists = (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (const wchar_t*)subKey.utf16(),
                                 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS);
    if (exists) {
        RegCloseKey(hKey);
    }

    if (exists) {
        if (QMessageBox::question(this, QStringLiteral("服务"),
                QStringLiteral("取消服务 %1 的安全模式(%2)启动许可?")
                    .arg(name, which == 0 ? QStringLiteral("最小化") : QStringLiteral("带网络")),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
        LONG rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, (const wchar_t*)subKey.utf16());
        if (rc != ERROR_SUCCESS) {
            QMessageBox::warning(this, QStringLiteral("服务"),
                QStringLiteral("删除 SafeBoot 键失败 (错误码: %1)").arg(rc));
        }
        return;
    }

    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, (const wchar_t*)subKey.utf16(),
                              0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc == ERROR_SUCCESS) {
        const wchar_t* typeStr = L"Service";
        rc = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                            (const BYTE*)typeStr, (DWORD)((wcslen(typeStr) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }
    if (rc != ERROR_SUCCESS) {
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("写入 SafeBoot 键失败 (错误码: %1)").arg(rc));
    }
}

void StartupPage::onDeleteService()
{
    int row = m_serviceTable->currentRow();
    if (row < 0) return;
    auto* nameItem = m_serviceTable->item(row, SV_Name);
    if (!nameItem) return;

    QString name = nameItem->text();
    if (QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("确定删除服务: %1 吗?\n\n"
                           "服务会被标记删除 (运行中的服务将在停止后移除)。").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    SC_HANDLE hMgr = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hMgr) {
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("打开 SCM 失败 (错误码: %1)").arg(GetLastError()));
        return;
    }
    SC_HANDLE hSvc = OpenServiceW(hMgr, (const wchar_t*)name.utf16(), DELETE);
    if (!hSvc) {
        CloseServiceHandle(hMgr);
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("打开服务失败 (错误码: %1)").arg(GetLastError()));
        return;
    }

    BOOL ok = DeleteService(hSvc);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hMgr);

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("服务"),
            QStringLiteral("删除服务失败 (错误码: %1)").arg(GetLastError()));
    }
    onRefreshServices();
}

void StartupPage::onCopyServicePath()
{
    int row = m_serviceTable->currentRow();
    if (row < 0) return;
    auto* item = m_serviceTable->item(row, SV_Path);
    if (item) QGuiApplication::clipboard()->setText(item->text());
}
