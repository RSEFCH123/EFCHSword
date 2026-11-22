#include "DeleteDirProgressDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QGuiApplication>
#include <QClipboard>
#include <QMessageBox>

DeleteDirProgressDialog::DeleteDirProgressDialog(const QString& dirPath, int mode, QWidget* parent)
    : QWidget(parent), m_dirPath(dirPath), m_mode(mode)
{
    setWindowTitle(QStringLiteral("删除目录 - %1").arg(dirPath));
    resize(700, 400);

    setupUi();

    // 启动工作线程
    m_thread = new QThread(this);
    auto* worker = new DeleteDirWorker(dirPath, mode);
    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &DeleteDirWorker::doWork);
    connect(worker, &DeleteDirWorker::currentFile, this, &DeleteDirProgressDialog::onCurrentFile);
    connect(worker, &DeleteDirWorker::failedFile, this, &DeleteDirProgressDialog::onFailedFile);
    connect(worker, &DeleteDirWorker::workFinished, this, &DeleteDirProgressDialog::onWorkFinished);
    connect(worker, &DeleteDirWorker::workFinished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    m_thread->start();
}

DeleteDirProgressDialog::~DeleteDirProgressDialog()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
}

void DeleteDirProgressDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_titleLabel = new QLabel(QStringLiteral("正在删除目录: %1").arg(m_dirPath), this);
    m_titleLabel->setWordWrap(true);
    QFont boldFont = m_titleLabel->font();
    boldFont.setBold(true);
    m_titleLabel->setFont(boldFont);
    mainLayout->addWidget(m_titleLabel);

    m_currentLabel = new QLabel(QStringLiteral("正在准备..."), this);
    m_currentLabel->setWordWrap(true);
    mainLayout->addWidget(m_currentLabel);

    auto* failedLabel = new QLabel(QStringLiteral("删除失败的文件:"), this);
    mainLayout->addWidget(failedLabel);

    m_failedTable = new QTableWidget(this);
    m_failedTable->setColumnCount(1);
    m_failedTable->setHorizontalHeaderLabels({QStringLiteral("文件路径")});
    m_failedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_failedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_failedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_failedTable->verticalHeader()->setVisible(false);
    m_failedTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(m_failedTable, 1);

    auto* btnLayout = new QHBoxLayout();
    m_copyFailedBtn = new QPushButton(QStringLiteral("复制失败列表"), this);
    m_closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    m_closeBtn->setEnabled(false);
    btnLayout->addStretch();
    btnLayout->addWidget(m_copyFailedBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(m_copyFailedBtn, &QPushButton::clicked, this, &DeleteDirProgressDialog::onCopyFailedList);
}

void DeleteDirProgressDialog::onCurrentFile(const QString& path)
{
    m_currentLabel->setText(QStringLiteral("正在删除: %1").arg(path));
    m_totalProcessed++;
}

void DeleteDirProgressDialog::onFailedFile(const QString& path)
{
    int row = m_failedTable->rowCount();
    m_failedTable->insertRow(row);
    m_failedTable->setItem(row, 0, new QTableWidgetItem(path));
}

void DeleteDirProgressDialog::onWorkFinished(bool allOk)
{
    m_currentLabel->setText(allOk
        ? QStringLiteral("删除完成! 共处理 %1 个项目").arg(m_totalProcessed)
        : QStringLiteral("删除完成 (部分失败)! 共处理 %1 个项目, 失败 %2 个")
              .arg(m_totalProcessed).arg(m_failedTable->rowCount()));
    m_closeBtn->setEnabled(true);
    emit finished(allOk);
}

void DeleteDirProgressDialog::onCopyFailedList()
{
    if (m_failedTable->rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有失败的文件"));
        return;
    }

    QString text;
    for (int i = 0; i < m_failedTable->rowCount(); ++i) {
        text += m_failedTable->item(i, 0)->text() + QStringLiteral("\n");
    }
    QGuiApplication::clipboard()->setText(text);
    QMessageBox::information(this, QStringLiteral("提示"),
        QStringLiteral("已复制 %1 个失败路径").arg(m_failedTable->rowCount()));
}
