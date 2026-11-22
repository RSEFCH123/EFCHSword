#pragma once

#include <QWidget>
#include <QVector>
#include "KernelDriver.h"   // ThreadInfo

class QTableWidget;
class QPushButton;
class QLabel;
class QMenu;
class QLineEdit;

// 系统线程管理标签页
// 通过 IOCTL_CID_ENUMSYSTEMTHREADS (0x604) 枚举全部线程：
//   内核 CID 表全量遍历 + PsIsSystemThread 判定 + 起始地址模块归属，
//   一次 IOCTL 返回完整列表（含每个系统线程所属驱动文件名）。
// 支持通过 IOCTL_APCTERMINATETHREADBYETHREAD 销毁线程
class SystemThreadPage : public QWidget
{
    Q_OBJECT
public:
    explicit SystemThreadPage(QWidget* parent = nullptr);
    ~SystemThreadPage() override;

    void refreshThreads();

private slots:
    void onRefreshClicked();
    void onContextMenu(const QPoint& pos);
    void onDestroyThread();
    void onCopyTid();
    void onCopyEThread();
    void onCopyDriverPath();

private:
    void setupUi();
    void setupContextMenu();
    quint64 currentEThread() const;
    quint32 currentTid() const;

    QTableWidget* m_table = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QMenu* m_contextMenu = nullptr;

    // 当前显示的线程列表（用于右键定位）
    QVector<ArkSysThreadEntry> m_threads;
};
