#pragma once

#include <QWidget>

class QTreeWidget;
class QTableWidget;
class QLineEdit;
class QTreeWidgetItem;

// 注册表标签页
// 对应易语言: 组合框_注册表 + 树形框注册表 + 注册表操作
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
    void loadValues(const QString& fullPath);
    QString currentPath() const;

    QTreeWidget* m_tree = nullptr;
    QTableWidget* m_valueTable = nullptr;
    class QMenu* m_treeMenu = nullptr;
    class QMenu* m_tableMenu = nullptr;
};
