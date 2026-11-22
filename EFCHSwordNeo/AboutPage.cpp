#include "AboutPage.h"
#include "AppContext.h"
#include "ThemeManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>

AboutPage::AboutPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::Theme) { applyTheme(); });
}

AboutPage::~AboutPage() = default;

void AboutPage::setupUi()
{
    auto& ctx = AppContext::instance();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(48, 36, 48, 28);
    mainLayout->setSpacing(10);

    // ---- Logo + 产品名 ----
    auto* headRow = new QHBoxLayout();
    headRow->addStretch();
    auto* logo = new QLabel(QStringLiteral("⚔️"), this);
    logo->setStyleSheet(QStringLiteral("font-size: 44px; background: transparent;"));
    headRow->addWidget(logo);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setText(QStringLiteral(
        "<span style='font-size:30px; font-weight:700;'>EFCHSword </span>"
        "<span style='font-size:30px; font-weight:700; color:#007acc;'>Neo</span>"));
    headRow->addWidget(m_titleLabel);
    headRow->addStretch();
    mainLayout->addLayout(headRow);

    // ---- 版本徽章 ----
    auto* badgeRow = new QHBoxLayout();
    badgeRow->addStretch();
    m_versionBadge = new QLabel(this);
    m_versionBadge->setTextFormat(Qt::RichText);
    badgeRow->addWidget(m_versionBadge);
    badgeRow->addStretch();
    mainLayout->addLayout(badgeRow);

    mainLayout->addSpacing(12);

    // ---- 信息卡片 ----
    m_infoCard = new QFrame(this);
    m_infoCard->setObjectName(QStringLiteral("aboutCard"));
    auto* cardLayout = new QVBoxLayout(m_infoCard);
    cardLayout->setContentsMargins(24, 18, 24, 18);
    cardLayout->setSpacing(10);

    auto* slogan = new QLabel(QStringLiteral("计算机监视与管理软件 for Windows NT · Qt 重构版本"), m_infoCard);
    slogan->setObjectName(QStringLiteral("aboutSlogan"));
    slogan->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(slogan);

    auto* line = new QFrame(m_infoCard);
    line->setFrameShape(QFrame::HLine);
    line->setObjectName(QStringLiteral("aboutLine"));
    cardLayout->addWidget(line);

    // 信息条目
    auto addRow = [this](const QString& icon, QLabel** label) {
        auto* rowLay = new QHBoxLayout();
        rowLay->setSpacing(8);
        auto* iconLabel = new QLabel(icon, m_infoCard);
        iconLabel->setStyleSheet(QStringLiteral("font-size: 15px; background: transparent;"));
        *label = new QLabel(m_infoCard);
        (*label)->setObjectName(QStringLiteral("aboutEntry"));
        (*label)->setTextFormat(Qt::RichText);
        rowLay->addWidget(iconLabel);
        rowLay->addWidget(*label);
        rowLay->addStretch();
        m_infoCard->layout()->addItem(rowLay);
    };
    addRow(QStringLiteral("👤"), &m_authorLabel);
    addRow(QStringLiteral("🌐"), &m_websiteLabel);
    addRow(QStringLiteral("💬"), &m_groupLabel);
    addRow(QStringLiteral("🧩"), &m_envLabel);

    mainLayout->addWidget(m_infoCard);

    // ---- 环境说明 ----
    m_envNoteLabel = new QLabel(this);
    m_envNoteLabel->setObjectName(QStringLiteral("aboutNote"));
    m_envNoteLabel->setWordWrap(true);
    mainLayout->addWidget(m_envNoteLabel);

    mainLayout->addStretch();

    // ---- 版权页脚 ----
    m_copyrightLabel = new QLabel(this);
    m_copyrightLabel->setObjectName(QStringLiteral("aboutCopyright"));
    m_copyrightLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_copyrightLabel);

    // 填充动态内容
    m_versionBadge->setText(QStringLiteral("版本 %1").arg(ctx.version()));
    m_authorLabel->setText(QStringLiteral("<b>作者:</b> bilibili 燃烧EFCH (Github RSEFCH123)"));
    m_websiteLabel->setText(QStringLiteral("<b>官方网站:</b> rsefch123.github.io/efchsword"));
    m_groupLabel->setText(QStringLiteral("<b>官方群聊:</b> 1050281660"));
    m_envLabel->setText(QStringLiteral("<b>运行环境:</b> %1  NTVersion: %2.%3  Build: %4")
        .arg(ctx.winName()).arg(ctx.winMajor()).arg(ctx.winMinor()).arg(ctx.winBuild()));
    m_envNoteLabel->setText(QStringLiteral(
        "本软件面向系统运维与安全研究场景，请勿用于任何违反法律法规的用途。"
        "使用本软件产生的一切后果由使用者自行承担。"));
    m_copyrightLabel->setText(QStringLiteral(
        "Copyright © 2024-2026 EFCH. All rights reserved."));

    applyTheme();
}

void AboutPage::applyTheme()
{
    const bool dark = ThemeManager::instance().isDark();
    const QString text = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#1e1e1e");
    const QString subText = dark ? QStringLiteral("#9d9d9d") : QStringLiteral("#5a5a5a");
    const QString cardBg = dark ? QStringLiteral("#252526") : QStringLiteral("#ffffff");
    const QString cardBorder = dark ? QStringLiteral("#3d3d40") : QStringLiteral("#dcdcdc");

    setStyleSheet(QStringLiteral(
        "#aboutCard { background: %1; border: 1px solid %2; border-radius: 10px; }"
        "#aboutSlogan { color: %3; font-size: 13px; background: transparent; }"
        "#aboutLine { border: none; border-top: 1px solid %2; background: transparent; max-height: 1px; }"
        "#aboutEntry { color: %4; font-size: 13px; background: transparent; }"
        "#aboutNote { color: %3; font-size: 12px; }"
        "#aboutCopyright { color: %3; font-size: 11px; }")
        .arg(cardBg, cardBorder, subText, text));

    m_versionBadge->setStyleSheet(QStringLiteral(
        "background: rgba(0,122,204,0.15); color: %1;"
        "border: 1px solid rgba(0,122,204,0.55); border-radius: 10px;"
        "padding: 3px 14px; font-size: 12px; font-weight: 600;")
        .arg(dark ? QStringLiteral("#3794ff") : QStringLiteral("#007acc")));
}
