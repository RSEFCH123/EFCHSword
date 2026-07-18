#pragma once

#include <QDialog>
#include <QString>
#include <QByteArray>

class QPlainTextEdit;
class QRadioButton;
class QCheckBox;
class QLabel;
class QPushButton;

// 文件阅读/编辑对话框
// 对应易语言: ReadFile_Window
class FileReaderDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FileReaderDialog(const QString& filePath, QWidget* parent = nullptr);
    ~FileReaderDialog() override;

private slots:
    void onViewModeChanged();
    void onSave();

private:
    void setupUi();
    void readFile();

    QString m_filePath;
    QByteArray m_fileData;

    QPlainTextEdit* m_textEdit = nullptr;
    QRadioButton* m_modeText = nullptr;
    QRadioButton* m_modeHex = nullptr;
    QRadioButton* m_modeOctal = nullptr;
    QCheckBox* m_chkModifiedOnly = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};
