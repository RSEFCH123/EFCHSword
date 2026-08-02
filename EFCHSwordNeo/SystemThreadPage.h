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
// 通过 IOCTL_ENUMTHREADBYPROCESS 枚举 PID=4 (System) 进程的所有线程
// 每个线程通过 IOCTL_GETDRIVERPATCHBYETHREAD 标注所属驱动
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
    QVector<ThreadInfo> m_threads;
};
