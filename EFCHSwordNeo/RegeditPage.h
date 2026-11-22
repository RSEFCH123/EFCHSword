#pragma once

#include <QWidget>
#include <QVector>
#include "KernelDriver.h"

class QTreeWidget;
class QTableWidget;
class QLineEdit;
class QTreeWidgetItem;

// 注册表标签页
// 驱动可用时全部操作走内核 IOCTL（System 令牌上下文，可越过 HKLM 等
// 键的普通 ACL 限制）；无驱动时回退 QSettings 用户层实现。
class RegeditPage : public QWidget
{
    Q_OBJECT
public:
    explicit RegeditPage(QWidget* parent = nullptr);
    ~RegeditPage() override;

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemClicked(QTreeWidgetItem* item, int col);
    void onRefresh();
    void onTreeContextMenu(const QPoint& pos);
    void onTableContextMenu(const QPoint& pos);
    void onDeleteKey();
    void onCreateKey();
    void onRenameKey();
    // 值操作
    void onModifyValue();
    void onDeleteValue();
    void onRenameValue();
    void onCreateValue();

private:
    void setupUi();
    void setupTreeContextMenu();
    void setupTableContextMenu();
    void loadSubKeys(QTreeWidgetItem* parent, const QString& fullPath);
    QStringList childGroupsSafe(const QString& fullPath);
    void loadValues(const QString& fullPath);
    QString currentPath() const;

    QTreeWidget* m_tree = nullptr;
    QTableWidget* m_valueTable = nullptr;
    class QMenu* m_treeMenu = nullptr;
    class QMenu* m_tableMenu = nullptr;

    // 内核枚举的当前键值原始数据（供修改时取类型/初值），行号即下标
    QVector<ArkRegValue> m_currentValues;
};
