#include "FilePage.h"
#include "AppContext.h"
#include "FilePropertiesDialog.h"
#include "FileReaderDialog.h"
#include "DeleteDirProgressDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QProcess>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileIconProvider>
#include <QRadioButton>
#include <QButtonGroup>
#include <algorithm>

#include <windows.h>
#include <shellapi.h>

// 文件列
enum FileColumn {
    FC_Name = 0,
    FC_Type,
    FC_Size,
    FC_Created,
    FC_Modified,
    FC_Count
};

FilePage::FilePage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupContextMenu();
    setupDrives();
}

FilePage::~FilePage() = default;

void FilePage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // 工具栏
    auto* toolLayout = new QHBoxLayout();
    m_driveCombo = new QComboBox(this);
    auto* upBtn = new QPushButton(QStringLiteral("上一级"), this);
    auto* gotoBtn = new QPushButton(QStringLiteral("转到"), this);
    m_pathEdit = new QLineEdit(this);
    m_fastScan = new QCheckBox(QStringLiteral("快速扫描"), this);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);

    // 排序模式选择
    m_sortMixedRadio = new QRadioButton(QStringLiteral("文件名混排"), this);
    m_sortFolderFirstRadio = new QRadioButton(QStringLiteral("文件夹优先"), this);
    m_sortFolderFirstRadio->setChecked(true);  // 默认文件夹优先
    auto* sortGroup = new QButtonGroup(this);
    sortGroup->addButton(m_sortMixedRadio);
    sortGroup->addButton(m_sortFolderFirstRadio);

    // 文件操作模式: IRP (经文件系统) / DirectNTFS (直接解析NTFS, 只读)
    m_modeIrpRadio = new QRadioButton(QStringLiteral("IRP"), this);
    m_modeDirectNtfsRadio = new QRadioButton(QStringLiteral("DirectNTFS"), this);
    m_modeIrpRadio->setChecked(true);  // 默认 IRP
    m_modeDirectNtfsRadio->setToolTip(QStringLiteral("DirectNTFS: 绕过文件系统直接解析NTFS结构, "
        "可读取被文件系统过滤驱动隐藏的文件, 但只支持读取(列出/阅读), 写入/删除等操作仍用IRP"));
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_modeIrpRadio);
    modeGroup->addButton(m_modeDirectNtfsRadio);

    connect(m_driveCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onDriveChanged(int)));
    connect(upBtn, &QPushButton::clicked, this, &FilePage::onUpClicked);
    connect(gotoBtn, &QPushButton::clicked, this, &FilePage::onGotoClicked);
    connect(refreshBtn, &QPushButton::clicked, this, &FilePage::onRefreshClicked);
    connect(m_fastScan, &QCheckBox::toggled, this, &FilePage::onFastScanToggled);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &FilePage::onGotoClicked);
    connect(m_sortMixedRadio, &QRadioButton::toggled, this, &FilePage::onSortModeChanged);
    connect(m_sortFolderFirstRadio, &QRadioButton::toggled, this, &FilePage::onSortModeChanged);
    // 切换操作模式时刷新当前目录
    connect(m_modeIrpRadio, &QRadioButton::toggled, this, [this]() {
        if (!m_currentDir.isEmpty()) browseTo(m_currentDir);
    });

    toolLayout->addWidget(m_driveCombo);
    toolLayout->addWidget(upBtn);
    toolLayout->addWidget(m_pathEdit, 1);
    toolLayout->addWidget(gotoBtn);
    toolLayout->addWidget(refreshBtn);
    toolLayout->addWidget(m_fastScan);
    toolLayout->addWidget(new QLabel(QStringLiteral("排序:"), this));
    toolLayout->addWidget(m_sortMixedRadio);
    toolLayout->addWidget(m_sortFolderFirstRadio);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(new QLabel(QStringLiteral("模式:"), this));
    toolLayout->addWidget(m_modeIrpRadio);
    toolLayout->addWidget(m_modeDirectNtfsRadio);
    mainLayout->addLayout(toolLayout);

    // 文件列表
    m_table = new QTableWidget(this);
    m_table->setColumnCount(FC_Count);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("类型"),
        QStringLiteral("大小"),
        QStringLiteral("创建时间"),
        QStringLiteral("修改时间")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setIconSize(QSize(16, 16));
    m_table->setColumnWidth(FC_Name, 250);
    m_table->setColumnWidth(FC_Type, 100);
    m_table->setColumnWidth(FC_Size, 100);
    m_table->setColumnWidth(FC_Created, 150);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &FilePage::onCellDoubleClicked);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &FilePage::onContextMenu);

    mainLayout->addWidget(m_table);

    // 状态栏
    auto* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);
}

void FilePage::setupContextMenu()
{
    // 文件右键菜单
    m_contextMenu = new QMenu(this);

    auto* runAction = m_contextMenu->addAction(QStringLiteral("执行"));
    connect(runAction, &QAction::triggered, this, &FilePage::onRunFile);

    auto* runHiddenAction = m_contextMenu->addAction(QStringLiteral("隐藏运行"));
    connect(runHiddenAction, &QAction::triggered, this, &FilePage::onRunHidden);

    m_contextMenu->addSeparator();

    auto* renameAction = m_contextMenu->addAction(QStringLiteral("重命名"));
    connect(renameAction, &QAction::triggered, this, &FilePage::onRename);

    auto* copyAction = m_contextMenu->addAction(QStringLiteral("复制"));
    connect(copyAction, &QAction::triggered, this, &FilePage::onCopyFile);

    auto* moveAction = m_contextMenu->addAction(QStringLiteral("移动"));
    connect(moveAction, &QAction::triggered, this, &FilePage::onMoveFile);

    m_contextMenu->addSeparator();

    auto* deleteAction = m_contextMenu->addAction(QStringLiteral("删除"));
    connect(deleteAction, &QAction::triggered, this, &FilePage::onDeleteNormal);

    auto* forceDeleteAction = m_contextMenu->addAction(QStringLiteral("强制删除 (IRP)"));
    connect(forceDeleteAction, &QAction::triggered, this, &FilePage::onForceDelete);

    auto* posixDeleteAction = m_contextMenu->addAction(QStringLiteral("POSIX 强制删除"));
    connect(posixDeleteAction, &QAction::triggered, this, &FilePage::onPosixForceDelete);

    auto* zwDeleteAction = m_contextMenu->addAction(QStringLiteral("ZwDeleteFile (NT路径)"));
    connect(zwDeleteAction, &QAction::triggered, this, &FilePage::onZwDeleteFile);

    m_contextMenu->addSeparator();

    auto* protectAction = m_contextMenu->addAction(QStringLiteral("保护此文件 (占用)"));
    connect(protectAction, &QAction::triggered, this, &FilePage::onProtectFile);

    auto* forceOccupyAction = m_contextMenu->addAction(QStringLiteral("强制占用文件"));
    connect(forceOccupyAction, &QAction::triggered, this, &FilePage::onForceOccupyFile);

    m_contextMenu->addSeparator();

    auto* readAction = m_contextMenu->addAction(QStringLiteral("阅读文件"));
    connect(readAction, &QAction::triggered, this, &FilePage::onReadFile);

    auto* propsAction = m_contextMenu->addAction(QStringLiteral("属性"));
    connect(propsAction, &QAction::triggered, this, &FilePage::onShowProperties);

    auto* explorerMenuAction = m_contextMenu->addAction(QStringLiteral("使用 Explorer 菜单"));
    connect(explorerMenuAction, &QAction::triggered, this, &FilePage::onUseExplorerMenu);

    // 文件夹右键菜单
    m_folderMenu = new QMenu(this);
    auto* createDirAction = m_folderMenu->addAction(QStringLiteral("新建文件夹"));
    connect(createDirAction, &QAction::triggered, this, &FilePage::onCreateDirectory);
    m_folderMenu->addAction(renameAction);
    m_folderMenu->addAction(deleteAction);
    m_folderMenu->addAction(forceDeleteAction);
    m_folderMenu->addSeparator();
    m_folderMenu->addAction(explorerMenuAction);
    m_folderMenu->addAction(propsAction);
}

void FilePage::setupDrives()
{
    // 枚举所有逻辑驱动器
    m_driveCombo->clear();
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            QString drive = QChar('A' + i) + QStringLiteral(":\\");
            m_driveCombo->addItem(drive, drive);
        }
    }

    if (m_driveCombo->count() > 0) {
        browseTo(QStringLiteral("C:\\"));
    }
}

// 获取文件图标（带缓存）
// 缓存策略:
//   - 文件夹: 统一使用 QFileIconProvider::Folder，用 "__folder__" 作 key
//   - exe 文件: 每个 exe 有不同图标，按完整路径缓存
//   - 其他文件: 按扩展名缓存（同一扩展名图标相同）
QIcon FilePage::iconForFile(const QFileInfo& fi)
{
    static QFileIconProvider provider;

    // 文件夹：统一图标
    if (fi.isDir()) {
        const QString key = QStringLiteral("__folder__");
        auto it = m_iconCache.constFind(key);
        if (it != m_iconCache.constEnd()) return it.value();
        QIcon icon = provider.icon(QFileIconProvider::Folder);
        m_iconCache.insert(key, icon);
        return icon;
    }

    // exe 文件：按完整路径缓存（每个 exe 图标不同）
    QString ext = fi.suffix().toLower();
    if (ext == QStringLiteral("exe")) {
        QString key = fi.absoluteFilePath().toLower();
        auto it = m_iconCache.constFind(key);
        if (it != m_iconCache.constEnd()) return it.value();
        QIcon icon = provider.icon(fi);
        if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
        m_iconCache.insert(key, icon);
        return icon;
    }

    // 其他文件：按扩展名缓存
    if (ext.isEmpty()) ext = QStringLiteral("__noext__");
    auto it = m_iconCache.constFind(ext);
    if (it != m_iconCache.constEnd()) return it.value();
    QIcon icon = provider.icon(fi);
    if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
    m_iconCache.insert(ext, icon);
    return icon;
}

void FilePage::browseTo(const QString& dir)
{
    if (dir.isEmpty()) return;

    QDir d(dir);
    if (!d.exists()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("无法找到目录: ") + dir);
        return;
    }

    m_currentDir = QDir::toNativeSeparators(d.absolutePath());
    m_pathEdit->setText(m_currentDir);
    m_table->setRowCount(0);

    auto& ctx = AppContext::instance();
    bool fastScan = m_fastScan->isChecked();

    // DirectNTFS 模式: 绕过文件系统直接解析 NTFS (只读, 可发现被过滤驱动隐藏的文件)
    if (m_modeDirectNtfsRadio->isChecked() && ctx.driverLoaded()) {
        if (browseToDirectNtfs(m_currentDir)) {
            return;
        }
        // DirectNTFS 失败 (非NTFS卷/驱动错误), 回退到 IRP/普通枚举
        m_statusLabel->setText(QStringLiteral("DirectNTFS 失败, 回退到 IRP 模式"));
    }

    // 获取文件列表
    QFileInfoList entries;

    if (ctx.driverLoaded() && !fastScan) {
        // 使用驱动枚举（可看到隐藏文件）
        QVector<DataInfoAnsi> list;
        if (ctx.driver().irpQueryFileAndFolders(m_currentDir + QStringLiteral("\\"), list)) {
            for (const auto& info : list) {
                QFileInfo fi(m_currentDir + QStringLiteral("\\") + info.fileName);
                // 驱动返回的可能是文件或目录
                entries.append(fi);
            }
        }
        // 如果驱动枚举失败，回退到普通枚举
        if (entries.isEmpty()) {
            d.setFilter(QDir::AllEntries | QDir::Hidden | QDir::System);
            entries = d.entryInfoList();
        }
    } else {
        d.setFilter(QDir::AllEntries | QDir::Hidden | QDir::System);
        entries = d.entryInfoList();
    }

    // 排序
    if (m_sortFolderFirstRadio->isChecked()) {
        // 文件夹优先排序：先按 文件夹/文件 分组，组内按名称字母排序
        std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
            bool aDir = a.isDir();
            bool bDir = b.isDir();
            if (aDir != bDir) return aDir;  // 文件夹在前
            return a.fileName().toLower() < b.fileName().toLower();
        });
    } else {
        // 文件名混排排序：所有项按名称字母排序
        std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
            return a.fileName().toLower() < b.fileName().toLower();
        });
    }

    m_table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const QFileInfo& fi = entries[i];

        // 名称（带图标）
        auto* nameItem = new QTableWidgetItem(fi.fileName());
        nameItem->setIcon(iconForFile(fi));
        m_table->setItem(i, FC_Name, nameItem);

        // 类型
        if (fi.isDir()) {
            m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("文件夹")));
        } else if (fi.isFile()) {
            QString ext = fi.suffix().toLower();
            if (ext == "exe") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("应用程序")));
            else if (ext == "dll") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("应用程序扩展")));
            else if (ext == "sys") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("系统文件")));
            else if (ext == "txt" || ext == "log") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("文本文档")));
            else if (ext == "ini" || ext == "cfg") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("配置文件")));
            else m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("文件")));
        } else {
            m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("未知")));
        }

        // 大小
        if (fi.isFile()) {
            qint64 size = fi.size();
            if (size < 1024) {
                m_table->setItem(i, FC_Size, new QTableWidgetItem(QString::number(size) + QStringLiteral(" B")));
            } else if (size < 1024 * 1024) {
                m_table->setItem(i, FC_Size, new QTableWidgetItem(QString::number(size / 1024) + QStringLiteral(" KB")));
            } else if (size < 1024 * 1024 * 1024) {
                m_table->setItem(i, FC_Size, new QTableWidgetItem(QString::number(size / (1024 * 1024)) + QStringLiteral(" MB")));
            } else {
                m_table->setItem(i, FC_Size, new QTableWidgetItem(QString::number(size / (1024 * 1024 * 1024), 'f', 2) + QStringLiteral(" GB")));
            }
        } else {
            m_table->setItem(i, FC_Size, new QTableWidgetItem(QStringLiteral("")));
        }

        // 时间
        if (!fastScan) {
            m_table->setItem(i, FC_Created, new QTableWidgetItem(fi.birthTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
            m_table->setItem(i, FC_Modified, new QTableWidgetItem(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
        }
    }

    int fileCount = entries.size();
    m_statusLabel->setText(QStringLiteral("项目数: %1").arg(fileCount));
}

bool FilePage::browseToDirectNtfs(const QString& dir)
{
    // DirectNTFS 模式: 通过 IOCTL_NTFS_INIT + IOCTL_NTFS_ENUMDIR 列目录
    // 返回 true 表示成功填充表格, false 表示失败 (调用方回退到 IRP)
    auto& ctx = AppContext::instance();

    // 提取盘符并初始化对应卷 (盘符变化时重新初始化)
    QChar driveLetter = m_currentDir.isEmpty() ? QChar() : m_currentDir[0].toUpper();
    if (driveLetter.isNull()) return false;

    if (m_ntfsInitializedDrive != driveLetter) {
        if (!ctx.driver().ntfsInitVolume(driveLetter.toLatin1())) {
            return false;
        }
        m_ntfsInitializedDrive = driveLetter;
    }

    // 枚举目录
    QVector<NtfsDataInfo> list;
    if (!ctx.driver().ntfsEnumDirectory(m_currentDir, list)) {
        return false;
    }

    // 转为 QFileInfo 列表并排序
    QFileInfoList entries;
    entries.reserve(list.size());
    for (const auto& info : list) {
        QString name = QString::fromWCharArray(info.name);
        if (name.isEmpty()) continue;
        // 跳过 . 和 ..
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) continue;
        QFileInfo fi(m_currentDir + QStringLiteral("\\") + name);
        entries.append(fi);
    }

    // 排序 (复用主排序逻辑)
    if (m_sortFolderFirstRadio->isChecked()) {
        std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
            // DirectNTFS 的 type 字段: 0=文件 1=目录 2=系统元数据文件 3=系统元数据目录
            // 这里用 QFileInfo.isDir() 判断, 但 DirectNTFS 模式下文件可能无法通过 QFileInfo 获取属性
            // 所以根据 NtfsDataInfo.type 判断更准确
            bool aDir = a.isDir();
            bool bDir = b.isDir();
            if (aDir != bDir) return aDir;
            return a.fileName().toLower() < b.fileName().toLower();
        });
    } else {
        std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
            return a.fileName().toLower() < b.fileName().toLower();
        });
    }

    m_table->setRowCount(entries.size());
    bool fastScan = m_fastScan->isChecked();

    for (int i = 0; i < entries.size(); ++i) {
        const QFileInfo& fi = entries[i];
        // 找到对应的 NtfsDataInfo 以获取 type
        int type = -1;
        for (const auto& info : list) {
            if (QString::fromWCharArray(info.name) == fi.fileName()) {
                type = (int)info.type;
                break;
            }
        }

        auto* nameItem = new QTableWidgetItem(fi.fileName());
        nameItem->setIcon(iconForFile(fi));
        m_table->setItem(i, FC_Name, nameItem);

        // 类型 (优先用 DirectNTFS 返回的 type)
        bool isDir = (type == 1 || type == 3) || (type == -1 && fi.isDir());
        if (isDir) {
            m_table->setItem(i, FC_Type, new QTableWidgetItem(
                type == 3 ? QStringLiteral("系统目录") : QStringLiteral("文件夹")));
        } else {
            QString ext = fi.suffix().toLower();
            if (ext == "exe") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("应用程序")));
            else if (ext == "dll") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("应用程序扩展")));
            else if (ext == "sys") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("系统文件")));
            else if (type == 2) m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("系统文件")));
            else if (ext == "txt" || ext == "log") m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("文本文档")));
            else m_table->setItem(i, FC_Type, new QTableWidgetItem(QStringLiteral("文件")));
        }

        // 大小 (DirectNTFS 模式下不单独查询文件大小, 留空)
        m_table->setItem(i, FC_Size, new QTableWidgetItem(QStringLiteral("")));

        // 时间 (DirectNTFS 不返回时间信息, 留空)
        if (!fastScan) {
            m_table->setItem(i, FC_Created, new QTableWidgetItem(fi.birthTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
            m_table->setItem(i, FC_Modified, new QTableWidgetItem(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
        }
    }

    m_statusLabel->setText(QStringLiteral("[DirectNTFS] 项目数: %1").arg(entries.size()));
    return true;
}

QString FilePage::currentPath() const
{
    return m_currentDir;
}

QString FilePage::selectedFilePath() const
{
    int row = m_table->currentRow();
    if (row < 0) return QString();

    auto* nameItem = m_table->item(row, FC_Name);
    if (!nameItem) return QString();

    QString name = nameItem->text();
    if (name == "." || name == "..") return QString();

    QString path = m_currentDir;
    if (!path.endsWith('\\')) path += '\\';
    path += name;
    return path;
}

bool FilePage::isDirectoryRow(int row) const
{
    if (row < 0) return false;
    auto* typeItem = m_table->item(row, FC_Type);
    return typeItem && typeItem->text() == QStringLiteral("文件夹");
}

void FilePage::onDriveChanged(int index)
{
    if (index < 0) return;
    QString drive = m_driveCombo->itemData(index).toString();
    browseTo(drive);
}

void FilePage::onGotoClicked()
{
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) return;
    browseTo(path);
}

void FilePage::onUpClicked()
{
    QDir d(m_currentDir);
    if (d.cdUp()) {
        browseTo(d.absolutePath());
    }
}

void FilePage::onRefreshClicked()
{
    browseTo(m_currentDir);
}

void FilePage::onFastScanToggled(bool checked)
{
    Q_UNUSED(checked);
    browseTo(m_currentDir);
}

void FilePage::onCellDoubleClicked(int row, int col)
{
    Q_UNUSED(col);
    if (row < 0) return;

    auto* nameItem = m_table->item(row, FC_Name);
    if (!nameItem) return;
    QString name = nameItem->text();

    if (name == "..") {
        onUpClicked();
        return;
    }
    if (name == ".") {
        return;
    }

    if (isDirectoryRow(row)) {
        // 进入子目录
        browseTo(m_currentDir + QStringLiteral("\\") + name);
    } else {
        // 执行文件
        QString path = selectedFilePath();
        if (!path.isEmpty()) {
            ShellExecuteW(nullptr, L"open", path.toStdWString().c_str(), nullptr, nullptr, SW_SHOW);
        }
    }
}

void FilePage::onContextMenu(const QPoint& pos)
{
    QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid()) {
        // 空白处右键 - 显示文件夹菜单
        m_folderMenu->exec(m_table->viewport()->mapToGlobal(pos));
        return;
    }

    if (isDirectoryRow(index.row())) {
        m_folderMenu->exec(m_table->viewport()->mapToGlobal(pos));
    } else {
        m_contextMenu->exec(m_table->viewport()->mapToGlobal(pos));
    }
}

void FilePage::onRunFile()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;
    ShellExecuteW(nullptr, L"open", path.toStdWString().c_str(), nullptr, nullptr, SW_SHOW);
}

void FilePage::onRunHidden()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;
    ShellExecuteW(nullptr, L"open", path.toStdWString().c_str(), nullptr, nullptr, SW_HIDE);
}

void FilePage::onRename()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    bool ok = false;
    QString newName = QInputDialog::getText(this, QStringLiteral("重命名"),
        QStringLiteral("请输入新名字:"), QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.isEmpty()) return;

    QString newPath = fi.absolutePath() + QStringLiteral("\\") + newName;

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        // 尝试驱动重命名
        if (!ctx.driver().irpRenameFile(path, newName)) {
            // 失败则用普通方式
            if (!QFile::rename(path, newPath)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("重命名失败!"));
            }
        }
    } else {
        if (!QFile::rename(path, newPath)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("重命名失败!"));
        }
    }
    browseTo(m_currentDir);
}

void FilePage::onDeleteNormal()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("确定删除: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QFileInfo fi(path);
    bool ok = false;
    if (fi.isDir()) {
        ok = QDir(path).removeRecursively();
    } else {
        ok = QFile::remove(path);
    }

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("删除失败!") + QStringLiteral("\n错误码: ") + QString::number(GetLastError()));
    }
    browseTo(m_currentDir);
}

void FilePage::onForceDelete()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载，无法使用强制删除"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("强制删除不可恢复!确定删除: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QFileInfo fi(path);
    if (fi.isDir()) {
        // 文件夹：弹出进度对话框，工作线程递归删除
        auto* dlg = new DeleteDirProgressDialog(path, 0, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlag(Qt::Window);
        // 关闭时刷新文件列表
        connect(dlg, &DeleteDirProgressDialog::finished, this, [this](bool) {
            browseTo(m_currentDir);
        });
        dlg->show();
    } else {
        bool ok = ctx.driver().forceDeleteFile(path);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("强制删除失败!"));
        }
        browseTo(m_currentDir);
    }
}

void FilePage::onPosixForceDelete()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("POSIX强制删除不可恢复!确定删除: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QFileInfo fi(path);
    if (fi.isDir()) {
        // POSIX 模式同样支持文件夹递归删除
        auto* dlg = new DeleteDirProgressDialog(path, 1, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlag(Qt::Window);
        connect(dlg, &DeleteDirProgressDialog::finished, this, [this](bool) {
            browseTo(m_currentDir);
        });
        dlg->show();
    } else {
        bool ok = ctx.driver().posixForceDeleteFile(path);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("POSIX删除失败!"));
        }
        browseTo(m_currentDir);
    }
}

void FilePage::onZwDeleteFile()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("ZwDeleteFile删除不可恢复!确定删除: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QFileInfo fi(path);
    if (fi.isDir()) {
        // ZwDeleteFile 模式同样支持文件夹递归删除
        auto* dlg = new DeleteDirProgressDialog(path, 2, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlag(Qt::Window);
        connect(dlg, &DeleteDirProgressDialog::finished, this, [this](bool) {
            browseTo(m_currentDir);
        });
        dlg->show();
    } else {
        // ZwDeleteFile 需要 NT 路径: \??\C:\xxx
        QString ntPath = QStringLiteral("\\??\\") + path;
        bool ok = ctx.driver().zwDeleteFile(ntPath);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("ZwDeleteFile失败!"));
        }
        browseTo(m_currentDir);
    }
}

void FilePage::onCopyFile()
{
    QString srcPath = selectedFilePath();
    if (srcPath.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择目标目录"), m_currentDir);
    if (destDir.isEmpty()) return;

    QFileInfo fi(srcPath);
    QString destPath = destDir + QStringLiteral("\\") + fi.fileName();

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        if (!ctx.driver().irpCopyFile(srcPath, destPath)) {
            // 回退到普通复制
            if (!QFile::copy(srcPath, destPath)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("复制失败!"));
            }
        }
    } else {
        if (!QFile::copy(srcPath, destPath)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("复制失败!"));
        }
    }
}

void FilePage::onMoveFile()
{
    QString srcPath = selectedFilePath();
    if (srcPath.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择目标目录"), m_currentDir);
    if (destDir.isEmpty()) return;

    QFileInfo fi(srcPath);
    QString destPath = destDir + QStringLiteral("\\") + fi.fileName();

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        if (!ctx.driver().irpMoveFile(srcPath, destPath)) {
            // 回退
            if (!QFile::rename(srcPath, destPath)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("移动失败!"));
            }
        }
    } else {
        if (!QFile::rename(srcPath, destPath)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("移动失败!"));
        }
    }
    browseTo(m_currentDir);
}

void FilePage::onProtectFile()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    QString ntPath = QStringLiteral("\\??\\") + path;
    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("保护文件: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().occupyFile(ntPath)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("保护文件失败!"));
    } else {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("文件已保护"));
    }
}

void FilePage::onForceOccupyFile()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("驱动未加载"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("强制占用文件: %1 吗?").arg(path),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    if (!ctx.driver().forceOccupyFile(path)) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("强制占用失败!"));
    } else {
        QMessageBox::information(this, QStringLiteral("EFCHSword"), QStringLiteral("文件已强制占用"));
    }
}

void FilePage::onReadFile()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    FileReaderDialog dlg(path, this, m_modeDirectNtfsRadio->isChecked());
    dlg.exec();
}

void FilePage::onShowProperties()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) return;

    FilePropertiesDialog dlg(path, this);
    dlg.exec();
}

void FilePage::onUseExplorerMenu()
{
    QString path = selectedFilePath();
    if (path.isEmpty()) {
        path = m_currentDir;
    }

    // 使用 SHShowContextMenu 显示 Explorer 右键菜单
    // 简化实现：使用 ShellExecute 打开
    QFileInfo fi(path);
    QString dir = fi.isDir() ? path : fi.absolutePath();
    QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << dir);
}

void FilePage::onCreateDirectory()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("新建文件夹"),
        QStringLiteral("文件夹名称:"), QLineEdit::Normal, QStringLiteral("新建文件夹"), &ok);
    if (!ok || name.isEmpty()) return;

    QString path = m_currentDir + QStringLiteral("\\") + name;

    auto& ctx = AppContext::instance();
    if (ctx.driverLoaded()) {
        if (!ctx.driver().irpCreateDirectory(path)) {
            if (!QDir().mkdir(path)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("创建文件夹失败!"));
            }
        }
    } else {
        if (!QDir().mkdir(path)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("创建文件夹失败!"));
        }
    }
    browseTo(m_currentDir);
}

void FilePage::onSortModeChanged()
{
    browseTo(m_currentDir);
}
