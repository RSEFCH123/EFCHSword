#include "AboutPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>

AboutPage::AboutPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

AboutPage::~AboutPage() = default;

void AboutPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(16);

    auto& ctx = AppContext::instance();

    // 标题
    auto* titleLabel = new QLabel(QStringLiteral("EFCHSword Neo"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* versionLabel = new QLabel(QStringLiteral("版本: %1").arg(ctx.version()), this);
    versionLabel->setAlignment(Qt::AlignCenter);
    QFont verFont = versionLabel->font();
    verFont.setPointSize(11);
    versionLabel->setFont(verFont);

    // 分隔线
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    // 详细信息
    auto* infoLabel = new QLabel(
        QStringLiteral(
            "<h3>EFCHSword Neo</h3>"
            "<p>计算机监视与管理软件 for Windows NT</p>"
            "<p>Qt 重构版本</p>"
            "<hr>"
            "<p><b>作者:</b> bilibili 燃烧EFCH (Github RSEFCH123)</p>"
            "<p><b>官方网站:</b> rsefch123.github.io/efchsword</p>"
            "<p><b>官方群聊:</b> 1051750501</p>"
            "<hr>"
            "<p><b>运行环境:</b> %1 NTVersion:%2.%3 Build:%4</p>")
            .arg(ctx.winName()).arg(ctx.winMajor()).arg(ctx.winMinor()).arg(ctx.winBuild()),
        this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);
    infoLabel->setTextFormat(Qt::RichText);

    mainLayout->addStretch();
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(versionLabel);
    mainLayout->addWidget(line);
    mainLayout->addWidget(infoLabel);
    mainLayout->addStretch();
}
