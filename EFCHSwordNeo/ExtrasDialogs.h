#pragma once

#include <QDialog>
#include <QVector>

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QTimer;
class QProgressBar;
class QPaintEvent;

// ============================================================================
// 文件粉碎器: 多次覆写 + 随机改名 + 删除（可选用驱动强删兜底）
// ============================================================================
class FileShredderDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileShredderDialog(QWidget* parent = nullptr);

private slots:
    void onBrowse();
    void onShred();

private:
    bool shredOneFile(const QString& path, int passes, QString& error);
    void collectFiles(const QString& path, QStringList& out);

    QLineEdit* m_pathEdit = nullptr;
    QSpinBox* m_passSpin = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_shredBtn = nullptr;
};

// ============================================================================
// 烟花彩蛋: 粒子动画，点击发射，Esc 退出
// ============================================================================
class FireworksDialog : public QDialog {
    Q_OBJECT
public:
    explicit FireworksDialog(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onTick();

private:
    struct Particle {
        double x, y;
        double vx, vy;
        int life = 0;
        int maxLife = 1;
        QColor color;
        bool rocket = false;
    };
    void launchAt(double x, double y);

    QTimer* m_timer = nullptr;
    QVector<Particle> m_particles;
    int m_tick = 0;
};
