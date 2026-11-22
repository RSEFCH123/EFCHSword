#include "WelcomePage.h"
#include "AppContext.h"
#include "ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>

// 状态卡片：圆角边框 + 标题 + 内容
class StatusCard : public QFrame {
public:
    StatusCard(const QString& icon, const QString& title, QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("welcomeCard"));
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 10, 14, 10);
        lay->setSpacing(4);

        auto* headRow = new QHBoxLayout();
        auto* iconLabel = new QLabel(icon, this);
        iconLabel->setStyleSheet(QStringLiteral("font-size: 18px; background: transparent; border: none;"));
        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setStyleSheet(
            QStringLiteral("color: #8a8a8a; font-size: 12px; background: transparent; border: none;"));
        headRow->addWidget(iconLabel);
        headRow->addWidget(m_titleLabel);
        headRow->addStretch();
        lay->addLayout(headRow);

        m_valueLabel = new QLabel(this);
        m_valueLabel->setStyleSheet(
            QStringLiteral("font-size: 14px; font-weight: 600; background: transparent; border: none;"));
        m_valueLabel->setWordWrap(true);
        lay->addWidget(m_valueLabel);
    }

    void setValue(const QString& text) { m_valueLabel->setText(text); }
    void setValueColor(const QString& color) {
        QString base = QStringLiteral(
            "font-size: 14px; font-weight: 600; background: transparent; border: none;");
        if (!color.isEmpty()) base += QStringLiteral(" color: %1;").arg(color);
        m_valueLabel->setStyleSheet(base); }

private:
    QLabel* m_titleLabel = nullptr;
    QLabel* m_valueLabel = nullptr;
};

QString cardSheet(bool dark)
{
    Q_UNUSED(dark);
    return QStringLiteral(
        "#welcomeCard { background: %1; border: 1px solid %2; border-radius: 8px; }");
}

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    refreshStatus();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::Theme) { applyTheme(); refreshStatus(); });
}

WelcomePage::~WelcomePage() = default;

void WelcomePage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(48, 36, 48, 28);
    mainLayout->setSpacing(12);

    // ---- 顶部: Logo + 标题 ----
    auto* titleRow = new QHBoxLayout();
    titleRow->addStretch();

    m_logoLabel = new QLabel(QStringLiteral("⚔️"), this);
    m_logoLabel->setStyleSheet(QStringLiteral("font-size: 52px; background: transparent;"));
    titleRow->addWidget(m_logoLabel);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setText(QStringLiteral(
        "<span style='font-size:34px; font-weight:700;'>EFCHSword </span>"
        "<span style='font-size:34px; font-weight:700; color:#007acc;'>Neo</span>"));
    titleCol->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(QStringLiteral("计算机监视与管理软件 for Windows NT"), this);
    m_subtitleLabel->setStyleSheet(QStringLiteral("font-size: 13px;"));
    titleCol->addWidget(m_subtitleLabel);

    titleRow->addLayout(titleCol);
    titleRow->addStretch();
    mainLayout->addLayout(titleRow);

    // ---- 版本徽章行 ----
    auto* badgeRow = new QHBoxLayout();
    badgeRow->addStretch();
    m_versionBadge = new QLabel(this);
    m_versionBadge->setTextFormat(Qt::RichText);
    m_versionBadge->setStyleSheet(QStringLiteral(
        "background: rgba(0,122,204,0.15); color: #007acc;"
        "border: 1px solid rgba(0,122,204,0.55); border-radius: 10px;"
        "padding: 3px 14px; font-size: 12px; font-weight: 600;"));
    badgeRow->addWidget(m_versionBadge);
    badgeRow->addStretch();
    mainLayout->addLayout(badgeRow);

    mainLayout->addSpacing(14);

    // ---- 三张状态卡片 ----
    auto* cardRow = new QHBoxLayout();
    cardRow->setSpacing(12);
    m_driverCard = new StatusCard(QStringLiteral("🛡️"), QStringLiteral("驱动状态"), this);
    m_systemCard = new StatusCard(QStringLiteral("🖥️"), QStringLiteral("操作系统"), this);
    m_fakeCard   = new StatusCard(QStringLiteral("🎭"), QStringLiteral("FakeMode"), this);
    cardRow->addWidget(m_driverCard, 1);
    cardRow->addWidget(m_systemCard, 1);
    cardRow->addWidget(m_fakeCard, 1);
    mainLayout->addLayout(cardRow);

    mainLayout->addSpacing(14);

    // ---- 命令行提示卡片 ----
    m_hintLabel = new QLabel(this);
    m_hintLabel->setTextFormat(Qt::RichText);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setText(QStringLiteral(
        "<div style='line-height:150%'>"
        "<b>命令行参数启动 Fake 模式</b><br>"
        "<code>EFCHSwordNeo.exe -fake</code> &nbsp;FakeMode, 伪装为 svchost<br>"
        "<code>EFCHSwordNeo.exe -fakesafe</code> &nbsp;FakeSafeMode, 伪装为 lsass"
        "</div>"));
    m_hintLabel->setObjectName(QStringLiteral("welcomeCard"));
    mainLayout->addWidget(m_hintLabel);

    mainLayout->addStretch();

    // ---- 页脚 ----
    m_footerLabel = new QLabel(this);
    m_footerLabel->setAlignment(Qt::AlignCenter);
    m_footerLabel->setTextFormat(Qt::RichText);
    mainLayout->addWidget(m_footerLabel);

    applyTheme();
}

void WelcomePage::applyTheme()
{
    const bool dark = ThemeManager::instance().isDark();
    const QString text = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#1e1e1e");
    const QString subText = dark ? QStringLiteral("#9d9d9d") : QStringLiteral("#5a5a5a");
    const QString cardBg = dark ? QStringLiteral("#252526") : QStringLiteral("#ffffff");
    const QString cardBorder = dark ? QStringLiteral("#3d3d40") : QStringLiteral("#dcdcdc");

    QString sheet = cardSheet(dark)
        .arg(cardBg, cardBorder);
    setStyleSheet(sheet);

    m_subtitleLabel->setStyleSheet(
        QStringLiteral("font-size: 13px; color: %1; background: transparent;").arg(subText));
    m_hintLabel->setStyleSheet(QStringLiteral(
        "#welcomeCard { background: %1; border: 1px solid %2; border-radius: 8px; }"
        "#welcomeCard code { background: %3; color: #4ec9b0; padding: 1px 5px; border-radius: 3px; }"
        "#welcomeLabel b { color: %4; }")
        .arg(cardBg, cardBorder,
             dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f0f0f0"), text));
    m_footerLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; background: transparent;").arg(subText));
    m_versionBadge->setStyleSheet(QStringLiteral(
        "background: rgba(0,122,204,0.15); color: %1;"
        "border: 1px solid rgba(0,122,204,0.55); border-radius: 10px;"
        "padding: 3px 14px; font-size: 12px; font-weight: 600;")
        .arg(dark ? QStringLiteral("#3794ff") : QStringLiteral("#007acc")));
}

void WelcomePage::refreshStatus()
{
    auto& ctx = AppContext::instance();

    if (ctx.driverLoaded()) {
        m_driverCard->setValue(QStringLiteral("已加载 (EFCHKernelDriver)"));
        m_driverCard->setValueColor(QStringLiteral("#4ec9b0"));
    } else {
        m_driverCard->setValue(QStringLiteral("未加载"));
        m_driverCard->setValueColor(ThemeManager::instance().isDark()
            ? QStringLiteral("#f48771") : QStringLiteral("#c0392b"));
    }

    m_systemCard->setValue(QStringLiteral("%1  NT %2.%3  Build %4")
        .arg(ctx.winName()).arg(ctx.winMajor()).arg(ctx.winMinor()).arg(ctx.winBuild()));
    m_systemCard->setValueColor(QString());

    m_fakeCard->setValue(ctx.fakeMode()
        ? (ctx.fakeSafeMode() ? QStringLiteral("FakeSafeMode (lsass)") : QStringLiteral("FakeMode (svchost)"))
        : QStringLiteral("未启用"));
    m_fakeCard->setValueColor(QString());

    m_versionBadge->setText(QStringLiteral("版本 %1").arg(ctx.version()));
    m_footerLabel->setText(QStringLiteral(
        "bilibili 燃烧EFCH · 官方群 1050281660 · rsefch123.github.io/efchsword"));
}
