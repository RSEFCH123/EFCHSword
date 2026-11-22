#pragma once

#include <QWidget>
#include <QHash>
#include <QIcon>

#include <windows.h>

class QTreeWidget;
class QTreeWidgetItem;
class QMenu;
class QCheckBox;
class QLabel;
class QTimer;

// 进程管理标签页
// 使用树形框展示进程父子关系（ProcessHacker 风格），含图标
// 使用驱动 EPROCESSEnumProcess 枚举（可发现隐藏进程），IOCTL_GETPROCESSIMAGENAME 取路径
class ProcessPage : public QWidget
{
    Q_OBJECT
public:
    explicit ProcessPage(QWidget* parent = nullptr);
    ~ProcessPage() override;

    void refreshProcesses();

private slots:
    void onRefreshClicked();
    void onFastScanToggled(bool checked);
    void onContextMenu(const QPoint& pos);
    void onItemSelectionChanged();
    void onItemDoubleClicked(QTreeWidgetItem* item, int col);

    // 进程操作
    void onKillProcess();              // 驱动结束进程（直接 zwKill/psp/apc）
    void onForceKillProcess();         // 强制结束进程（Ctrl信号→窗口消息→驱动结束）
    void onHvmKillProcess();           // HVM 强杀：R-1 拒绝执行关键页，命中注入 #UD
    void onHvmFreezeProcess();         // HVM 冻结：命中注入 #PF，原地自旋，可撤销
    void onHvmResumeProcess();         // HVM 恢复：撤销冻结/强杀处置
    void onSuspendProcess();
    void onResumeProcess();
    void onHideProcess();
    void onUnHideProcess();
    void onProtectProcess();
    void onUnProtectProcess();
    void onForceProtectProcess();
    void onUnForceProtectProcess();
    void onMemKillProcess();
    void onSetPPL();
    void onSetCritical();
    void onUnSetCritical();
    void onOpenFileLocation();
    void onCopyCommandLine();
    void onCopyFilePath();
    void onShowProcessProperties();

private:
    void setupUi();
    void setupContextMenu();
    quint32 currentPid() const;
    QString currentProcessName() const;
    QString currentProcessPath() const;
    void killProcessWithFallback(quint32 pid);  // 驱动结束
    bool forceKillProcess(quint32 pid);          // 强制结束：返回是否已结束
    static BOOL CALLBACK sendCloseMsgToWnd(HWND hwnd, LPARAM lParam);
    QIcon iconForPath(const QString& path);      // 获取（带缓存的）exe 图标

    // CPU 占用采样
    struct CpuSample {
        ULARGE_INTEGER lastKernel;
        ULARGE_INTEGER lastUser;
        ULARGE_INTEGER lastTime;
    };
    void updateCpuBeginSample();                 // 刷新开始时采样
    double cpuUsageForPid(quint32 pid);          // 计算单进程 CPU 占用百分比

    QTreeWidget* m_tree = nullptr;
    QCheckBox* m_fastScan = nullptr;
    QLabel* m_statusLabel = nullptr;
    QMenu* m_contextMenu = nullptr;
    QTimer* m_autoRefreshTimer = nullptr;

    // 图标缓存：路径 -> 图标
    QHash<QString, QIcon> m_iconCache;

    // CPU 采样缓存：PID -> 采样
    QHash<quint32, CpuSample> m_cpuSamples;
    ULARGE_INTEGER m_lastTotalTime {};
    int m_cpuCoreCount = 1;
};
