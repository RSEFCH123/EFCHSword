#include "FileReaderDialog.h"
#include "AppContext.h"

#include <QPlainTextEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>

FileReaderDialog::FileReaderDialog(const QString& filePath, QWidget* parent, bool useDirectNtfs)
    : QDialog(parent), m_filePath(filePath), m_useDirectNtfs(useDirectNtfs)
{
    setupUi();
    readFile();
}

FileReaderDialog::~FileReaderDialog() = default;

void FileReaderDialog::setupUi()
{
    auto& ctx = AppContext::instance();
    QString title = QStringLiteral("文件编辑: ") + m_filePath;
    if (ctx.driverLoaded()) {
        title = QStringLiteral("文件编辑 [IRP MODE]: ") + m_filePath;
    }
    setWindowTitle(title);
    resize(700, 500);

    auto* mainLayout = new QVBoxLayout(this);

    // 模式选择
    auto* modeGroup = new QGroupBox(QStringLiteral("显示模式"), this);
    auto* modeLayout = new QHBoxLayout(modeGroup);
    m_modeText = new QRadioButton(QStringLiteral("文本"), this);
    m_modeHex = new QRadioButton(QStringLiteral("Hex"), this);
    m_modeOctal = new QRadioButton(QStringLiteral("八进制"), this);
    m_modeText->setChecked(true);
    m_chkModifiedOnly = new QCheckBox(QStringLiteral("仅翻译修改内容"), this);
    modeLayout->addWidget(m_modeText);
    modeLayout->addWidget(m_modeHex);
    modeLayout->addWidget(m_modeOctal);
    modeLayout->addStretch();
    modeLayout->addWidget(m_chkModifiedOnly);
    mainLayout->addWidget(modeGroup);

    // 文本编辑区
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setFont(QFont(QStringLiteral("Consolas"), 9));
    mainLayout->addWidget(m_textEdit, 1);

    // 状态栏
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    // 按钮
    auto* btnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton(QStringLiteral("保存"), this);
    m_closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_modeText, &QRadioButton::toggled, this, &FileReaderDialog::onViewModeChanged);
    connect(m_modeHex, &QRadioButton::toggled, this, &FileReaderDialog::onViewModeChanged);
    connect(m_modeOctal, &QRadioButton::toggled, this, &FileReaderDialog::onViewModeChanged);
    connect(m_saveBtn, &QPushButton::clicked, this, &FileReaderDialog::onSave);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void FileReaderDialog::readFile()
{
    m_statusLabel->setText(QStringLiteral("正在读取..."));

    // DirectNTFS 模式下不需要文件存在检查 (文件可能被隐藏, QFileInfo 看不到)
    if (!m_useDirectNtfs) {
        QFileInfo fi(m_filePath);
        if (!fi.exists()) {
            m_statusLabel->setText(QStringLiteral("错误! 文件不存在!"));
            return;
        }
    }

    auto& ctx = AppContext::instance();
    bool ok = false;
    if (ctx.driverLoaded()) {
        if (m_useDirectNtfs) {
            // DirectNTFS 模式: 直接解析 NTFS 读取文件内容
            ok = ctx.driver().ntfsReadFile(m_filePath, m_fileData);
            if (!ok) {
                m_statusLabel->setText(QStringLiteral("错误! NtfsReadFile 失败!"));
            }
        } else {
            ok = ctx.driver().irpReadFile(m_filePath, m_fileData);
            if (!ok) {
                m_statusLabel->setText(QStringLiteral("错误! IrpReadFile 失败!"));
            }
        }
    }
    // DirectNTFS 模式下不回退到 QFile (因为目的就是绕过文件系统)
    if (!m_useDirectNtfs && (!ok || m_fileData.isEmpty())) {
        QFile f(m_filePath);
        if (f.open(QIODevice::ReadOnly)) {
            m_fileData = f.readAll();
            f.close();
            ok = !m_fileData.isEmpty();
        }
    }

    if (!ok) {
        m_statusLabel->setText(QStringLiteral("错误! 读取文件失败!"));
        return;
    }

    // 默认文本模式显示
    m_textEdit->setPlainText(QString::fromLatin1(m_fileData));
    m_statusLabel->setText(QStringLiteral("完成! 大小: %1 字节").arg(m_fileData.size()));
}

void FileReaderDialog::onViewModeChanged()
{
    m_statusLabel->setText(QStringLiteral("正在翻译..."));
    QApplication::processEvents();

    QByteArray sourceData = m_fileData;
    if (m_chkModifiedOnly->isChecked()) {
        // 使用编辑框中的当前内容作为源
        sourceData = m_textEdit->toPlainText().toLatin1();
    }

    if (m_modeText->isChecked()) {
        m_textEdit->setPlainText(QString::fromLatin1(sourceData));
    } else if (m_modeHex->isChecked()) {
        // Hex 格式: 每行 16 字节，显示 hex + ASCII
        QString hexText;
        for (int i = 0; i < sourceData.size(); i += 16) {
            QString hexPart;
            QString asciiPart;
            for (int j = 0; j < 16; ++j) {
                if (i + j < sourceData.size()) {
                    uchar b = (uchar)sourceData[i + j];
                    hexPart += QString::asprintf("%02X ", b);
                    asciiPart += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
                } else {
                    hexPart += "   ";
                }
            }
            hexText += QString::asprintf("%08X  ", i) + hexPart + " " + asciiPart + "\n";
        }
        m_textEdit->setPlainText(hexText);
    } else if (m_modeOctal->isChecked()) {
        // 八进制格式
        QString octText;
        for (int i = 0; i < sourceData.size(); ++i) {
            octText += QString::asprintf("%03o ", (uchar)sourceData[i]);
            if ((i + 1) % 16 == 0) octText += "\n";
        }
        m_textEdit->setPlainText(octText);
    }

    m_statusLabel->setText(QStringLiteral("完成!"));
}

void FileReaderDialog::onSave()
{
    QByteArray data;
    if (m_modeText->isChecked()) {
        data = m_textEdit->toPlainText().toLatin1();
    } else {
        QMessageBox::information(this, QStringLiteral("EFCHSword"),
            QStringLiteral("仅文本模式下可保存"));
        return;
    }

    auto& ctx = AppContext::instance();
    bool ok = false;
    if (ctx.driverLoaded()) {
        ok = ctx.driver().forceWriteFile(m_filePath, data);
    }
    if (!ok) {
        QFile f(m_filePath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
            f.close();
            ok = true;
        }
    }

    if (ok) {
        m_fileData = data;
        m_statusLabel->setText(QStringLiteral("保存成功!"));
    } else {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("保存失败!"));
    }
}
