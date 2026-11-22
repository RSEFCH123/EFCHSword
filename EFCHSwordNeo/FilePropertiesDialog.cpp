#include "FilePropertiesDialog.h"
#include "AppContext.h"

#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QDesktopServices>

#include <windows.h>
#include <winver.h>

#pragma comment(lib, "version.lib")

FilePropertiesDialog::FilePropertiesDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent), m_filePath(filePath)
{
    setupUi();
    loadFileInfo();
}

FilePropertiesDialog::~FilePropertiesDialog() = default;

void FilePropertiesDialog::setupUi()
{
    setWindowTitle(m_filePath + QStringLiteral(" 的属性"));
    setMinimumWidth(480);

    auto* mainLayout = new QVBoxLayout(this);

    // 基本信息
    auto* infoGroup = new QGroupBox(QStringLiteral("基本信息"), this);
    auto* infoLayout = new QGridLayout(infoGroup);

    m_filePathLabel = new QLabel(this);
    m_fileDescLabel = new QLabel(this);
    m_fileVersionLabel = new QLabel(this);
    m_fileCopyrightLabel = new QLabel;
    m_fileSizeLabel = new QLabel(this);
    m_fileTypeLabel = new QLabel(this);

    infoLayout->addWidget(new QLabel(QStringLiteral("路径:"), this), 0, 0);
    infoLayout->addWidget(m_filePathLabel, 0, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("文件描述:"), this), 1, 0);
    infoLayout->addWidget(m_fileDescLabel, 1, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("文件版本:"), this), 2, 0);
    infoLayout->addWidget(m_fileVersionLabel, 2, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("版权:"), this), 3, 0);
    infoLayout->addWidget(m_fileCopyrightLabel, 3, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("文件大小:"), this), 4, 0);
    infoLayout->addWidget(m_fileSizeLabel, 4, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("文件类型:"), this), 5, 0);
    infoLayout->addWidget(m_fileTypeLabel, 5, 1);
    infoLayout->setColumnStretch(1, 1);

    mainLayout->addWidget(infoGroup);

    // 哈希值
    auto* hashGroup = new QGroupBox(QStringLiteral("哈希值"), this);
    auto* hashLayout = new QGridLayout(hashGroup);

    m_md5Edit = new QLineEdit(this);
    m_md5Edit->setReadOnly(true);
    m_sha1Edit = new QLineEdit(this);
    m_sha1Edit->setReadOnly(true);
    m_sha256Edit = new QLineEdit(this);
    m_sha256Edit->setReadOnly(true);

    hashLayout->addWidget(new QLabel(QStringLiteral("MD5:"), this), 0, 0);
    hashLayout->addWidget(m_md5Edit, 0, 1);
    hashLayout->addWidget(new QLabel(QStringLiteral("SHA1:"), this), 1, 0);
    hashLayout->addWidget(m_sha1Edit, 1, 1);
    hashLayout->addWidget(new QLabel(QStringLiteral("SHA256:"), this), 2, 0);
    hashLayout->addWidget(m_sha256Edit, 2, 1);
    hashLayout->setColumnStretch(1, 1);

    mainLayout->addWidget(hashGroup);

    // 文件属性
    auto* attrGroup = new QGroupBox(QStringLiteral("文件属性"), this);
    auto* attrLayout = new QGridLayout(attrGroup);

    m_chkReadOnly = new QCheckBox(QStringLiteral("只读"), this);
    m_chkHidden = new QCheckBox(QStringLiteral("隐藏"), this);
    m_chkSystem = new QCheckBox(QStringLiteral("系统"), this);
    m_chkArchive = new QCheckBox(QStringLiteral("存档"), this);
    m_chkTemporary = new QCheckBox(QStringLiteral("临时"), this);
    m_chkCompressed = new QCheckBox(QStringLiteral("压缩"), this);
    m_chkEncrypted = new QCheckBox(QStringLiteral("加密"), this);

    attrLayout->addWidget(m_chkReadOnly, 0, 0);
    attrLayout->addWidget(m_chkHidden, 0, 1);
    attrLayout->addWidget(m_chkSystem, 0, 2);
    attrLayout->addWidget(m_chkArchive, 1, 0);
    attrLayout->addWidget(m_chkTemporary, 1, 1);
    attrLayout->addWidget(m_chkCompressed, 1, 2);
    attrLayout->addWidget(m_chkEncrypted, 2, 0);

    mainLayout->addWidget(attrGroup);

    // 按钮
    auto* btnLayout = new QHBoxLayout();
    m_locateBtn = new QPushButton(QStringLiteral("定位文件"), this);
    m_closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_locateBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_locateBtn, &QPushButton::clicked, this, &FilePropertiesDialog::onLocateFile);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_chkReadOnly, &QCheckBox::toggled, this, &FilePropertiesDialog::onAttributeChanged);
    connect(m_chkHidden, &QCheckBox::toggled, this, &FilePropertiesDialog::onAttributeChanged);
    connect(m_chkSystem, &QCheckBox::toggled, this, &FilePropertiesDialog::onAttributeChanged);
    connect(m_chkArchive, &QCheckBox::toggled, this, &FilePropertiesDialog::onAttributeChanged);
    connect(m_chkTemporary, &QCheckBox::toggled, this, &FilePropertiesDialog::onAttributeChanged);
}

void FilePropertiesDialog::loadFileInfo()
{
    m_filePathLabel->setText(m_filePath);
    m_filePathLabel->setWordWrap(true);
    m_filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QFileInfo fi(m_filePath);
    if (!fi.exists()) {
        m_fileSizeLabel->setText(QStringLiteral("文件不存在"));
        m_initing = false;
        return;
    }

    // 文件大小
    auto& ctx = AppContext::instance();
    qint64 size = -1;
    if (ctx.driverLoaded()) {
        size = ctx.driver().irpGetFileSize(m_filePath);
    }
    if (size < 0) {
        size = fi.size();
    }
    m_fileSizeLabel->setText(QStringLiteral("%1 字节 (%2 KB)")
        .arg(size).arg(size / 1024));

    // 文件版本信息（从 Windows 版本资源读取）
    DWORD dummy = 0;
    DWORD verSize = GetFileVersionInfoSizeW(m_filePath.toStdWString().c_str(), &dummy);
    if (verSize > 0) {
        QByteArray verData(verSize, 0);
        if (GetFileVersionInfoW(m_filePath.toStdWString().c_str(), 0, verSize, verData.data())) {
            struct LANGANDCODEPAGE {
                WORD wLanguage;
                WORD wCodePage;
            } *lpTranslate = nullptr;
            UINT cbTranslate = 0;
            if (VerQueryValueW(verData.constData(), L"\\VarFileInfo\\Translation",
                               (LPVOID*)&lpTranslate, &cbTranslate) && cbTranslate >= sizeof(*lpTranslate)) {
                // 遍历所有语言，找到第一个非空的结果
                auto querySub = [&](const wchar_t* subBlock) -> QString {
                    for (UINT i = 0; i < cbTranslate / sizeof(*lpTranslate); ++i) {
                        QString path = QStringLiteral("\\StringFileInfo\\%1%2\\%3")
                            .arg(lpTranslate[i].wLanguage, 4, 16, QChar('0'))
                            .arg(lpTranslate[i].wCodePage, 4, 16, QChar('0'))
                            .arg(QString::fromWCharArray(subBlock));
                        WCHAR* szBuf = nullptr;
                        UINT bufLen = 0;
                        if (VerQueryValueW(verData.constData(),
                                           path.toStdWString().c_str(),
                                           (LPVOID*)&szBuf, &bufLen) && bufLen > 0 && szBuf) {
                            // bufLen 包含 null 终止符的字符数
                            return QString::fromWCharArray(szBuf, bufLen > 0 ? bufLen - 1 : 0);
                        }
                    }
                    return QString();
                };
                m_fileDescLabel->setText(querySub(L"FileDescription"));
                m_fileVersionLabel->setText(querySub(L"FileVersion"));
                m_fileCopyrightLabel->setText(querySub(L"LegalCopyright"));
            }
        }
    }

    // 文件类型判断（PE 文件检测）
    QFile f(m_filePath);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray header = f.read(2);
        if (header.size() >= 2 && (uchar)header[0] == 'M' && (uchar)header[1] == 'Z') {
            // PE 文件，判断 32/64 位
            m_fileTypeLabel->setText(QStringLiteral("PE 可执行文件"));
        } else {
            m_fileTypeLabel->setText(fi.suffix().isEmpty()
                ? QStringLiteral("文件") : fi.suffix() + QStringLiteral(" 文件"));
        }
        f.close();
    }

    // 计算哈希值
    m_md5Edit->setText(QStringLiteral("计算中..."));
    m_sha1Edit->setText(QStringLiteral("计算中..."));
    m_sha256Edit->setText(QStringLiteral("计算中..."));

    // 读取文件数据（优先用驱动 IRP 读取）
    QByteArray fileData;
    if (ctx.driverLoaded()) {
        ctx.driver().irpReadFile(m_filePath, fileData);
    }
    if (fileData.isEmpty()) {
        if (f.open(QIODevice::ReadOnly)) {
            fileData = f.readAll();
            f.close();
        }
    }

    if (!fileData.isEmpty()) {
        m_md5Edit->setText(QString::fromLatin1(
            QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toHex()));
        m_sha1Edit->setText(QString::fromLatin1(
            QCryptographicHash::hash(fileData, QCryptographicHash::Sha1).toHex()));
        m_sha256Edit->setText(QString::fromLatin1(
            QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex()));
    } else {
        m_md5Edit->setText(QStringLiteral("读取失败"));
        m_sha1Edit->setText(QStringLiteral("读取失败"));
        m_sha256Edit->setText(QStringLiteral("读取失败"));
    }

    // 加载文件属性
    loadFileAttributes();
    m_initing = false;
}

void FilePropertiesDialog::loadFileAttributes()
{
    DWORD attrs = GetFileAttributesW(m_filePath.toStdWString().c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return;

    m_chkReadOnly->setChecked(attrs & FILE_ATTRIBUTE_READONLY);
    m_chkHidden->setChecked(attrs & FILE_ATTRIBUTE_HIDDEN);
    m_chkSystem->setChecked(attrs & FILE_ATTRIBUTE_SYSTEM);
    m_chkArchive->setChecked(attrs & FILE_ATTRIBUTE_ARCHIVE);
    m_chkTemporary->setChecked(attrs & FILE_ATTRIBUTE_TEMPORARY);
    m_chkCompressed->setChecked(attrs & FILE_ATTRIBUTE_COMPRESSED);
    m_chkEncrypted->setChecked(attrs & FILE_ATTRIBUTE_ENCRYPTED);
}

DWORD FilePropertiesDialog::getCurrentAttributes()
{
    DWORD attr = 0;
    if (m_chkReadOnly->isChecked()) attr |= FILE_ATTRIBUTE_READONLY;
    if (m_chkHidden->isChecked()) attr |= FILE_ATTRIBUTE_HIDDEN;
    if (m_chkSystem->isChecked()) attr |= FILE_ATTRIBUTE_SYSTEM;
    if (m_chkArchive->isChecked()) attr |= FILE_ATTRIBUTE_ARCHIVE;
    if (m_chkTemporary->isChecked()) attr |= FILE_ATTRIBUTE_TEMPORARY;
    return attr;
}

void FilePropertiesDialog::setFileAttribute(DWORD attr, bool add)
{
    if (m_initing) return;

    DWORD current = GetFileAttributesW(m_filePath.toStdWString().c_str());
    if (current == INVALID_FILE_ATTRIBUTES) return;

    DWORD newAttr;
    if (add) {
        newAttr = current | attr;
    } else {
        newAttr = current & ~attr;
    }

    auto& ctx = AppContext::instance();
    bool ok = false;
    if (ctx.driverLoaded()) {
        ok = ctx.driver().irpSetInformationFile(m_filePath, newAttr);
    }
    if (!ok) {
        ok = SetFileAttributesW(m_filePath.toStdWString().c_str(), newAttr);
    }
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("设置文件属性失败，错误码: %1").arg(GetLastError()));
    }
}

void FilePropertiesDialog::onAttributeChanged()
{
    // 直接根据当前复选框状态设置完整属性
    if (m_initing) return;

    DWORD newAttr = getCurrentAttributes();
    // 保留原有的一些不可修改属性（如 DIRECTORY）
    DWORD current = GetFileAttributesW(m_filePath.toStdWString().c_str());
    if (current != INVALID_FILE_ATTRIBUTES) {
        newAttr |= (current & FILE_ATTRIBUTE_DIRECTORY);
        newAttr |= (current & FILE_ATTRIBUTE_COMPRESSED);
        newAttr |= (current & FILE_ATTRIBUTE_ENCRYPTED);
    }

    auto& ctx = AppContext::instance();
    bool ok = false;
    if (ctx.driverLoaded()) {
        ok = ctx.driver().irpSetInformationFile(m_filePath, newAttr);
    }
    if (!ok) {
        ok = SetFileAttributesW(m_filePath.toStdWString().c_str(), newAttr);
    }
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("EFCHSword"),
            QStringLiteral("设置文件属性失败，错误码: %1").arg(GetLastError()));
    }
}

void FilePropertiesDialog::onLocateFile()
{
    QFileInfo fi(m_filePath);
    QString dir = fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
