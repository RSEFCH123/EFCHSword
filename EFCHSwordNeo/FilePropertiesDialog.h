#pragma once

#include <QDialog>
#include <QString>
#include <windows.h>

class QLabel;
class QLineEdit;
class QCheckBox;
class QPushButton;

// 文件属性对话框
// 对应易语言: 文件属性窗口
class FilePropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilePropertiesDialog(const QString& filePath, QWidget* parent = nullptr);
    ~FilePropertiesDialog() override;

private slots:
    void onLocateFile();
    void onAttributeChanged();

private:
    void setupUi();
    void loadFileInfo();
    void loadFileAttributes();
    void setFileAttribute(DWORD attr, bool add);
    DWORD getCurrentAttributes();

    QString m_filePath;
    bool m_initing = true;

    QLabel* m_filePathLabel = nullptr;
    QLabel* m_fileDescLabel = nullptr;
    QLabel* m_fileVersionLabel = nullptr;
    QLabel* m_fileCopyrightLabel = nullptr;
    QLabel* m_fileSizeLabel = nullptr;
    QLabel* m_fileTypeLabel = nullptr;
    QLineEdit* m_md5Edit = nullptr;
    QLineEdit* m_sha1Edit = nullptr;
    QLineEdit* m_sha256Edit = nullptr;

    QCheckBox* m_chkReadOnly = nullptr;
    QCheckBox* m_chkHidden = nullptr;
    QCheckBox* m_chkSystem = nullptr;
    QCheckBox* m_chkArchive = nullptr;
    QCheckBox* m_chkTemporary = nullptr;
    QCheckBox* m_chkCompressed = nullptr;
    QCheckBox* m_chkEncrypted = nullptr;

    QPushButton* m_locateBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};
