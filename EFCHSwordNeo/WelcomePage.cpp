#include "WelcomePage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    refreshStatus();
}

WelcomePage::~WelcomePage() = default;

void WelcomePage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(20);

    // 标题
    m_titleLabel = new QLabel(QStringLiteral("EFCHSword Neo"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(28);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    auto* subtitle = new QLabel(
        QStringLiteral("计算机监视与管理软件 for Windows NT"), this);
    QFont subFont = subtitle->font();
    subFont.setPointSize(12);
    subtitle->setFont(subFont);
    subtitle->setAlignment(Qt::AlignCenter);

    // 分隔线
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    // 状态信息
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);

    // 详细信息
    m_infoLabel = new QLabel(this);
    m_infoLabel->setAlignment(Qt::AlignCenter);
    m_infoLabel->setWordWrap(true);

    // 命令行提示（只显示命令行，不再提供按钮）
    auto* hintLabel = new QLabel(
        QStringLiteral("命令行参数启动 Fake 模式:\n"
                       "  EFCHSwordNeo.exe -fake       (FakeMode, 伪装为 svchost)\n"
                       "  EFCHSwordNeo.exe -fakesafe   (FakeSafeMode, 伪装为 lsass)"), this);
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setWordWrap(true);

    mainLayout->addStretch();
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(subtitle);
    mainLayout->addWidget(line);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_infoLabel);
    mainLayout->addWidget(hintLabel);
    mainLayout->addStretch();
}

void WelcomePage::refreshStatus()
{
    auto& ctx = AppContext::instance();

    QString status;
    if (ctx.driverLoaded()) {
        status = QStringLiteral("驱动状态: 已加载 (EFCHKernelDriver)");
    } else {
        status = QStringLiteral("驱动状态: 未加载");
    }
    m_statusLabel->setText(status);

    QString info = QStringLiteral(
        "版本: %1\n"
        "操作系统: %2 NTVersion:%3.%4 Build:%5\n"
        "FakeMode: %6")
        .arg(ctx.version())
        .arg(ctx.winName()).arg(ctx.winMajor()).arg(ctx.winMinor()).arg(ctx.winBuild())
        .arg(ctx.fakeMode() ? (ctx.fakeSafeMode() ? QStringLiteral("FakeSafeMode") : QStringLiteral("FakeMode"))
                            : QStringLiteral("未启用"));
    m_infoLabel->setText(info);
}
