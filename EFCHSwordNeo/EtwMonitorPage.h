#pragma once

#include <QWidget>
#include <QMutex>
#include <QVector>
#include <QHash>
#include <QString>
#include <QSet>
#include <QTimer>

class QPushButton;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;
class QTreeWidget;
class QTabWidget;
class QPlainTextEdit;
class EtwSessionManager;

struct EtwEventItem {
    quint64 sequence = 0;
    quint64 timestamp = 0;      // FILETIME
    quint32 processId = 0;
    quint32 threadId = 0;
    quint32 parentProcessId = 0;
    quint16 id = 0;
    quint8 opcode = 0;
    quint8 level = 0;
    quint64 keyword = 0;
    QString provider;
    QString eventName;          // 解析出的事件名
    QString type;               // 行为分类（中文）
    QString detail;             // 顶层属性 "name=value; ..."
    QString path;               // 尽力提取的路径/对象名
    QByteArray userData;        // 原始 payload 副本
    QStringList propertyLines;  // 属性树用（"name = value"）
};

// ETW 监视页（2.0 重写）
//  - 内核提供程序: NT Kernel Logger 全部 EVENT_TRACE_FLAG（按类别分组）
//  - 清单提供程序: 常用目录 + TdhEnumerateProviders 全系统枚举（支持关键字/级别）
//  - 进程多选过滤 / 子进程跟踪 / 启动并监视 / CSV 导出 / 属性+Hex 详情
class EtwMonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit EtwMonitorPage(QWidget* parent = nullptr);
    ~EtwMonitorPage() override;

private slots:
    void startStop();
    void pollEvents();
    void clearEvents();
    void exportCsv();
    void refreshProcesses();
    void launchAndMonitor();
    void onSelectionChanged();

private:
    void buildKernelFlagsTree();
    void buildProviderCatalog();
    void refreshSystemProviders();
    void updateStats();
    void appendEvent(const EtwEventItem& event);
    bool matchesFilter(const EtwEventItem& event) const;
    void showDetail(const EtwEventItem& event);

    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_childCheck = nullptr;
    QCheckBox* m_autoScrollCheck = nullptr;
    QLineEdit* m_keywordEdit = nullptr;
    QComboBox* m_levelCombo = nullptr;
    QLineEdit* m_providerSearchEdit = nullptr;
    QTreeWidget* m_typeTree = nullptr;          // 监视源树（内核标志 + 提供程序）
    QTreeWidget* m_processTree = nullptr;
    QPushButton* m_startButton = nullptr;
    QTableWidget* m_table = nullptr;
    QTreeWidget* m_detailTree = nullptr;
    QPlainTextEdit* m_hexView = nullptr;
    QLabel* m_statsLabel = nullptr;
    QTimer* m_timer = nullptr;
    EtwSessionManager* m_session = nullptr;
    QSet<quint32> m_trackedPids;
    QSet<quint32> m_launchPids;
    quint64 m_lastEventCount = 0;
    qint64 m_lastStatsTick = 0;
    QHash<quint64, EtwEventItem> m_bySeq;       // seq -> 最近事件（详情查看）
};
