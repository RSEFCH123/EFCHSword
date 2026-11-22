#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QIcon>
#include <QFileInfo>

class QTableWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QMenu;
class QLabel;
class QRadioButton;

// 文件管理标签页
// 对应易语言: 树形框 + 超级列表框_文件浏览 + 文件操作右键菜单
class FilePage : public QWidget
{
    Q_OBJECT
public:
    explicit FilePage(QWidget* parent = nullptr);
    ~FilePage() override;

private slots:
    void onDriveChanged(int index);
    void onGotoClicked();
    void onUpClicked();
    void onRefreshClicked();
    void onFastScanToggled(bool checked);
    void onCellDoubleClicked(int row, int col);
    void onContextMenu(const QPoint& pos);

    // 文件操作
    void onRunFile();
    void onRunHidden();
    void onRename();
    void onDeleteNormal();
    void onForceDelete();
    void onPosixForceDelete();
    void onZwDeleteFile();
    void onNtfsDelete();
    void onCopyFile();
    void onMoveFile();
    void onProtectFile();
    void onForceOccupyFile();
    void onScanOccupy();
    void onReadFile();
    void onShowProperties();
    void onUseExplorerMenu();
    void onCreateDirectory();
    void onSortModeChanged();

private:
    void setupUi();
    void setupContextMenu();
    void setupDrives();
    void browseTo(const QString& dir);
    QString currentPath() const;
    QString selectedFilePath() const;
    QStringList selectedFilePaths() const;
    bool isDirectoryRow(int row) const;
    // 多选删除公共流程: mode 0=IRP强制删除 1=POSIX 2=ZwDeleteFile
    void deleteSelectedItems(int mode, const QString& title);
    // DirectNTFS 模式下枚举目录 (绕过文件系统, 直接解析 NTFS)
    bool browseToDirectNtfs(const QString& dir);

    QComboBox* m_driveCombo = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QTableWidget* m_table = nullptr;
    QCheckBox* m_fastScan = nullptr;
    QLabel* m_statusLabel = nullptr;
    QMenu* m_contextMenu = nullptr;
    QMenu* m_folderMenu = nullptr;
    class QAction* m_ntfsDeleteAction = nullptr;
    QRadioButton* m_sortMixedRadio = nullptr;   // 文件名混排排序
    QRadioButton* m_sortFolderFirstRadio = nullptr;  // 文件夹优先排序
    QRadioButton* m_modeIrpRadio = nullptr;        // IRP 模式
    QRadioButton* m_modeDirectNtfsRadio = nullptr; // DirectNTFS 模式

    // DirectNTFS 模式下已初始化的盘符, 避免重复初始化
    QChar m_ntfsInitializedDrive;

    QString m_currentDir;

    // 文件图标缓存：扩展名 -> 图标（文件夹用空串作 key）
    QHash<QString, QIcon> m_iconCache;
    QIcon iconForFile(const QFileInfo& fi);
};
