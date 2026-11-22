#pragma once

#include <QWidget>
#include <QTimer>
#include "KernelDriver.h"

class QLabel;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QLineEdit;
class QPlainTextEdit;

// Hypervisor (Intel VT-x) 页
// 支持检测 / 启停 / CPUID 拦截功能（隐藏虚拟化痕迹、伪造厂商串）/ exit 统计
class HypervisorPage : public QWidget {
    Q_OBJECT
public:
    explicit HypervisorPage(QWidget* parent = nullptr);
    ~HypervisorPage() override;

private slots:
    void onParityNow();          // 一键进 Parity Error（三重故障复位）
    void refreshStatus();
    void startHypervisor();
    void stopHypervisor();
    void onRing1Read();
    void onRing1Write();

private:
    void updateUi(const KernelDriver::HvQueryInfo& info);
    static QString hexDump(const QByteArray& data);
    static QByteArray parseHex(const QString& text);

    QCheckBox* m_hideHvCheck = nullptr;
    QCheckBox* m_spoofVendorCheck = nullptr;
    QCheckBox* m_nestedVmxCheck = nullptr;
    QCheckBox* m_evmcsCheck = nullptr;
    QCheckBox* m_veCheck = nullptr;
    QCheckBox* m_vmfuncCheck = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_parityBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_vendorLabel = nullptr;
    QTreeWidget* m_capabilityTree = nullptr;
    QTableWidget* m_exitTable = nullptr;
    QTimer* m_timer = nullptr;
    quint64 m_lastTotalExits = 0;

    // Ring-1 内存读写
    QLineEdit* m_r1PidEdit = nullptr;
    QLineEdit* m_r1AddrEdit = nullptr;
    QLineEdit* m_r1SizeEdit = nullptr;
    QPlainTextEdit* m_r1DataEdit = nullptr;
    QPushButton* m_r1ReadBtn = nullptr;
    QPushButton* m_r1WriteBtn = nullptr;
    QLabel* m_r1StatusLabel = nullptr;
};
