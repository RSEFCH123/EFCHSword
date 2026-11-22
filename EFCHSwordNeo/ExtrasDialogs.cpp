#include "ExtrasDialogs.h"
#include "AppContext.h"

#include <QBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTimer>
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QKeyEvent>
#include <cmath>
#include <windows.h>
#include <ctime>

// ============================================================================
// 文件粉碎器
// ============================================================================
FileShredderDialog::FileShredderDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("文件粉碎器"));
    setMinimumWidth(520);
    auto* layout = new QFormLayout(this);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(QStringLiteral("文件或文件夹路径"));
    auto* row = new QHBoxLayout();
    row->addWidget(m_pathEdit, 1);
    auto* browse = new QPushButton(QStringLiteral("浏览..."), this);
    row->addWidget(browse);
    layout->addRow(QStringLiteral("目标:"), row);

    m_passSpin = new QSpinBox(this);
    m_passSpin->setRange(1, 16);
    m_passSpin->setValue(3);
    layout->addRow(QStringLiteral("覆写次数:"), m_passSpin);

    m_progress = new QProgressBar(this);
    layout->addRow(m_progress);
    m_statusLabel = new QLabel(QStringLiteral("将用随机数据多次覆写后改名并删除；失败项自动改用驱动强删。"), this);
    m_statusLabel->setWordWrap(true);
    layout->addRow(m_statusLabel);

    auto* btnRow = new QHBoxLayout();
    m_shredBtn = new QPushButton(QStringLiteral("粉碎 (不可恢复!)"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnRow->addStretch();
    btnRow->addWidget(m_shredBtn);
    btnRow->addWidget(closeBtn);
    layout->addRow(btnRow);

    connect(browse, &QPushButton::clicked, this, &FileShredderDialog::onBrowse);
    connect(m_shredBtn, &QPushButton::clicked, this, &FileShredderDialog::onShred);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void FileShredderDialog::onBrowse()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要粉碎的文件"), QString());
    if (!path.isEmpty()) m_pathEdit->setText(path);
}

void FileShredderDialog::collectFiles(const QString& path, QStringList& out)
{
    QFileInfo fi(path);
    if (!fi.exists()) return;
    if (fi.isFile()) { out << path; return; }
    QDir dir(path);
    for (const auto& sub : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        collectFiles(sub.absoluteFilePath(), out);
    // 目录本身最后置空删除
    out << path;
}

bool FileShredderDialog::shredOneFile(const QString& path, int passes, QString& error)
{
    QFile f(path);
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadWrite)) {
        error = QStringLiteral("无法打开文件");
        return false;
    }
    const qint64 size = f.size();
    for (int p = 0; p < passes; ++p) {
        f.seek(0);
        QByteArray buf(1024 * 1024, Qt::Uninitialized);
        if (p % 2 == 0) {
            for (int i = 0; i < buf.size(); ++i) buf[i] = char(QRandomGenerator::global()->generate() & 0xFF);
        } else {
            buf.fill(char(0x00));
        }
        qint64 written = 0;
        while (written < size) {
            const qint64 chunk = qMin<qint64>(buf.size(), size - written);
            if (f.write(buf.constData(), chunk) != chunk) {
                error = QStringLiteral("写入失败");
                f.close();
                return false;
            }
            written += chunk;
        }
        f.flush();
        QApplication::processEvents();
    }
    f.close();
    return true;
}

void FileShredderDialog::onShred()
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("粉碎"), QStringLiteral("请先选择目标。"));
        return;
    }
    const auto ret = QMessageBox::warning(this, QStringLiteral("文件粉碎器"),
        QStringLiteral("目标将被多次覆写后删除，完全不可恢复:\n%1\n\n确定继续?").arg(path),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QStringList files;
    collectFiles(path, files);
    if (files.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("粉碎"), QStringLiteral("目标不存在。"));
        return;
    }
    const int passes = m_passSpin->value();
    m_progress->setRange(0, files.size());
    m_shredBtn->setEnabled(false);
    int okCount = 0, failCount = 0;
    auto& ctx = AppContext::instance();

    for (int i = files.size() - 1; i >= 0; --i) {
        const QString& f = files.at(i);
        m_statusLabel->setText(QStringLiteral("正在粉碎 (%1/%2): %3").arg(files.size() - i).arg(files.size()).arg(f));
        QApplication::processEvents();
        QFileInfo fi(f);
        if (fi.isDir()) {
            if (QDir().rmdir(f) || QDir(f).removeRecursively()) ++okCount; else ++failCount;
            m_progress->setValue(files.size() - i);
            continue;
        }
        QString error;
        bool ok = shredOneFile(f, passes, error);
        // 随机改名再删除
        if (ok) {
            const QString newName = fi.absolutePath() + QStringLiteral("/~EFCH%1.tmp").arg(QRandomGenerator::global()->generate());
            ok = QFile::rename(f, newName) && QFile::remove(newName);
        }
        if (!ok && ctx.driverLoaded()) {
            ok = ctx.driver().forceDeleteFile(f);
        }
        if (ok) ++okCount; else ++failCount;
        m_progress->setValue(files.size() - i);
        QApplication::processEvents();
    }
    m_shredBtn->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("完成: 成功 %1, 失败 %2").arg(okCount).arg(failCount));
    QMessageBox::information(this, QStringLiteral("粉碎"), QStringLiteral("粉碎完成: 成功 %1 项, 失败 %2 项。").arg(okCount).arg(failCount));
}

// ============================================================================
// 烟花彩蛋
// ============================================================================
FireworksDialog::FireworksDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("🎆 EFCHSword 烟花 (点击发射, Esc 退出)"));
    setMinimumSize(800, 560);
    setAttribute(Qt::WA_DeleteOnClose);
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &FireworksDialog::onTick);
    m_timer->start();
}

void FireworksDialog::launchAt(double x, double y)
{
    const int hue = QRandomGenerator::global()->bounded(360);
    for (int i = 0; i < 90; ++i) {
        Particle p;
        p.x = x; p.y = y;
        const double ang = double(QRandomGenerator::global()->generate()) / double(ULONG_MAX) * 6.28318;
        const double spd = 2.0 + double(QRandomGenerator::global()->bounded(70)) / 12.0;
        p.vx = cos(ang) * spd;
        p.vy = sin(ang) * spd;
        p.life = p.maxLife = 50 + QRandomGenerator::global()->bounded(35);
        p.color = QColor::fromHsv(hue + QRandomGenerator::global()->bounded(30), 220, 255);
        m_particles.append(p);
    }
}

void FireworksDialog::onTick()
{
    ++m_tick;
    // 自动随机发射
    if (m_tick % 45 == 0) {
        launchAt(double(QRandomGenerator::global()->bounded(width() / 5, width() * 4 / 5)),
                 double(QRandomGenerator::global()->bounded(height() / 5, height() / 2)));
    }
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle& p = m_particles[i];
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.10;             // 重力
        p.vx *= 0.985;
        p.vy *= 0.985;
        if (--p.life <= 0) m_particles.removeAt(i);
    }
    if (m_particles.size() > 6000) m_particles.remove(0, m_particles.size() - 6000);
    update();
}

void FireworksDialog::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(6, 8, 20));
    painter.setRenderHint(QPainter::Antialiasing);
    for (const Particle& p : m_particles) {
        const double fade = double(p.life) / double(p.maxLife);
        QColor c = p.color;
        c.setAlpha(int(255 * fade));
        painter.setPen(Qt::NoPen);
        painter.setBrush(c);
        painter.drawEllipse(QPointF(p.x, p.y), 2.2 * fade + 0.6, 2.2 * fade + 0.6);
    }
    painter.setPen(QColor(200, 210, 240, 130));
    painter.drawText(12, height() - 12, QStringLiteral("EFCHSword Neo %1 - 烟花彩蛋").arg(AppContext::instance().version()));
}

void FireworksDialog::mousePressEvent(QMouseEvent* e)
{
    // 注意：不能把 QMouseEvent* 捕获进 singleShot 的 lambda——事件对象在槽
    // 返回后即被 Qt 销毁，连点时解引用悬垂指针会崩（曾表现为空指针崩溃）。
    // 只捕获坐标值。
    const QPointF pos = e->position();
    launchAt(pos.x(), pos.y());
    // 三连发
    QTimer::singleShot(120, this, [this, pos] { launchAt(pos.x() + 18, pos.y() - 10); });
    QTimer::singleShot(240, this, [this, pos] { launchAt(pos.x() - 18, pos.y() - 26); });
}

void FireworksDialog::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) close();
    QDialog::keyPressEvent(e);
}
