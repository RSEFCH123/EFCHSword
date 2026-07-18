#pragma once

#include <QWidget>
#include <QByteArray>

class QRadioButton;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;

// DiskHex 磁盘扇区编辑对话框
// 三种模式:
//   Ring3    - 用户层 CreateFile + ReadFile/WriteFile 直接读写物理磁盘
//   IRP      - 驱动 IOCTL_FORCEWRITEDISK / IOCTL_IRPREADDISK
//   Storport - 驱动 IOCTL_STORDIRECT_WRITEDISK / IOCTL_STORDIRECT_READDISK
//              需要 RaidPort 输入框让用户填写
class DiskHexDialog : public QWidget
{
    Q_OBJECT
public:
    explicit DiskHexDialog(QWidget* parent = nullptr);
    ~DiskHexDialog() override;

private slots:
    void onReadClicked();          // 读取磁盘扇区
    void onWriteClicked();         // 将编辑框内十六进制写入磁盘
    void onWriteFileClicked();     // 从文件读取内容写入磁盘
    void onCreateBlankClicked();   // 创建指定大小的空文件
    void onModeChanged();          // 切换模式时更新 UI
    void onHexChanged();           // 十六进制变更时同步 ASCII 预览

private:
    void setupUi();

    // 模式选择
    QRadioButton* m_ring3Radio = nullptr;     // Ring3 模式
    QRadioButton* m_irpRadio = nullptr;       // IRP 模式
    QRadioButton* m_storRadio = nullptr;      // Storport 模式

    // 参数
    QLineEdit* m_startSectorEdit = nullptr;   // 起始扇区
    QLineEdit* m_sectorNumEdit = nullptr;     // 扇区数量
    QLineEdit* m_raidPortEdit = nullptr;      // Storport RaidPort（用户填写）
    QLineEdit* m_diskNumberEdit = nullptr;    // 物理磁盘号（Ring3 模式用）

    // 数据
    QPlainTextEdit* m_hexEdit = nullptr;      // 十六进制编辑框
    QPlainTextEdit* m_asciiEdit = nullptr;    // ASCII 预览（只读）

    // 状态
    QLabel* m_statusLabel = nullptr;

    // 当前缓存数据（写入时使用）
    QByteArray m_currentData;
    quint64 m_currentStartSector = 0;
    quint64 m_currentSectorNumber = 0;
};
