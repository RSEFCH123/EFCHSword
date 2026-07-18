#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QThread>

#include "AppContext.h"

class QLabel;
class QTableWidget;
class QPushButton;

// 工作线程类：负责递归枚举并删除所有文件
// 必须放在头文件中以便 moc 正确处理 Q_OBJECT
class DeleteDirWorker : public QObject
{
    Q_OBJECT
public:
    DeleteDirWorker(const QString& dirPath, int mode) : m_dirPath(dirPath), m_mode(mode) {}

public slots:
    void doWork()
    {
        bool allOk = true;

        // 先递归删除所有子项
        if (!deleteDirRecursive(m_dirPath)) {
            allOk = false;
        }

        // 最后删除空文件夹本身
        bool delOk = deleteOne(m_dirPath);
        if (!delOk) {
            emit failedFile(m_dirPath);
            allOk = false;
        }

        emit workFinished(allOk);
    }

signals:
    void currentFile(const QString& path);
    void failedFile(const QString& path);
    void workFinished(bool allOk);

private:
    QString m_dirPath;
    int m_mode;

    // 删除单个文件/空文件夹
    bool deleteOne(const QString& path)
    {
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) return false;

        if (m_mode == 0) {
            return ctx.driver().forceDeleteFile(path);
        } else if (m_mode == 1) {
            return ctx.driver().posixForceDeleteFile(path);
        } else { // m_mode == 2
            QString ntPath = QStringLiteral("\\??\\") + path;
            return ctx.driver().zwDeleteFile(ntPath);
        }
    }

    // 递归删除目录
    bool deleteDirRecursive(const QString& dirPath)
    {
        auto& ctx = AppContext::instance();
        if (!ctx.driverLoaded()) return false;

        // 确保路径以 \ 结尾
        QString queryPath = dirPath;
        if (!queryPath.endsWith('\\')) queryPath += '\\';

        // 通过驱动枚举所有子项
        QVector<DataInfoAnsi> entries;
        if (!ctx.driver().irpQueryFileAndFolders(queryPath, entries)) {
            return false;
        }

        bool allOk = true;
        for (const auto& entry : entries) {
            if (entry.fileName == QStringLiteral(".") || entry.fileName == QStringLiteral("..")) {
                continue;
            }

            QString fullPath = dirPath;
            if (!fullPath.endsWith('\\')) fullPath += '\\';
            fullPath += entry.fileName;

            emit currentFile(fullPath);

            if (entry.isDirectory) {
                // 递归删除子目录
                if (!deleteDirRecursive(fullPath)) {
                    allOk = false;
                }
                // 删除空文件夹本身
                if (!deleteOne(fullPath)) {
                    emit failedFile(fullPath);
                    allOk = false;
                }
            } else {
                // 删除文件
                if (!deleteOne(fullPath)) {
                    emit failedFile(fullPath);
                    allOk = false;
                }
            }
        }
        return allOk;
    }
};

// 删除目录进度对话框
// 工作线程进行递归删除，主线程显示进度和失败文件列表
// 删除模式:
//   0 = ForceDeleteFile (IRP 暴力删除)
//   1 = POSIXForceDeleteFile
//   2 = ZwDeleteFile
class DeleteDirProgressDialog : public QWidget
{
    Q_OBJECT
public:
    explicit DeleteDirProgressDialog(const QString& dirPath, int mode, QWidget* parent = nullptr);
    ~DeleteDirProgressDialog() override;

signals:
    void finished(bool allOk);

private slots:
    void onCurrentFile(const QString& path);
    void onFailedFile(const QString& path);
    void onWorkFinished(bool allOk);
    void onCopyFailedList();

private:
    void setupUi();

    QString m_dirPath;
    int m_mode;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_currentLabel = nullptr;
    QTableWidget* m_failedTable = nullptr;
    QPushButton* m_copyFailedBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QThread* m_thread = nullptr;
    int m_totalProcessed = 0;
};
