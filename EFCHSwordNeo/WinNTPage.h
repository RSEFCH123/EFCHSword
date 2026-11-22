#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

// WinNT 标签页
// 对应易语言: WinNT 系统信息显示
class WinNTPage : public QWidget
{
    Q_OBJECT
public:
    explicit WinNTPage(QWidget* parent = nullptr);
    ~WinNTPage() override;

private slots:
    void onRefresh();

private:
    void setupUi();
    void loadSystemInfo();
    void loadKernelInfo();
    void loadHardwareInfo();

    QLabel* m_osLabel = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_memLabel = nullptr;
    QLabel* m_kernelLabel = nullptr;
    QTableWidget* m_driverTable = nullptr;
};
