#pragma once

#include <QWidget>

class QLabel;
class QFrame;

// 关于页
class AboutPage : public QWidget
{
    Q_OBJECT
public:
    explicit AboutPage(QWidget* parent = nullptr);
    ~AboutPage() override;

private:
    void setupUi();
    void applyTheme();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_versionBadge = nullptr;
    QFrame* m_infoCard = nullptr;
    QLabel* m_authorLabel = nullptr;
    QLabel* m_websiteLabel = nullptr;
    QLabel* m_groupLabel = nullptr;
    QLabel* m_envLabel = nullptr;
    QLabel* m_envNoteLabel = nullptr;
    QLabel* m_copyrightLabel = nullptr;
};
