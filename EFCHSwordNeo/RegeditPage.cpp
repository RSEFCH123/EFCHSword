#include "RegeditPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QSettings>
#include <QHeaderView>
#include <functional>

// 5 个根键，直接作为树形框的顶级项
static const struct RootKeyDef {
    const wchar_t* name;
    const QString  path;   // QSettings 路径
} g_rootKeys[] = {
    { L"HKEY_CLASSES_ROOT",   QStringLiteral("HKEY_CLASSES_ROOT") },
    { L"HKEY_CURRENT_USER",   QStringLiteral("HKEY_CURRENT_USER") },
    { L"HKEY_LOCAL_MACHINE",  QStringLiteral("HKEY_LOCAL_MACHINE") },
    { L"HKEY_USERS",          QStringLiteral("HKEY_USERS") },
    { L"HKEY_CURRENT_CONFIG", QStringLiteral("HKEY_CURRENT_CONFIG") }
};

RegeditPage::RegeditPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupTreeContextMenu();
    setupTableContextMenu();
    onRefresh();
}

RegeditPage::~RegeditPage() = default;

void RegeditPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* toolLayout = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &RegeditPage::onRefresh);
    toolLayout->addStretch();
    toolLayout->addWidget(refreshBtn);
    mainLayout->addLayout(toolLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧键树 - 直接显示所有 5 个根键作为顶级项
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel(QStringLiteral("注册表键"));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &RegeditPage::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemClicked, this, &RegeditPage::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &RegeditPage::onTreeContextMenu);

    splitter->addWidget(m_tree);

    // 右侧值表
    m_valueTable = new QTableWidget(this);
    m_valueTable->setColumnCount(3);
    m_valueTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("数据")});
    m_valueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_valueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_valueTable->setAlternatingRowColors(true);
    m_valueTable->verticalHeader()->setVisible(false);
    m_valueTable->horizontalHeader()->setStretchLastSection(true);
    m_valueTable->horizontalHeader()->setHighlightSections(false);
    m_valueTable->setColumnWidth(0, 200);
    m_valueTable->setColumnWidth(1, 100);
    m_valueTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_valueTable, &QTableWidget::customContextMenuRequested, this, &RegeditPage::onTableContextMenu);

    splitter->addWidget(m_valueTable);
    splitter->setSizes({400, 600});

    mainLayout->addWidget(splitter);
}

void RegeditPage::setupTreeContextMenu()
{
    m_treeMenu = new QMenu(this);

    auto* createKeyAction = m_treeMenu->addAction(QStringLiteral("新建项"));
    connect(createKeyAction, &QAction::triggered, this, &RegeditPage::onCreateKey);

    auto* deleteKeyAction = m_treeMenu->addAction(QStringLiteral("删除项"));
    connect(deleteKeyAction, &QAction::triggered, this, &RegeditPage::onDeleteKey);

    auto* renameKeyAction = m_treeMenu->addAction(QStringLiteral("重命名项"));
    connect(renameKeyAction, &QAction::triggered, this, &RegeditPage::onRenameKey);
}

void RegeditPage::setupTableContextMenu()
{
    m_tableMenu = new QMenu(this);

    auto* modifyAction = m_tableMenu->addAction(QStringLiteral("修改数据"));
    connect(modifyAction, &QAction::triggered, this, &RegeditPage::onModifyValue);

    auto* deleteAction = m_tableMenu->addAction(QStringLiteral("删除值"));
    connect(deleteAction, &QAction::triggered, this, &RegeditPage::onDeleteValue);

    auto* renameAction = m_tableMenu->addAction(QStringLiteral("重命名值"));
    connect(renameAction, &QAction::triggered, this, &RegeditPage::onRenameValue);

    m_tableMenu->addSeparator();

    auto* createAction = m_tableMenu->addAction(QStringLiteral("新建值"));
    connect(createAction, &QAction::triggered, this, &RegeditPage::onCreateValue);
}

void RegeditPage::onRefresh()
{
    m_tree->clear();

    // 直接添加 5 个根键作为顶级项
    for (const auto& rk : g_rootKeys) {
        auto* rootItem = new QTreeWidgetItem(m_tree);
        rootItem->setText(0, QString::fromWCharArray(rk.name));
        rootItem->setData(0, Qt::UserRole, rk.path);
        rootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        m_tree->addTopLevelItem(rootItem);
    }
}

void RegeditPage::onItemExpanded(QTreeWidgetItem* item)
{
    if (item->childCount() > 0) return; // 已加载

    QString path = item->data(0, Qt::UserRole).toString();
    loadSubKeys(item, path);
}

void RegeditPage::onItemClicked(QTreeWidgetItem* item, int col)
{
    Q_UNUSED(col);
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    loadValues(path);
}

void RegeditPage::loadSubKeys(QTreeWidgetItem* parent, const QString& fullPath)
{
    QSettings settings(fullPath, QSettings::NativeFormat);
    QStringList groups = settings.childGroups();

    for (const auto& group : groups) {
        auto* child = new QTreeWidgetItem(parent);
        child->setText(0, group);
        child->setData(0, Qt::UserRole, fullPath + QStringLiteral("\\") + group);
        child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
}

void RegeditPage::loadValues(const QString& fullPath)
{
    m_valueTable->setRowCount(0);

    QSettings settings(fullPath, QSettings::NativeFormat);
    QStringList keys = settings.childKeys();

    for (const auto& key : keys) {
        QVariant value = settings.value(key);
        QString typeStr;
        QString dataStr;

        switch (value.type()) {
            case QVariant::Int:
            case QVariant::LongLong:
            case QVariant::UInt:
            case QVariant::ULongLong:
                typeStr = QStringLiteral("REG_DWORD");
                dataStr = QStringLiteral("0x%1").arg(value.toLongLong(), 8, 16, QChar('0'));
                break;
            case QVariant::StringList:
                typeStr = QStringLiteral("REG_MULTI_SZ");
                dataStr = value.toStringList().join(QStringLiteral("; "));
                break;
            case QVariant::ByteArray:
                typeStr = QStringLiteral("REG_BINARY");
                dataStr = value.toByteArray().toHex();
                break;
            default:
                typeStr = QStringLiteral("REG_SZ");
                dataStr = value.toString();
                break;
        }

        int row = m_valueTable->rowCount();
        m_valueTable->insertRow(row);
        m_valueTable->setItem(row, 0, new QTableWidgetItem(key.isEmpty() ? QStringLiteral("(默认)") : key));
        m_valueTable->setItem(row, 1, new QTableWidgetItem(typeStr));
        m_valueTable->setItem(row, 2, new QTableWidgetItem(dataStr));
    }
}

QString RegeditPage::currentPath() const
{
    auto* item = m_tree->currentItem();
    if (!item) return QString();
    return item->data(0, Qt::UserRole).toString();
}

void RegeditPage::onTreeContextMenu(const QPoint& pos)
{
    m_treeMenu->exec(m_tree->viewport()->mapToGlobal(pos));
}

void RegeditPage::onTableContextMenu(const QPoint& pos)
{
    m_tableMenu->exec(m_valueTable->viewport()->mapToGlobal(pos));
}

void RegeditPage::onCreateKey()
{
    auto* parent = m_tree->currentItem();
    if (!parent) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("新建项"),
        QStringLiteral("项名称:"), QLineEdit::Normal, QStringLiteral("New Key #1"), &ok);
    if (!ok || name.isEmpty()) return;

    QString parentPath = parent->data(0, Qt::UserRole).toString();
    QString newPath = parentPath + QStringLiteral("\\") + name;

    QSettings settings(newPath, QSettings::NativeFormat);
    settings.setValue(QStringLiteral("."), QString()); // 创建空键

    // 刷新
    parent->takeChildren();
    loadSubKeys(parent, parentPath);
}

void RegeditPage::onDeleteKey()
{
    auto* item = m_tree->currentItem();
    if (!item) return;

    // 不允许直接删除根键
    bool isRoot = false;
    for (const auto& rk : g_rootKeys) {
        if (item->text(0) == QString::fromWCharArray(rk.name)) {
            isRoot = true;
            break;
        }
    }
    if (isRoot) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("不允许直接删除根键"));
        return;
    }

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定删除项: %1 及其所有子项吗?").arg(item->text(0)),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QString path = item->data(0, Qt::UserRole).toString();
    QSettings settings(path, QSettings::NativeFormat);
    settings.remove("");

    // 从树中移除
    if (auto* parent = item->parent()) {
        parent->removeChild(item);
    } else {
        m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item));
    }
}

void RegeditPage::onModifyValue()
{
    int row = m_valueTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_valueTable->item(row, 0);
    auto* typeItem = m_valueTable->item(row, 1);
    auto* dataItem = m_valueTable->item(row, 2);
    if (!nameItem || !dataItem) return;

    QString typeStr = typeItem ? typeItem->text() : QStringLiteral("REG_SZ");
    QString origName = nameItem->text();
    QString valueName = (origName == QStringLiteral("(默认)")) ? QString() : origName;

    QString path = currentPath();
    if (path.isEmpty()) return;

    if (typeStr == QStringLiteral("REG_DWORD")) {
        // DWORD 类型：以十六进制输入
        bool ok = false;
        QString oldVal = dataItem->text();
        oldVal.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
        QString newStr = QInputDialog::getText(this, QStringLiteral("修改 DWORD 值"),
            QStringLiteral("数据 (十六进制):"), QLineEdit::Normal, oldVal, &ok);
        if (!ok) return;
        newStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
        bool parseOk = false;
        quint32 val = newStr.toUInt(&parseOk, 16);
        if (!parseOk) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制数值"));
            return;
        }
        QSettings settings(path, QSettings::NativeFormat);
        settings.setValue(valueName, (uint)val);
    } else {
        // 字符串类型
        bool ok = false;
        QString newValue = QInputDialog::getText(this, QStringLiteral("修改值"),
            QStringLiteral("数据:"), QLineEdit::Normal, dataItem->text(), &ok);
        if (!ok) return;
        QSettings settings(path, QSettings::NativeFormat);
        settings.setValue(valueName, newValue);
    }

    loadValues(path);
}

void RegeditPage::onDeleteValue()
{
    int row = m_valueTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_valueTable->item(row, 0);
    if (!nameItem) return;

    QString valueName = nameItem->text();
    QString realName = (valueName == QStringLiteral("(默认)")) ? QString() : valueName;

    if (QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("确定删除值: %1 吗?").arg(valueName),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

    QString path = currentPath();
    if (path.isEmpty()) return;

    QSettings settings(path, QSettings::NativeFormat);
    settings.remove(realName);

    loadValues(path);
}

void RegeditPage::onRenameValue()
{
    int row = m_valueTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_valueTable->item(row, 0);
    auto* typeItem = m_valueTable->item(row, 1);
    auto* dataItem = m_valueTable->item(row, 2);
    if (!nameItem) return;

    QString oldName = nameItem->text();
    if (oldName == QStringLiteral("(默认)")) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("默认值不能重命名"));
        return;
    }

    bool ok = false;
    QString newName = QInputDialog::getText(this, QStringLiteral("重命名值"),
        QStringLiteral("新名称:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;

    QString path = currentPath();
    if (path.isEmpty()) return;

    // 读取旧值并写入新名称，然后删除旧值
    QSettings settings(path, QSettings::NativeFormat);
    QVariant value = settings.value(oldName);
    settings.setValue(newName, value);
    settings.remove(oldName);

    loadValues(path);
}

void RegeditPage::onCreateValue()
{
    QString path = currentPath();
    if (path.isEmpty()) return;

    // 选择类型
    QStringList types = {QStringLiteral("REG_SZ"), QStringLiteral("REG_DWORD"),
                         QStringLiteral("REG_MULTI_SZ"), QStringLiteral("REG_BINARY")};
    bool ok = false;
    QString typeStr = QInputDialog::getItem(this, QStringLiteral("新建值"),
        QStringLiteral("选择类型:"), types, 0, false, &ok);
    if (!ok) return;

    QString name = QInputDialog::getText(this, QStringLiteral("新建值"),
        QStringLiteral("值名称:"), QLineEdit::Normal, QStringLiteral("新值 #1"), &ok);
    if (!ok || name.isEmpty()) return;

    QSettings settings(path, QSettings::NativeFormat);

    if (typeStr == QStringLiteral("REG_DWORD")) {
        settings.setValue(name, (uint)0);
    } else if (typeStr == QStringLiteral("REG_MULTI_SZ")) {
        settings.setValue(name, QStringList{QStringLiteral("")});
    } else if (typeStr == QStringLiteral("REG_BINARY")) {
        settings.setValue(name, QByteArray());
    } else {
        settings.setValue(name, QString());
    }

    loadValues(path);
}

void RegeditPage::onRenameKey()
{
    auto* item = m_tree->currentItem();
    if (!item) return;

    // 不允许重命名根键
    for (const auto& rk : g_rootKeys) {
        if (item->text(0) == QString::fromWCharArray(rk.name)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("不允许重命名根键"));
            return;
        }
    }

    auto* parentItem = item->parent();
    if (!parentItem) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("无法重命名此项"));
        return;
    }

    bool ok = false;
    QString newName = QInputDialog::getText(this, QStringLiteral("重命名项"),
        QStringLiteral("新名称:"), QLineEdit::Normal, item->text(0), &ok);
    if (!ok || newName.isEmpty() || newName == item->text(0)) return;

    QString parentPath = parentItem->data(0, Qt::UserRole).toString();
    QString oldPath = item->data(0, Qt::UserRole).toString();
    QString newPath = parentPath + QStringLiteral("\\") + newName;

    // 读取旧键所有值和子键，写入新键，然后删除旧键
    QSettings oldSettings(oldPath, QSettings::NativeFormat);
    QSettings newSettings(newPath, QSettings::NativeFormat);

    // 复制值
    for (const auto& key : oldSettings.childKeys()) {
        newSettings.setValue(key, oldSettings.value(key));
    }

    // 递归复制子键
    std::function<void(QSettings&, QSettings&)> copyKeys;
    copyKeys = [&copyKeys](QSettings& src, QSettings& dst) {
        for (const auto& g : src.childGroups()) {
            src.beginGroup(g);
            dst.beginGroup(g);
            for (const auto& k : src.childKeys()) {
                dst.setValue(k, src.value(k));
            }
            copyKeys(src, dst);
            dst.endGroup();
            src.endGroup();
        }
    };
    copyKeys(oldSettings, newSettings);

    // 删除旧键
    oldSettings.remove("");

    // 刷新父项
    parentItem->takeChildren();
    loadSubKeys(parentItem, parentPath);
}
