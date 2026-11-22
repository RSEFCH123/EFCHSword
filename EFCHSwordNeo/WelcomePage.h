#pragma once

#include <QWidget>

class QLabel;
class StatusCard;

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
    void applyTheme();

    QLabel* m_logoLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_versionBadge = nullptr;
    StatusCard* m_driverCard = nullptr;
    StatusCard* m_systemCard = nullptr;
    StatusCard* m_fakeCard = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_footerLabel = nullptr;
};
