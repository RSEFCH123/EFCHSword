#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

// PC 信息对话框
// 对应易语言: PCInfo_Window
class PCInfoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PCInfoDialog(QWidget* parent = nullptr);
    ~PCInfoDialog() override;

private:
    void setupUi();
    void loadOsInfo();
    void loadCpuInfo();
    void loadMemoryInfo();
    void loadVideoCards();
    void loadNetworkCards();

    QLabel* m_osLabel = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_memLabel = nullptr;
    QTableWidget* m_videoTable = nullptr;
    QTableWidget* m_netTable = nullptr;
};
