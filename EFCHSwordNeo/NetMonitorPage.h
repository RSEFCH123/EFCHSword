#pragma once

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <deque>
#include <atomic>
#include <memory>

#include "NetTable.h"   // 必须先于 windows.h (winsock2 顺序)

class QListWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class QPlainTextEdit;
class QMenu;
struct CapturedPacket;

// 网络监控页
// Tab1 数据包捕获: 原始套接字(SIO_RCVALL)抓包 -> 进程(多选)过滤 -> 分层解析/Hex/ASCII 可视化
// Tab2 连接列表: 全局 TCP/UDP 连接表 (GetExtendedTcpTable/GetExtendedUdpTable)
class NetMonitorPage : public QWidget
{
    Q_OBJECT
public:
    explicit NetMonitorPage(QWidget* parent = nullptr);
    ~NetMonitorPage() override;

    void refreshConnections();

private slots:
    // ---- 连接列表（原功能保留）
    void onAutoRefreshToggled(bool on);
    void onContextMenu(const QPoint& pos);
    void onDisconnectConnection();
    void onKillProcess();
    void onCopyRow();

    // ---- 抓包
    void onStartStopCapture();
    void refreshAdapters();
    void refreshProcessList();
    void onDrainPackets();
    void onPacketSelected();
    void onExportPcap();
    void onClearPackets();

private:
    void setupUi();
    void setupCaptureTab();
    void setupConnectionTab();
    void showPacketDetail(const CapturedPacket& pkt);
    quint32 currentPid() const;

    // ===== 抓包 Tab =====
    QComboBox* m_adapterCombo = nullptr;
    QPushButton* m_captureBtn = nullptr;
    QPushButton* m_pcapExportBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_procRefreshBtn = nullptr;
    QLineEdit* m_procSearchEdit = nullptr;
    QLineEdit* m_pktFilterEdit = nullptr;
    QCheckBox* m_autoScrollCheck = nullptr;
    QCheckBox* m_pauseCheck = nullptr;
    QListWidget* m_processList = nullptr;      // 进程多选
    QTableWidget* m_pktTable = nullptr;
    QTabWidget* m_detailTabs = nullptr;
    QTreeWidget* m_decodeTree = nullptr;
    QPlainTextEdit* m_hexView = nullptr;
    QPlainTextEdit* m_asciiView = nullptr;
    QLabel* m_pktStatsLabel = nullptr;
    QTimer* m_drainTimer = nullptr;

    // ===== 连接列表 Tab =====
    QTableWidget* m_table = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QCheckBox* m_udpCheck = nullptr;
    QCheckBox* m_autoCheck = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLineEdit* m_capturePidEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_timer = nullptr;
    QMenu* m_contextMenu = nullptr;
    QVector<NetConnInfo> m_conns;

    QTabWidget* m_rootTabs = nullptr;

    // ===== 抓包引擎（内部类，见 .cpp） =====
    class CaptureEngine;
    std::unique_ptr<CaptureEngine> m_engine;
};
