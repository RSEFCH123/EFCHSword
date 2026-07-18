#pragma once

#include <QWidget>

class QLabel;

// 关于页
class AboutPage : public QWidget
{
    Q_OBJECT
public:
    explicit AboutPage(QWidget* parent = nullptr);
    ~AboutPage() override;

private:
    void setupUi();
};
