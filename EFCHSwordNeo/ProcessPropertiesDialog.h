#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QPlainTextEdit>

class ProcessPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProcessPropertiesDialog(quint32 pid, QWidget* parent = nullptr);

private slots:
    void onOpenFileLocation();
    void onShowFileProperties();
    void onKillProcess();
    void onRefreshThreads();
    void onRefreshWindows();
    void onTerminateThread();
    void onSetPPL();

    // 权力管理
    void onLoadPower();
    void onSetPriority();
    void onApplyAffinity();
    void onTogglePrivilege();
    // 句柄管理
    void onLoadHandles();
    void onCloseSelectedHandle();
    void onShowHandleDetails();
    // 内存管理
    void onQueryMemory();
    void onReadMemory();
    void onWriteMemory();
    // Token
    void onLoadToken();
    // 网络连接
    void onLoadNetConnections();
    // 热键监视 (RegisterHotKey 枚举，按当前进程过滤)
    void onRefreshProcessHotkeys();

private:
    void setupUi();
    void loadProcessInfo();
    void loadThreads();
    void loadWindows();
    void setupPowerTab(class QWidget* tab);
    void setupHandleTab(class QWidget* tab);
    void setupMemoryTab(class QWidget* tab);
    void setupTokenTab(class QWidget* tab);
    void setupNetTab(class QWidget* tab);
    void setupHotkeyTab(class QWidget* tab);

    quint32 m_pid;

    // 基本信息
    QLabel*     m_nameLabel = nullptr;
    QLabel*     m_synopsisLabel = nullptr;
    QLabel*     m_verLabel = nullptr;
    QLabel*     m_parentLabel = nullptr;
    QLabel*     m_startTimeLabel = nullptr;
    QLabel*     m_userLabel = nullptr;
    QLabel*     m_imageLabel = nullptr;
    QLineEdit*  m_filePathEdit = nullptr;
    QLineEdit*  m_cmdLineEdit = nullptr;

    // 线程 / 窗口
    QTableWidget* m_threadTable = nullptr;
    QTableWidget* m_windowTable = nullptr;

    // PPL
    QComboBox*  m_pplCombo = nullptr;
    QPushButton* m_pplSetBtn = nullptr;

    // 权力管理
    QComboBox* m_priorityCombo = nullptr;
    QVector<class QCheckBox*> m_affinityChecks;
    QLabel* m_affinityLabel = nullptr;
    QTableWidget* m_privTable = nullptr;

    // 句柄管理
    QTableWidget* m_handleTable = nullptr;
    QPushButton* m_handleRefreshBtn = nullptr;
    QPushButton* m_handleCloseBtn = nullptr;
    QPushButton* m_handleDetailBtn = nullptr;
    QLabel* m_handleCountLabel = nullptr;

    // 内存管理
    QLineEdit* m_memAddrEdit = nullptr;
    QLineEdit* m_memSizeEdit = nullptr;
    QLabel* m_memRegionLabel = nullptr;
    QPlainTextEdit* m_memHexView = nullptr;

    // Token
    QLabel* m_tokUserLabel = nullptr;
    QLabel* m_tokSessionLabel = nullptr;
    QLabel* m_tokElevLabel = nullptr;
    QLabel* m_tokIntegrityLabel = nullptr;
    QLabel* m_tokGroupsLabel = nullptr;

    // 网络连接
    QTableWidget* m_netTable = nullptr;
    QPushButton* m_netRefreshBtn = nullptr;

    // 热键监视
    QTableWidget* m_kbdTable = nullptr;
    QLabel* m_kbdStateLabel = nullptr;
};
