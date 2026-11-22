#include "SystemThreadPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QBrush>
#include <memory>

// 线程表格列
enum ThreadColumn {
    TC_TID = 0,        // 线程 ID
    TC_ETHREAD,        // 内核 ETHREAD 地址
    TC_STARTADDR,      // 线程起始地址
    TC_DRIVER_PATH,    // 所属驱动
    TC_Count
};

SystemThreadPage::SystemThreadPage(QWidget* parent) : QWidget(parent)
{
    setupUi();
    setupContextMenu();
}

SystemThreadPage::~SystemThreadPage() = default;

void SystemThreadPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // 工具栏
    auto* toolLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton(QStringLiteral("刷新线程列表"), this);
    auto* hintLabel = new QLabel(
        QStringLiteral("通过内核 CID 表全量枚举所有线程, 以 PsIsSystemThread 判定系统线程,\n"
                       "并标注线程起始地址所属的驱动模块。右键可销毁线程, 销毁系统关键线程可能导致蓝屏。"), this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QStringLiteral("color: gray;"));

    toolLayout->addWidget(m_refreshBtn);
    toolLayout->addStretch();
    mainLayout->addLayout(toolLayout);
    mainLayout->addWidget(hintLabel);

    // 线程列表
    m_table = new QTableWidget(this);
    m_table->setColumnCount(TC_Count);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("TID"),
        QStringLiteral("ETHREAD"),
        QStringLiteral("起始地址"),
        QStringLiteral("所属驱动")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setColumnWidth(TC_TID, 90);
    m_table->setColumnWidth(TC_ETHREAD, 150);
    m_table->setColumnWidth(TC_STARTADDR, 150);

    connect(m_table, &QTableWidget::customContextMenuRequested, this, &SystemThreadPage::onContextMenu);
    mainLayout->addWidget(m_table);

    // 状态栏
    auto* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    connect(m_refreshBtn, &QPushButton::clicked, this, &SystemThreadPage::onRefreshClicked);
}

void SystemThreadPage::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    auto* destroyAction = m_contextMenu->addAction(QStringLiteral("销毁线程 (APC)"));
    connect(destroyAction, &QAction::triggered, this, &SystemThreadPage::onDestroyThread);

    m_contextMenu->addSeparator();

    auto* copyTidAction = m_contextMenu->addAction(QStringLiteral("复制 TID"));
    connect(copyTidAction, &QAction::triggered, this, &SystemThreadPage::onCopyTid);

    auto* copyEThreadAction = m_contextMenu->addAction(QStringLiteral("复制 ETHREAD 地址"));
    connect(copyEThreadAction, &QAction::triggered, this, &SystemThreadPage::onCopyEThread);

    auto* copyDriverAction = m_contextMenu->addAction(QStringLiteral("复制驱动路径"));
    connect(copyDriverAction, &QAction::triggered, this, &SystemThreadPage::onCopyDriverPath);
}

void SystemThreadPage::refreshThreads()
{
    m_table->setRowCount(0);
    m_threads.clear();

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        m_statusLabel->setText(QStringLiteral("驱动未加载, 无法枚举系统线程"));
        return;
    }

    m_statusLabel->setText(QStringLiteral("正在枚举系统线程..."));
    m_table->setUpdatesEnabled(false);

    // 内核 CID 表全量枚举 + PsIsSystemThread 判定 + 模块归属，
    // 一次 IOCTL 返回（不再走 System 进程线程链表，被摘链/跨进程
    // 挂靠的系统线程也能枚举到，也无需逐线程二次查询驱动路径）。
    // ArkSysThreadArray 约 280KB, 堆分配避免逼近 1MB 线程栈上限
    auto arr = std::make_unique<ArkSysThreadArray>();
    if (!ctx.driver().cidEnumSystemThreads(*arr)) {
        m_table->setUpdatesEnabled(true);
        m_statusLabel->setText(QStringLiteral("枚举失败 (错误码: %1)")
            .arg(ctx.driver().lastIoError()));
        return;
    }

    m_threads.reserve(arr->count);
    m_table->setRowCount(arr->count);
    for (quint32 i = 0; i < arr->count && i < 1024; ++i) {
        const ArkSysThreadEntry& e = arr->entries[i];
        m_threads.append(e);

        m_table->setItem(i, TC_TID, new QTableWidgetItem(QString::number(e.tid)));

        if (e.ethread != 0) {
            m_table->setItem(i, TC_ETHREAD, new QTableWidgetItem(
                QStringLiteral("0x%1").arg(e.ethread, 0, 16, QChar('0'))));
        } else {
            auto* item = new QTableWidgetItem(QStringLiteral("N/A"));
            item->setForeground(QBrush(Qt::gray));
            m_table->setItem(i, TC_ETHREAD, item);
        }

        if (e.startAddress != 0) {
            m_table->setItem(i, TC_STARTADDR, new QTableWidgetItem(
                QStringLiteral("0x%1").arg(e.startAddress, 0, 16, QChar('0'))));
        } else {
            auto* item = new QTableWidgetItem(QStringLiteral("N/A"));
            item->setForeground(QBrush(Qt::gray));
            m_table->setItem(i, TC_STARTADDR, item);
        }

        QString driver = QString::fromWCharArray(e.driverPath);
        if (driver.isEmpty()) {
            auto* item = new QTableWidgetItem(QStringLiteral("(未知)"));
            item->setForeground(QBrush(Qt::gray));
            m_table->setItem(i, TC_DRIVER_PATH, item);
        } else {
            m_table->setItem(i, TC_DRIVER_PATH, new QTableWidgetItem(driver));
        }
    }

    m_table->setUpdatesEnabled(true);
    m_statusLabel->setText(QStringLiteral("共 %1 个系统线程 (全系统线程 %2 个)")
        .arg(arr->count).arg(arr->totalThreads));
}

void SystemThreadPage::onRefreshClicked()
{
    refreshThreads();
}

quint64 SystemThreadPage::currentEThread() const
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_threads.size()) return 0;
    return m_threads[row].ethread;
}

quint32 SystemThreadPage::currentTid() const
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_threads.size()) return 0;
    return m_threads[row].tid;
}

void SystemThreadPage::onContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    m_contextMenu->exec(m_table->viewport()->mapToGlobal(pos));
}

void SystemThreadPage::onDestroyThread()
{
    quint64 ethread = currentEThread();
    quint32 tid = currentTid();
    if (ethread == 0) {
        QMessageBox::warning(this, QStringLiteral("系统线程"),
            QStringLiteral("该线程没有 ETHREAD 地址, 无法销毁"));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("系统线程"),
        QStringLiteral("确定要销毁线程 TID=%1 (ETHREAD=0x%2) 吗?\n\n"
                       "警告: 销毁系统关键线程会导致蓝屏, 请确认该线程非关键线程!")
            .arg(tid)
            .arg(ethread, 0, 16, QChar('0')),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& ctx = AppContext::instance();
    if (!ctx.driverLoaded()) {
        QMessageBox::warning(this, QStringLiteral("系统线程"), QStringLiteral("驱动未加载"));
        return;
    }

    if (ctx.driver().apcTerminateThreadByEThread(ethread)) {
        QMessageBox::information(this, QStringLiteral("系统线程"),
            QStringLiteral("销毁线程指令已发送 (TID=%1)").arg(tid));
        // 刷新列表
        refreshThreads();
    } else {
        QMessageBox::warning(this, QStringLiteral("系统线程"),
            QStringLiteral("销毁线程失败 (错误码: %1)").arg(ctx.driver().lastIoError()));
    }
}

void SystemThreadPage::onCopyTid()
{
    quint32 tid = currentTid();
    if (tid == 0) return;
    QGuiApplication::clipboard()->setText(QString::number(tid));
    m_statusLabel->setText(QStringLiteral("已复制 TID: %1").arg(tid));
}

void SystemThreadPage::onCopyEThread()
{
    quint64 ethread = currentEThread();
    if (ethread == 0) return;
    QGuiApplication::clipboard()->setText(
        QStringLiteral("0x%1").arg(ethread, 0, 16, QChar('0')));
    m_statusLabel->setText(QStringLiteral("已复制 ETHREAD 地址"));
}

void SystemThreadPage::onCopyDriverPath()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* item = m_table->item(row, TC_DRIVER_PATH);
    if (!item) return;
    QString path = item->text();
    if (path.startsWith(QChar('('))) return;  // 占位文字不复制
    QGuiApplication::clipboard()->setText(path);
    m_statusLabel->setText(QStringLiteral("已复制驱动路径"));
}
