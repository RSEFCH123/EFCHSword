#pragma once

#include <QWidget>

class QLabel;

// 欢迎页
class WelcomePage : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
    ~WelcomePage() override;

    void refreshStatus();

private:
    void setupUi();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_infoLabel = nullptr;
};
