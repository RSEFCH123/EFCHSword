#include "DiskHexDialog.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QByteArray>
#include <QStyle>
#include <QFontDatabase>

#include <windows.h>

// 扇区大小（字节）
static constexpr int kSectorSize = 512;

// 格式化 Win32 错误码为可读字符串
static QString formatWin32Error(DWORD code)
{
    LPWSTR buf = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                               nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               (LPWSTR)&buf, 0, nullptr);
    QString msg;
    if (len > 0 && buf) {
        msg = QString::fromWCharArray(buf, (int)len).trimmed();
        LocalFree(buf);
    }
    if (msg.isEmpty()) {
        msg = QStringLiteral("未知错误");
    }
    return QStringLiteral("错误码 %1 (0x%2): %3").arg(code).arg(code, 8, 16, QChar('0')).arg(msg);
}

// 格式化驱动 IOCTL 错误（读取 KernelDriver::lastIoError）
static QString formatDriverError()
{
    auto& ctx = AppContext::instance();
    DWORD code = ctx.driver().lastIoError();
    return formatWin32Error(code);
}

// 字节集 <-> 十六进制字符串转换
static QByteArray hexToBytes(const QString& hex)
{
    QString cleaned = hex;
    cleaned.remove(' ').remove('\t').remove('\n').remove('\r');
    if (cleaned.size() % 2 != 0) return QByteArray();

    QByteArray result;
    result.reserve(cleaned.size() / 2);
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool ok = false;
        unsigned char b = cleaned.mid(i, 2).toUShort(&ok, 16);
        if (!ok) return QByteArray();
        result.append((char)b);
    }
    return result;
}

static QString bytesToHex(const QByteArray& bytes, int bytesPerLine = 16)
{
    QString result;
    result.reserve(bytes.size() * 3);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0 && i % bytesPerLine == 0) result += '\n';
        else if (i > 0) result += ' ';
        result += QString("%1").arg((unsigned char)bytes[i], 2, 16, QChar('0')).toUpper();
    }
    return result;
}

static QString bytesToAscii(const QByteArray& bytes)
{
    QString result;
    result.reserve(bytes.size());
    int bytesPerLine = 16;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0 && i % bytesPerLine == 0) result += '\n';
        unsigned char c = (unsigned char)bytes[i];
        // 可打印 ASCII (0x20-0x7E) 显示，其他显示为 .
        if (c >= 0x20 && c <= 0x7E) result += QChar(c);
        else result += '.';
    }
    return result;
}

DiskHexDialog::DiskHexDialog(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("DiskHex - 磁盘扇区编辑器"));
    resize(900, 600);

    setupUi();

    // 默认值
    m_startSectorEdit->setText(QStringLiteral("0"));
    m_sectorNumEdit->setText(QStringLiteral("1"));
    m_diskNumberEdit->setText(QStringLiteral("0"));
    m_raidPortEdit->setText(QStringLiteral("-1"));

    // 驱动未加载时禁用 IRP/Storport
    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        m_irpRadio->setEnabled(false);
        m_storRadio->setEnabled(false);
        m_ring3Radio->setChecked(true);
    } else {
        m_ring3Radio->setChecked(true);
    }
    onModeChanged();
}

DiskHexDialog::~DiskHexDialog() = default;

void DiskHexDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // ===== 模式选择 =====
    auto* modeGroup = new QGroupBox(QStringLiteral("读写模式"), this);
    auto* modeLayout = new QHBoxLayout(modeGroup);

    m_ring3Radio = new QRadioButton(QStringLiteral("Ring3"), modeGroup);
    m_irpRadio = new QRadioButton(QStringLiteral("IRP"), modeGroup);
    m_storRadio = new QRadioButton(QStringLiteral("Storport"), modeGroup);

    modeLayout->addWidget(m_ring3Radio);
    modeLayout->addWidget(m_irpRadio);
    modeLayout->addWidget(m_storRadio);
    modeLayout->addStretch();

    connect(m_ring3Radio, &QRadioButton::toggled, this, &DiskHexDialog::onModeChanged);
    connect(m_irpRadio, &QRadioButton::toggled, this, &DiskHexDialog::onModeChanged);
    connect(m_storRadio, &QRadioButton::toggled, this, &DiskHexDialog::onModeChanged);

    mainLayout->addWidget(modeGroup);

    // ===== 参数 =====
    auto* paramGroup = new QGroupBox(QStringLiteral("参数"), this);
    auto* paramLayout = new QGridLayout(paramGroup);

    paramLayout->addWidget(new QLabel(QStringLiteral("起始扇区 (StartSector):")), 0, 0);
    m_startSectorEdit = new QLineEdit(paramGroup);
    paramLayout->addWidget(m_startSectorEdit, 0, 1);

    paramLayout->addWidget(new QLabel(QStringLiteral("扇区数量 (SectorNumber):")), 0, 2);
    m_sectorNumEdit = new QLineEdit(paramGroup);
    paramLayout->addWidget(m_sectorNumEdit, 0, 3);

    // Ring3 物理磁盘号
    paramLayout->addWidget(new QLabel(QStringLiteral("物理磁盘号:")), 1, 0);
    m_diskNumberEdit = new QLineEdit(paramGroup);
    paramLayout->addWidget(m_diskNumberEdit, 1, 1);

    // Storport RaidPort
    paramLayout->addWidget(new QLabel(QStringLiteral("RaidPort (-1=系统盘):")), 1, 2);
    m_raidPortEdit = new QLineEdit(paramGroup);
    paramLayout->addWidget(m_raidPortEdit, 1, 3);

    mainLayout->addWidget(paramGroup);

    // ===== 数据编辑 =====
    auto* dataGroup = new QGroupBox(QStringLiteral("数据 (十六进制)"), this);
    auto* dataLayout = new QVBoxLayout(dataGroup);

    // 使用等宽字体
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_hexEdit = new QPlainTextEdit(dataGroup);
    m_hexEdit->setFont(monoFont);
    m_hexEdit->setPlaceholderText(QStringLiteral("十六进制数据，例如: EB 52 90 4E 54 46 53 ..."));
    dataLayout->addWidget(m_hexEdit);

    auto* asciiLabel = new QLabel(QStringLiteral("ASCII 预览:"), dataGroup);
    dataLayout->addWidget(asciiLabel);

    m_asciiEdit = new QPlainTextEdit(dataGroup);
    m_asciiEdit->setFont(monoFont);
    m_asciiEdit->setReadOnly(true);
    m_asciiEdit->setMaximumHeight(100);
    dataLayout->addWidget(m_asciiEdit);

    connect(m_hexEdit, &QPlainTextEdit::textChanged, this, &DiskHexDialog::onHexChanged);

    mainLayout->addWidget(dataGroup, 1);

    // ===== 按钮栏 =====
    auto* btnLayout = new QHBoxLayout();

    auto* readBtn = new QPushButton(QStringLiteral("读取扇区"), this);
    auto* writeBtn = new QPushButton(QStringLiteral("写入扇区"), this);
    auto* writeFileBtn = new QPushButton(QStringLiteral("从文件写入"), this);
    auto* createBlankBtn = new QPushButton(QStringLiteral("创建空文件"), this);

    btnLayout->addWidget(readBtn);
    btnLayout->addWidget(writeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(writeFileBtn);
    btnLayout->addWidget(createBlankBtn);

    mainLayout->addLayout(btnLayout);

    // ===== 状态栏 =====
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    mainLayout->addWidget(m_statusLabel);

    connect(readBtn, &QPushButton::clicked, this, &DiskHexDialog::onReadClicked);
    connect(writeBtn, &QPushButton::clicked, this, &DiskHexDialog::onWriteClicked);
    connect(writeFileBtn, &QPushButton::clicked, this, &DiskHexDialog::onWriteFileClicked);
    connect(createBlankBtn, &QPushButton::clicked, this, &DiskHexDialog::onCreateBlankClicked);
}

void DiskHexDialog::onModeChanged()
{
    // 根据模式启用/禁用 RaidPort 输入框
    bool isStor = m_storRadio->isChecked();
    m_raidPortEdit->setEnabled(isStor);

    // Ring3 模式启用磁盘号
    bool isRing3 = m_ring3Radio->isChecked();
    m_diskNumberEdit->setEnabled(isRing3);
}

void DiskHexDialog::onHexChanged()
{
    // 同步 ASCII 预览
    QString hex = m_hexEdit->toPlainText();
    QByteArray bytes = hexToBytes(hex);
    if (bytes.isEmpty() && !hex.trimmed().isEmpty()) {
        // 十六进制格式错误，不更新
        return;
    }
    m_asciiEdit->setPlainText(bytesToAscii(bytes));
}

void DiskHexDialog::onReadClicked()
{
    bool ok1 = false, ok2 = false;
    quint64 ss = m_startSectorEdit->text().toULongLong(&ok1);
    quint64 sn = m_sectorNumEdit->text().toULongLong(&ok2);
    if (!ok1 || !ok2 || sn == 0) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("参数无效"));
        return;
    }

    QByteArray data;
    bool ok = false;
    QString errMsg;

    if (m_ring3Radio->isChecked()) {
        // Ring3: ReadFile
        bool okDn = false;
        int diskNum = m_diskNumberEdit->text().toInt(&okDn);
        if (!okDn || diskNum < 0) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的磁盘号"));
            return;
        }

        // 打开物理磁盘 \\.\PhysicalDriveN
        wchar_t path[64] = {0};
        swprintf_s(path, L"\\\\.\\PhysicalDrive%d", diskNum);
        HANDLE hDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDisk == INVALID_HANDLE_VALUE) {
            errMsg = QStringLiteral("CreateFile 失败: %1").arg(GetLastError());
            QMessageBox::warning(this, QStringLiteral("DiskHex"), errMsg);
            return;
        }

        // 定位到起始扇区
        LARGE_INTEGER offset;
        offset.QuadPart = (ULONGLONG)ss * kSectorSize;
        LARGE_INTEGER newPos = {0};
        if (!SetFilePointerEx(hDisk, offset, &newPos, FILE_BEGIN)) {
            errMsg = QStringLiteral("SetFilePointerEx 失败: %1").arg(GetLastError());
            CloseHandle(hDisk);
            QMessageBox::warning(this, QStringLiteral("DiskHex"), errMsg);
            return;
        }

        // 读取数据
        DWORD totalToRead = (DWORD)(sn * kSectorSize);
        data.resize(totalToRead);
        DWORD totalRead = 0;
        while (totalRead < totalToRead) {
            DWORD bytesRead = 0;
            BOOL r = ReadFile(hDisk, data.data() + totalRead, totalToRead - totalRead, &bytesRead, nullptr);
            if (!r || bytesRead == 0) break;
            totalRead += bytesRead;
        }
        CloseHandle(hDisk);

        if (totalRead != totalToRead) {
            data.resize(totalRead);
            errMsg = QStringLiteral("读取不完整: 请求 %1 字节，实际 %2 字节")
                         .arg(totalToRead).arg(totalRead);
        }
        ok = (totalRead > 0);
    } else if (m_irpRadio->isChecked()) {
        // IRP: IOCTL_IRPREADDISK
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("驱动未加载"));
            return;
        }
        ok = ctx.driver().irpReadDisk(ss, sn, data);
        if (!ok) errMsg = QStringLiteral("IRP 读取失败 - ") + formatDriverError();
    } else if (m_storRadio->isChecked()) {
        // Storport: IOCTL_STORDIRECT_READDISK
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("驱动未加载"));
            return;
        }

        bool okPort = false;
        qint16 raidPort = (qint16)m_raidPortEdit->text().toShort(&okPort);
        if (!okPort) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的 RaidPort"));
            return;
        }
        ok = ctx.driver().storDirectReadDisk(ss, sn, raidPort, data);
        if (!ok) errMsg = QStringLiteral("Storport 读取失败 - ") + formatDriverError();
    }

    if (ok) {
        m_currentData = data;
        m_currentStartSector = ss;
        m_currentSectorNumber = sn;
        m_hexEdit->setPlainText(bytesToHex(data));
        m_statusLabel->setText(QStringLiteral("读取成功: %1 字节 (起始扇区 %2, 扇区数 %3)")
                                   .arg(data.size()).arg(ss).arg(sn));
    } else {
        m_statusLabel->setText(QStringLiteral("读取失败: %1").arg(errMsg));
        QMessageBox::warning(this, QStringLiteral("DiskHex"),
            QStringLiteral("读取失败: %1").arg(errMsg));
    }
}

void DiskHexDialog::onWriteClicked()
{
    bool ok1 = false, ok2 = false;
    quint64 ss = m_startSectorEdit->text().toULongLong(&ok1);
    quint64 sn = m_sectorNumEdit->text().toULongLong(&ok2);
    if (!ok1 || !ok2) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("参数无效"));
        return;
    }

    // 从编辑框获取十六进制数据
    QString hex = m_hexEdit->toPlainText();
    QByteArray data = hexToBytes(hex);
    if (data.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的十六进制数据"));
        return;
    }

    // 检查数据大小是否与扇区数匹配
    qint64 expectedSize = (qint64)sn * kSectorSize;
    if (data.size() != expectedSize) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"),
            QStringLiteral("数据大小不匹配: 当前 %1 字节，需要 %2 字节 (扇区数 %3 × 512)")
                .arg(data.size()).arg(expectedSize).arg(sn));
        return;
    }

    // MBR 警告
    if (ss == 0) {
        auto ret = QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("你操作的对象是 MBR，修改后可能导致系统无法启动!确定写入?"),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    // 二次确认
    auto ret = QMessageBox::warning(this, QStringLiteral("警告"),
        QStringLiteral("确定写入磁盘数据? (起始扇区: %1, 扇区数: %2)").arg(ss).arg(sn),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    bool ok = false;
    QString errMsg;

    if (m_ring3Radio->isChecked()) {
        // Ring3: WriteFile
        bool okDn = false;
        int diskNum = m_diskNumberEdit->text().toInt(&okDn);
        if (!okDn || diskNum < 0) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的磁盘号"));
            return;
        }

        wchar_t path[64] = {0};
        swprintf_s(path, L"\\\\.\\PhysicalDrive%d", diskNum);
        HANDLE hDisk = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDisk == INVALID_HANDLE_VALUE) {
            errMsg = QStringLiteral("CreateFile 失败: %1").arg(GetLastError());
            QMessageBox::warning(this, QStringLiteral("DiskHex"), errMsg);
            return;
        }

        LARGE_INTEGER offset;
        offset.QuadPart = (ULONGLONG)ss * kSectorSize;
        LARGE_INTEGER newPos = {0};
        if (!SetFilePointerEx(hDisk, offset, &newPos, FILE_BEGIN)) {
            errMsg = QStringLiteral("SetFilePointerEx 失败: %1").arg(GetLastError());
            CloseHandle(hDisk);
            QMessageBox::warning(this, QStringLiteral("DiskHex"), errMsg);
            return;
        }

        DWORD totalToWrite = (DWORD)data.size();
        DWORD totalWritten = 0;
        while (totalWritten < totalToWrite) {
            DWORD bytesWritten = 0;
            BOOL w = WriteFile(hDisk, data.constData() + totalWritten,
                               totalToWrite - totalWritten, &bytesWritten, nullptr);
            if (!w || bytesWritten == 0) break;
            totalWritten += bytesWritten;
        }
        CloseHandle(hDisk);

        ok = (totalWritten == totalToWrite);
        if (!ok) errMsg = QStringLiteral("写入不完整: %1/%2 字节").arg(totalWritten).arg(totalToWrite);
    } else if (m_irpRadio->isChecked()) {
        // IRP: IOCTL_FORCEWRITEDISK
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("驱动未加载"));
            return;
        }
        ok = ctx.driver().forceWriteDisk(ss, sn, data);
        if (!ok) errMsg = QStringLiteral("IRP 写入失败 - ") + formatDriverError();
    } else if (m_storRadio->isChecked()) {
        // Storport: IOCTL_STORDIRECT_WRITEDISK
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("驱动未加载"));
            return;
        }

        bool okPort = false;
        qint16 raidPort = (qint16)m_raidPortEdit->text().toShort(&okPort);
        if (!okPort) {
            QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的 RaidPort"));
            return;
        }
        ok = ctx.driver().storDirectWriteDisk(ss, sn, raidPort, data);
        if (!ok) errMsg = QStringLiteral("Storport 写入失败");
    }

    if (ok) {
        m_statusLabel->setText(QStringLiteral("写入成功: %1 字节").arg(data.size()));
        QMessageBox::information(this, QStringLiteral("DiskHex"), QStringLiteral("Success!"));
    } else {
        m_statusLabel->setText(QStringLiteral("写入失败: %1").arg(errMsg));
        QMessageBox::warning(this, QStringLiteral("DiskHex"),
            QStringLiteral("写入失败: %1").arg(errMsg));
    }
}

void DiskHexDialog::onWriteFileClicked()
{
    bool ok1 = false, ok2 = false;
    quint64 ss = m_startSectorEdit->text().toULongLong(&ok1);
    quint64 sn = m_sectorNumEdit->text().toULongLong(&ok2);
    if (!ok1 || !ok2) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("参数无效"));
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择要写入磁盘的文件"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无法读取文件"));
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("文件内容为空"));
        return;
    }

    qint64 expectedSize = (qint64)sn * kSectorSize;
    if (fileData.size() != expectedSize) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"),
            QStringLiteral("文件大小(%1)与扇区数×512(%2)不匹配")
                .arg(fileData.size()).arg(expectedSize));
        return;
    }

    // 二次确认（含文件名）
    auto ret = QMessageBox::warning(this, QStringLiteral("警告"),
        QStringLiteral("确定将 %1 写入磁盘数据? (起始扇区: %2, 扇区数: %3)")
            .arg(QFileInfo(filePath).fileName()).arg(ss).arg(sn),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    // MBR 警告
    if (ss == 0) {
        auto ret2 = QMessageBox::warning(this, QStringLiteral("警告"),
            QStringLiteral("你操作的对象是 MBR，修改后可能导致系统无法启动!确定写入?"),
            QMessageBox::Yes | QMessageBox::No);
        if (ret2 != QMessageBox::Yes) return;
    }

    // 更新编辑框
    m_hexEdit->setPlainText(bytesToHex(fileData));
    m_currentData = fileData;
    m_currentStartSector = ss;
    m_currentSectorNumber = sn;

    // 调用写入逻辑
    onWriteClicked();
}

void DiskHexDialog::onCreateBlankClicked()
{
    bool ok = false;
    QString sizeStr = QInputDialog::getText(this, QStringLiteral("创建空文件"),
        QStringLiteral("文件大小 (Byte):"), QLineEdit::Normal, QStringLiteral("512"), &ok);
    if (!ok || sizeStr.isEmpty()) return;

    bool okSize = false;
    int size = sizeStr.toInt(&okSize);
    if (!okSize || size <= 0) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无效的大小"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("保存空文件"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("DiskHex"), QStringLiteral("无法创建文件"));
        return;
    }
    file.write(QByteArray(size, '\0'));
    file.close();

    QMessageBox::information(this, QStringLiteral("DiskHex"), QStringLiteral("Success!"));
}
