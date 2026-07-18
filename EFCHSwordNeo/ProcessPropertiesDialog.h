#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>

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

private:
    void setupUi();
    void loadProcessInfo();
    void loadThreads();
    void loadWindows();

    quint32 m_pid;
    QLabel*     m_nameLabel = nullptr;
    QLabel*     m_synopsisLabel = nullptr;
    QLabel*     m_verLabel = nullptr;
    QLabel*     m_parentLabel = nullptr;
    QLabel*     m_startTimeLabel = nullptr;
    QLabel*     m_userLabel = nullptr;
    QLabel*     m_imageLabel = nullptr;
    QLineEdit*  m_filePathEdit = nullptr;
    QLineEdit*  m_cmdLineEdit = nullptr;
    QTableWidget* m_threadTable = nullptr;
    QTableWidget* m_windowTable = nullptr;
    QComboBox*  m_pplCombo = nullptr;
    QPushButton* m_pplSetBtn = nullptr;
};
