#pragma once

#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QDateTime>
#include "KernelDriver.h"

class QTableWidget;
class QLineEdit;
class QLabel;
class QCheckBox;
class QTimer;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;

// 进程行为监视页
// 内核环事件：进程创建/退出、线程创建/退出、镜像加载、注册表写操作(8种)、
//             文件操作(3种)、磁盘写入、跨进程句柄(打开/复制)
// R3 本地事件：网络连接建立/断开/新监听端口（TCP 表轮询差分）
// 过滤面板为树形（主枝=类别，枝条=子项），支持全局/多选目标(含子进程跟踪)、
// CSV 导出、"启动并监视"（管理员/降权可选）。
class ProcessBehaviorPage : public QWidget, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit ProcessBehaviorPage(QWidget* parent = nullptr);
    ~ProcessBehaviorPage() override;

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void onToggleMonitor();
    void onPollTimeout();
    void onPickTargets();
    void onClearEvents();
    void onExportCsv();
    void onLaunchAndMonitor();
    void onKillTargets();         // 强制终止监视目标(含子进程)
    void onSuspendTargets();      // 挂起监视目标(含子进程)
    void onFilterItemChanged(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);

private:
    void setupUi();
    void setupFilterTree();
    void setupGlobalHotkeys();    // RegisterHotKey 全局热键（无需窗口焦点）
    void rebuildTable();          // 按当前树状态重新渲染全部事件（临时显示过滤）
    void appendEvent(const ArkBehaviorEvent& ev);
    void appendEventRow(const ArkBehaviorEvent& ev);   // 渲染单行（不过滤, 调用方负责）
    void appendNetEvent(const QString& eventName, quint32 pid, const QString& image,
                        const QString& local, const QString& remote);
    bool eventMatchesFilters(const ArkBehaviorEvent& ev) const;
    bool netSubChecked(int sub) const;           // sub: 0=建立 1=断开 2=监听
    void applyKernelFilterMask();
    void pollNetworkR3();
    QString pidNameCached(quint32 pid);
    // 启动辅助：返回是否成功；outPid 出 PID；hProcess/hThread 由调用方关闭
    // （mode 2/3 以挂起态创建，调用方完成门控后负责 Resume/Terminate）
    bool launchProcess(const QString& exePath, int mode, bool bypass740,
                       quint32* outPid, HANDLE* outProcess, HANDLE* outThread);
    bool isChildElevated(quint32 pid);
    static bool isUacDisabled();
    static QByteArray envBlockWithCompatLayer(const wchar_t* compatLayer);
    void syncHardenState();                      // 按开关同步名单
    static quint64 subKey(int kind, int op) {
        return ((quint64)kind << 32) | (quint64)(quint32)op;
    }

    QSplitter* m_splitter = nullptr;
    QTreeWidget* m_filterTree = nullptr;         // 监视行为树（左）
    QTableWidget* m_table = nullptr;             // 事件表（右）
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_toggleBtn = nullptr;
    QPushButton* m_pickBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_launchBtn = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QCheckBox* m_globalWatch = nullptr;          // 全局监视（不过滤 PID）
    bool m_blockMalware = false;                 // 阻止可能的恶意行为（启动对话框选择）

    // 树节点映射
    QHash<int, QTreeWidgetItem*> m_kindItems;    // kind(BH_*) -> 类别节点
    QHash<quint64, QTreeWidgetItem*> m_subItems; // (kind,op) -> 子项节点
    QTreeWidgetItem* m_diskBigOnlyItem = nullptr;
    bool m_treeUpdating = false;                 // 防止程序化改状态时递归

    QTimer* m_pollTimer = nullptr;

    quint64 m_lastSeq = 0;
    bool m_monitoring = false;
    bool m_watchChildren = false;
    QSet<quint32> m_watchPids;
    QSet<quint32> m_hardenedPids;                // 已加入内核 Harden 名单的 PID
    QString m_watchSummary;
    QVector<ArkBehaviorEvent> m_allEvents;       // 全量事件（临时显示过滤的数据源）

    // R3 网络轮询差分状态
    struct NetConn {
        quint32 pid;
        quint32 localAddr;
        quint16 localPort;
        quint32 remoteAddr;
        quint16 remotePort;
        bool operator==(const NetConn& o) const {
            return pid == o.pid && localAddr == o.localAddr && localPort == o.localPort &&
                   remoteAddr == o.remoteAddr && remotePort == o.remotePort;
        }
    };
    QVector<NetConn> m_prevNetConns;
    struct NetListen {
        quint32 pid;
        quint32 localAddr;
        quint16 localPort;
        bool operator==(const NetListen& o) const {
            return pid == o.pid && localAddr == o.localAddr && localPort == o.localPort;
        }
    };
    QVector<NetListen> m_prevNetListens;
    bool m_netFirstPoll = true;

    QHash<quint32, QString> m_pidNameCache;
    QDateTime m_pidNameCacheTime;

    // RegisterHotKey 句柄/实际组合键（注册失败时自动回退）
    void* m_hotkeyHwnd = nullptr;
    unsigned int m_killHotkeyId = 0;
    unsigned int m_suspendHotkeyId = 0;
    unsigned int m_hotkeyMods = 0;
    unsigned int m_killHotkeyVk = 0;
    unsigned int m_suspendHotkeyVk = 0;
};
