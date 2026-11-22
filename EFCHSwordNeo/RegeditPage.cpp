#include "RegeditPage.h"
#include "AppContext.h"

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
#include <QSet>
#include <QHeaderView>
#include <functional>
#include <memory>

// ArkRegEnumKeys(约1MB)/ArkRegEnumValues(约800KB) 远超 1MB 默认线程栈，
// 必须堆分配——此前栈上声明导致注册表页点击任意项即栈溢出崩溃

// 5 个根键，直接作为树形框的顶级项
static const struct RootKeyDef {
    const wchar_t* name;
    const QString  path;
} g_rootKeys[] = {
    { L"HKEY_CLASSES_ROOT",   QStringLiteral("HKEY_CLASSES_ROOT") },
    { L"HKEY_CURRENT_USER",   QStringLiteral("HKEY_CURRENT_USER") },
    { L"HKEY_LOCAL_MACHINE",  QStringLiteral("HKEY_LOCAL_MACHINE") },
    { L"HKEY_USERS",          QStringLiteral("HKEY_USERS") },
    { L"HKEY_CURRENT_CONFIG", QStringLiteral("HKEY_CURRENT_CONFIG") }
};

// ---- REG_* 类型常量（数值与 ntifs.h / winreg.h 一致） ----
static const quint32 kRegNone      = 0;
static const quint32 kRegSz        = 1;
static const quint32 kRegExpandSz  = 2;
static const quint32 kRegBinary    = 3;
static const quint32 kRegDword     = 4;
static const quint32 kRegMultiSz   = 7;
static const quint32 kRegQword     = 11;

static QString regTypeName(quint32 type)
{
    switch (type) {
    case kRegNone:     return QStringLiteral("REG_NONE");
    case kRegSz:       return QStringLiteral("REG_SZ");
    case kRegExpandSz: return QStringLiteral("REG_EXPAND_SZ");
    case kRegBinary:   return QStringLiteral("REG_BINARY");
    case kRegDword:    return QStringLiteral("REG_DWORD");
    case 5:            return QStringLiteral("REG_DWORD_BIG_ENDIAN");
    case 6:            return QStringLiteral("REG_LINK");
    case kRegMultiSz:  return QStringLiteral("REG_MULTI_SZ");
    case 8:            return QStringLiteral("REG_RESOURCE_LIST");
    case 9:            return QStringLiteral("REG_FULL_RESOURCE_DESCRIPTOR");
    case 10:           return QStringLiteral("REG_RESOURCE_REQUIREMENTS_LIST");
    case kRegQword:    return QStringLiteral("REG_QWORD");
    default:           return QStringLiteral("REG_(%1)").arg(type);
    }
}

static QString formatRegData(const ArkRegValue& v)
{
    if (v.dataLength == 0) {
        return QStringLiteral("(空)");
    }

    switch (v.type) {
    case kRegSz:
    case kRegExpandSz: {
        int chars = v.dataLength / 2;
        if (chars > 512) chars = 512;
        QString s = QString::fromWCharArray((const wchar_t*)v.data, chars);
        int nul = s.indexOf(QChar(0));
        if (nul >= 0) s.truncate(nul);
        return s;
    }
    case kRegDword: {
        if (v.dataLength >= 4) {
            quint32 val = 0;
            memcpy(&val, v.data, 4);
            return QStringLiteral("0x%1 (%2)").arg(val, 8, 16, QChar('0')).arg(val);
        }
        break;
    }
    case kRegQword: {
        if (v.dataLength >= 8) {
            quint64 val = 0;
            memcpy(&val, v.data, 8);
            return QStringLiteral("0x%1 (%2)").arg(val, 16, 16, QChar('0')).arg((qulonglong)val);
        }
        break;
    }
    case kRegMultiSz: {
        QStringList parts;
        int chars = v.dataLength / 2;
        QString cur;
        for (int i = 0; i < chars; ++i) {
            wchar_t c = ((const wchar_t*)v.data)[i];
            if (c == 0) {
                if (cur.isEmpty()) break;   // 结尾双 NUL
                parts << cur;
                cur.clear();
            } else {
                cur.append(QChar((ushort)c));
            }
        }
        if (!cur.isEmpty()) parts << cur;
        return parts.join(QStringLiteral("; "));
    }
    default:
        break;
    }

    // 二进制显示（截断到前 64 字节）
    QString hex;
    int n = (int)v.dataLength;
    if (n > 64) n = 64;
    for (int i = 0; i < n; ++i) {
        hex += QString::asprintf("%02X ", v.data[i]);
    }
    QString r = hex.trimmed();
    if (v.dataLength > 64) r += QStringLiteral(" ...");
    if (v.truncated) r += QStringLiteral(" (数据截断)");
    return r;
}

// 字符串 -> REG_SZ / REG_EXPAND_SZ / REG_MULTI_SZ 的字节序列
static QByteArray encodeRegString(const QString& s, bool multiSz)
{
    QByteArray bytes;
    if (multiSz) {
        const QStringList parts = s.split(QStringLiteral("; "));
        for (const QString& p : parts) {
            const std::wstring ws = p.toStdWString();
            bytes += QByteArray((const char*)ws.c_str(), (int)(ws.size() + 1) * (int)sizeof(wchar_t));
        }
        // 结尾再补一个空串（双 NUL）
        bytes += QByteArray((const char*)L"\0", 0);
        const wchar_t nul = 0;
        bytes += QByteArray((const char*)&nul, (int)sizeof(wchar_t));
    } else {
        const std::wstring ws = s.toStdWString();
        bytes = QByteArray((const char*)ws.c_str(), (int)(ws.size() + 1) * (int)sizeof(wchar_t));
    }
    return bytes;
}

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

    // 展开后确实没有子键 → 收起箭头（叶子节点）
    if (item->childCount() == 0) {
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }
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
    QSet<QString> kernelNames;

    // 首选：内核枚举（System 令牌上下文，能看到受 ACL 保护/被隐藏的子键）
    if (AppContext::instance().driver().isLoaded()) {
        auto out = std::make_unique<ArkRegEnumKeys>();
        if (AppContext::instance().driver().regEnumSubKeys(fullPath, *out) &&
            out->count > 0) {
            for (quint32 i = 0; i < out->count && i < 2048; ++i) {
                const ArkRegSubKey& sk = out->subKeys[i];
                QString name = QString::fromWCharArray(sk.name);
                if (name.trimmed().isEmpty()) {
                    continue;   // 跳过空名条目（驱动侧个别键会多报一条空项）
                }
                kernelNames.insert(name);
                auto* child = new QTreeWidgetItem(parent);
                child->setText(0, name);
                child->setData(0, Qt::UserRole, fullPath + QStringLiteral("\\") + name);
                // 箭头策略不依赖内核回传的子键数量（个别键会漏报，
                // 表现为最后一个子键永远没有箭头）：一律显示，
                // 展开后无子键再收起
                child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            }
            // 兜底：内核枚举可能因个别条目失败漏报子键（曾表现为最后一个
            // 子键被"吞掉"）。与用户层枚举比对，缺谁补谁。
            if (kernelNames.size() < 2048) {
                const QStringList userGroups = childGroupsSafe(fullPath);
                for (const QString& g : userGroups) {
                    if (!kernelNames.contains(g)) {
                        auto* child = new QTreeWidgetItem(parent);
                        child->setText(0, g);
                        child->setData(0, Qt::UserRole, fullPath + QStringLiteral("\\") + g);
                        child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                    }
                }
            }
            return;
        }
        // 内核枚举失败或为空（键不存在等）→ 回退用户层再试
    }

    // 回退：QSettings 用户层
    QSettings settings(fullPath, QSettings::NativeFormat);
    QStringList groups = settings.childGroups();

    for (const auto& group : groups) {
        auto* child = new QTreeWidgetItem(parent);
        child->setText(0, group);
        child->setData(0, Qt::UserRole, fullPath + QStringLiteral("\\") + group);
        child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
}

// 用户层子键列表（受限/被保护键可能为空或部分缺失，仅作内核枚举的对账参考）
QStringList RegeditPage::childGroupsSafe(const QString& fullPath)
{
    QSettings settings(fullPath, QSettings::NativeFormat);
    return settings.childGroups();
}

void RegeditPage::loadValues(const QString& fullPath)
{
    m_valueTable->setRowCount(0);
    m_currentValues.clear();

    // 首选：内核枚举（含类型和原始数据，修改时直接复用类型）
    if (AppContext::instance().driver().isLoaded()) {
        auto out = std::make_unique<ArkRegEnumValues>();
        if (AppContext::instance().driver().regEnumValues(fullPath, *out)) {
            for (quint32 i = 0; i < out->count && i < 512; ++i) {
                const ArkRegValue& v = out->values[i];
                m_currentValues.append(v);

                QString name = QString::fromWCharArray(v.name);
                int row = m_valueTable->rowCount();
                m_valueTable->insertRow(row);
                m_valueTable->setItem(row, 0, new QTableWidgetItem(
                    name.isEmpty() ? QStringLiteral("(默认)") : name));
                m_valueTable->setItem(row, 1, new QTableWidgetItem(regTypeName(v.type)));
                m_valueTable->setItem(row, 2, new QTableWidgetItem(formatRegData(v)));
            }
            return;
        }
    }

    // 回退：QSettings 用户层
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

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        if (!drv.regCreateKey(newPath)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核创建项失败: %1").arg(newPath));
            return;
        }
    } else {
        QSettings settings(newPath, QSettings::NativeFormat);
        settings.setValue(QStringLiteral("."), QString()); // 创建空键
    }

    // 刷新
    parent->takeChildren();
    parent->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
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

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        if (!drv.regDeleteKey(path)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核删除项失败（可能被占用或权限不足）: %1").arg(path));
            return;
        }
    } else {
        QSettings settings(path, QSettings::NativeFormat);
        settings.remove("");
    }

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
    auto* dataItem = m_valueTable->item(row, 2);
    if (!nameItem || !dataItem) return;

    QString origName = nameItem->text();
    QString valueName = (origName == QStringLiteral("(默认)")) ? QString() : origName;

    QString path = currentPath();
    if (path.isEmpty()) return;

    // 首选：内核修改（保留原始类型）
    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded() && row < m_currentValues.size()) {
        const ArkRegValue& v = m_currentValues[row];
        bool ok = false;
        QByteArray data;

        switch (v.type) {
        case kRegDword: {
            quint32 cur = 0;
            if (v.dataLength >= 4) memcpy(&cur, v.data, 4);
            QString newStr = QInputDialog::getText(this, QStringLiteral("修改 DWORD 值"),
                QStringLiteral("数据 (十六进制):"), QLineEdit::Normal,
                QStringLiteral("%1").arg(cur, 8, 16, QChar('0')), &ok);
            if (!ok) return;
            newStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
            bool parseOk = false;
            quint32 val = newStr.toUInt(&parseOk, 16);
            if (!parseOk) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制数值"));
                return;
            }
            data = QByteArray((const char*)&val, 4);
            if (!drv.regSetValue(path, valueName, kRegDword, data)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                    QStringLiteral("内核写入值失败（键被占用或权限不足）"));
                return;
            }
            break;
        }
        case kRegQword: {
            quint64 cur = 0;
            if (v.dataLength >= 8) memcpy(&cur, v.data, 8);
            QString newStr = QInputDialog::getText(this, QStringLiteral("修改 QWORD 值"),
                QStringLiteral("数据 (十六进制):"), QLineEdit::Normal,
                QStringLiteral("%1").arg(cur, 16, 16, QChar('0')), &ok);
            if (!ok) return;
            newStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
            bool parseOk = false;
            quint64 val = newStr.toULongLong(&parseOk, 16);
            if (!parseOk) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制数值"));
                return;
            }
            data = QByteArray((const char*)&val, 8);
            if (!drv.regSetValue(path, valueName, kRegQword, data)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                    QStringLiteral("内核写入值失败（键被占用或权限不足）"));
                return;
            }
            break;
        }
        case kRegSz:
        case kRegExpandSz: {
            QString cur = formatRegData(v);
            QString newValue = QInputDialog::getText(this, QStringLiteral("修改字符串值"),
                QStringLiteral("数据:"), QLineEdit::Normal, cur, &ok);
            if (!ok) return;
            data = encodeRegString(newValue, false);
            if (!drv.regSetValue(path, valueName, v.type, data)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                    QStringLiteral("内核写入值失败（键被占用或权限不足）"));
                return;
            }
            break;
        }
        case kRegMultiSz: {
            QString cur = formatRegData(v);
            QString newValue = QInputDialog::getText(this, QStringLiteral("修改多字符串值"),
                QStringLiteral("数据 (多项以 ; 分隔):"), QLineEdit::Normal, cur, &ok);
            if (!ok) return;
            data = encodeRegString(newValue, true);
            if (!drv.regSetValue(path, valueName, kRegMultiSz, data)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                    QStringLiteral("内核写入值失败（键被占用或权限不足）"));
                return;
            }
            break;
        }
        default: {
            // 二进制：以十六进制编辑（空格分隔）
            QString cur;
            int n = (int)v.dataLength;
            if (n > 1024) n = 1024;
            for (int i = 0; i < n; ++i) cur += QString::asprintf("%02X ", v.data[i]);
            QString newStr = QInputDialog::getText(this, QStringLiteral("修改二进制值"),
                QStringLiteral("数据 (十六进制字节, 空格分隔):"), QLineEdit::Normal, cur.trimmed(), &ok);
            if (!ok) return;
            newStr.remove(QStringLiteral(" "));
            if ((newStr.size() % 2) != 0) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("十六进制长度必须为偶数"));
                return;
            }
            data = QByteArray::fromHex(newStr.toLatin1());
            if (!drv.regSetValue(path, valueName, v.type, data)) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                    QStringLiteral("内核写入值失败（键被占用或权限不足）"));
                return;
            }
            break;
        }
        }

        loadValues(path);
        return;
    }

    // 回退：QSettings 用户层
    auto* typeItem = m_valueTable->item(row, 1);
    QString typeStr = typeItem ? typeItem->text() : QStringLiteral("REG_SZ");

    if (typeStr == QStringLiteral("REG_DWORD")) {
        // DWORD 类型：以十六进制输入
        bool ok = false;
        QString oldVal = dataItem->text();
        oldVal.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
        oldVal = oldVal.left(oldVal.indexOf(QLatin1Char(' ')));
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

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        if (!drv.regDeleteValue(path, realName)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核删除值失败（键被占用或权限不足）"));
            return;
        }
    } else {
        QSettings settings(path, QSettings::NativeFormat);
        settings.remove(realName);
    }

    loadValues(path);
}

void RegeditPage::onRenameValue()
{
    int row = m_valueTable->currentRow();
    if (row < 0) return;

    auto* nameItem = m_valueTable->item(row, 0);
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

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        // 内核完整搬运数据（不受枚举截断影响）
        if (!drv.regRenameValue(path, oldName, newName)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核重命名值失败（键被占用或权限不足）"));
            return;
        }
    } else {
        // 回退：读取旧值写入新名称，然后删除旧值
        QSettings settings(path, QSettings::NativeFormat);
        QVariant value = settings.value(oldName);
        settings.setValue(newName, value);
        settings.remove(oldName);
    }

    loadValues(path);
}

void RegeditPage::onCreateValue()
{
    QString path = currentPath();
    if (path.isEmpty()) return;

    // 选择类型
    QStringList types = {QStringLiteral("REG_SZ"), QStringLiteral("REG_DWORD"),
                         QStringLiteral("REG_QWORD"), QStringLiteral("REG_MULTI_SZ"),
                         QStringLiteral("REG_BINARY"), QStringLiteral("REG_EXPAND_SZ")};
    bool ok = false;
    QString typeStr = QInputDialog::getItem(this, QStringLiteral("新建值"),
        QStringLiteral("选择类型:"), types, 0, false, &ok);
    if (!ok) return;

    QString name = QInputDialog::getText(this, QStringLiteral("新建值"),
        QStringLiteral("值名称:"), QLineEdit::Normal, QStringLiteral("新值 #1"), &ok);
    if (!ok || name.isEmpty()) return;

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        QByteArray data;
        quint32 type = kRegSz;

        if (typeStr == QStringLiteral("REG_DWORD")) {
            type = kRegDword;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建 DWORD 值"),
                QStringLiteral("数据 (十六进制):"), QLineEdit::Normal,
                QStringLiteral("00000000"), &ok);
            if (!ok) return;
            valStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
            bool parseOk = false;
            quint32 val = valStr.toUInt(&parseOk, 16);
            if (!parseOk) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制数值"));
                return;
            }
            data = QByteArray((const char*)&val, 4);
        } else if (typeStr == QStringLiteral("REG_QWORD")) {
            type = kRegQword;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建 QWORD 值"),
                QStringLiteral("数据 (十六进制):"), QLineEdit::Normal,
                QStringLiteral("0000000000000000"), &ok);
            if (!ok) return;
            valStr.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
            bool parseOk = false;
            quint64 val = valStr.toULongLong(&parseOk, 16);
            if (!parseOk) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("无效的十六进制数值"));
                return;
            }
            data = QByteArray((const char*)&val, 8);
        } else if (typeStr == QStringLiteral("REG_MULTI_SZ")) {
            type = kRegMultiSz;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建多字符串值"),
                QStringLiteral("数据 (多项以 ; 分隔):"), QLineEdit::Normal,
                QStringLiteral("值1; 值2"), &ok);
            if (!ok) return;
            data = encodeRegString(valStr, true);
        } else if (typeStr == QStringLiteral("REG_BINARY")) {
            type = kRegBinary;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建二进制值"),
                QStringLiteral("数据 (十六进制字节, 空格分隔):"), QLineEdit::Normal,
                QStringLiteral("00 00 00 00"), &ok);
            if (!ok) return;
            valStr.remove(QStringLiteral(" "));
            if ((valStr.size() % 2) != 0) {
                QMessageBox::warning(this, QStringLiteral("EFCHSword"), QStringLiteral("十六进制长度必须为偶数"));
                return;
            }
            data = QByteArray::fromHex(valStr.toLatin1());
        } else if (typeStr == QStringLiteral("REG_EXPAND_SZ")) {
            type = kRegExpandSz;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建可扩充字符串值"),
                QStringLiteral("数据:"), QLineEdit::Normal,
                QStringLiteral("%SystemRoot%"), &ok);
            if (!ok) return;
            data = encodeRegString(valStr, false);
        } else {
            type = kRegSz;
            QString valStr = QInputDialog::getText(this, QStringLiteral("新建字符串值"),
                QStringLiteral("数据:"), QLineEdit::Normal, QString(), &ok);
            if (!ok) return;
            data = encodeRegString(valStr, false);
        }

        if (!drv.regSetValue(path, name, type, data)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核写入值失败（键被占用或权限不足）"));
            return;
        }
    } else {
        // 回退：QSettings 用户层
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

    auto& drv = AppContext::instance().driver();
    if (drv.isLoaded()) {
        // 内核 ZwRenameKey：原子重命名，子键/值全保留
        if (!drv.regRenameKey(oldPath, newName)) {
            QMessageBox::warning(this, QStringLiteral("EFCHSword"),
                QStringLiteral("内核重命名项失败（键被占用或权限不足）"));
            return;
        }
    } else {
        // 回退：读取旧键所有值和子键，写入新键，然后删除旧键
        QString newPath = parentPath + QStringLiteral("\\") + newName;
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
    }

    // 刷新父项
    parentItem->takeChildren();
    loadSubKeys(parentItem, parentPath);
}
