#include "EtwMonitorPage.h"

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "tdh.lib")

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QMutexLocker>
#include <QAbstractItemView>
#include <QRegularExpression>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <deque>

// ============================================================================
// 常量
// ============================================================================
namespace {

// NT Kernel Logger（SystemTraceControlGuid），部分 SDK 未导出
const GUID kSystemTraceControlGuid =
    { 0x9e814aad, 0x3204, 0x11d2, { 0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39 } };

// 私有清单会话 GUID（EFCHSword Neo）
const GUID kEfwSessionGuid =
    { 0x3f8a1d5c, 0x9b2e, 0x4c47, { 0xa1, 0x66, 0x5e, 0xf0, 0x77, 0x33, 0x21, 0xaa } };

const wchar_t kPrivateSessionName[] = L"EFCHSwordNeoETW";

struct KernelFlagEntry {
    const char* name;
    ULONG flag;
};

// 全部 EVENT_TRACE_FLAG（evntrace.h），按类别分组 —— 覆盖 Windows 内核 ETW 全部行为
const struct { const char* category; std::vector<KernelFlagEntry> flags; } kKernelFlagGroups[] = {
    { u8"进程 / 线程", {
        { u8"进程创建与退出", 0x00000001 },           // PROCESS
        { u8"线程创建与退出", 0x00000002 },           // THREAD
        { u8"进程性能计数器", 0x00000008 },           // PROCESS_COUNTERS
        { u8"上下文切换", 0x00000010 },               // CSWITCH
        { u8"调度器 ReadyThread", 0x00000800 },       // DISPATCHER
        { u8"作业对象", 0x00080000 },                 // JOB
        { u8"系统调用", 0x00000080 },                 // SYSTEMCALL
    }},
    { u8"磁盘 / 文件", {
        { u8"文件 IO", 0x02000000 },                  // FILE_IO
        { u8"文件 IO 初始化", 0x04000000 },           // FILE_IO_INIT
        { u8"磁盘物理 IO", 0x00000100 },              // DISK_IO
        { u8"磁盘 IO 初始化", 0x00000400 },           // DISK_IO_INIT
        { u8"磁盘拆分 IO (卷管理)", 0x00200000 },     // SPLIT_IO
    }},
    { u8"注册表", {
        { u8"注册表调用", 0x00020000 },               // REGISTRY
    }},
    { u8"网络", {
        { u8"TCP/IP 收发", 0x00010000 },              // NETWORK_TCPIP
    }},
    { u8"内存 / 分页", {
        { u8"页错误 (全部)", 0x00001000 },            // MEMORY_PAGE_FAULTS
        { u8"硬页错误", 0x00002000 },                 // MEMORY_HARD_FAULTS
        { u8"虚拟内存分配", 0x00008000 },             // VIRTUAL_ALLOC
        { u8"映射/卸载 (VAMAP)", 0x00004000 },        // VAMAP
    }},
    { u8"镜像 / 驱动", {
        { u8"镜像加载", 0x00000004 },                 // IMAGE_LOAD
        { u8"驱动延迟", 0x00800000 },                 // DRIVER
    }},
    { u8"系统中断 / 性能", {
        { u8"DPC", 0x00000020 },                      // DPC
        { u8"中断", 0x00000040 },                     // INTERRUPT
        { u8"CPU 采样剖析 (Profile)", 0x01000000 },   // PROFILE
    }},
    { u8"调试 / 其他", {
        { u8"DbgPrint 输出", 0x00040000 },            // DBGPRINT
        { u8"调试器事件", 0x00400000 },               // DEBUG_EVENTS
        { u8"ALPC", 0x00100000 },                     // ALPC
        { u8"系统配置 rundown 禁用", 0x10000000 },    // NO_SYSCONFIG
    }},
};

// 常用清单提供程序目录
struct CatalogProvider {
    const char* name;
    const char* guid;
    const char* keywords;   // 默认关键字掩码(hex)
    const char* desc;
};
const CatalogProvider kProviderCatalog[] = {
    { "Microsoft-Windows-Kernel-Process",  "{22FB2CD6-0E7B-422B-A0C7-2FAD1FD0981F}", "0x70",  "进程/线程/镜像 (Process=0x10 Thread=0x20 Image=0x40)" },
    { "Microsoft-Windows-Kernel-File",     "{EDD08927-9CC4-4E65-B970-C2560FB5C289}", "0x70",  "文件 IO (IO=0x10 Init=0x40)" },
    { "Microsoft-Windows-Kernel-Network",  "{7DD42A49-5329-4832-8DFD-43D979153A88}", "0x30",  "网络 TCP/UDP" },
    { "Microsoft-Windows-Kernel-Registry", "{70EB4F03-C1DE-4F73-A051-33D13D5413BD}", "0xFF",  "注册表 (新建/打开/查询/删除...)" },
    { "Microsoft-Windows-DNS-Client",      "{1C95126E-7EEA-49A9-A3FE-A378B03DDB4D}", "0x0",   "DNS 解析请求" },
    { "Microsoft-Windows-PowerShell",      "{A0C1853B-5C40-4B15-8766-3CF1C58F985A}", "0x0",   "PowerShell 脚本执行" },
    { ".NET Runtime",                      "{E13C0D23-CCBC-4E12-931B-D9CC2EEE27E4}", "0x0",   ".NET GC/JIT/异常/加载器" },
    { "Microsoft-Windows-Win32k",          "{67D1B39B-93D8-4F6D-9CAE-81E4B6F2E4A9}", "0x0",   "Win32k (窗口/输入/消息)" },
};

QString guidText(const GUID& g) {
    wchar_t buf[64]{};
    if (StringFromGUID2(g, buf, 64) <= 0) return QStringLiteral("{unknown}");
    return QString::fromWCharArray(buf);
}

bool guidFromString(const QString& s, GUID* out)
{
    QString t = s.trimmed();
    if (!t.startsWith(QChar('{'))) t = QChar('{') + t + QChar('}');
    return SUCCEEDED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(t.utf16()), out));
}

QString hexPayload(const EVENT_RECORD* rec) {
    if (!rec || !rec->UserData || rec->UserDataLength == 0) return {};
    const auto* p = static_cast<const unsigned char*>(rec->UserData);
    const unsigned n = qMin<unsigned>(rec->UserDataLength, 64);
    QString out;
    for (unsigned i = 0; i < n; ++i) out += QStringLiteral("%1 ").arg(p[i], 2, 16, QLatin1Char('0'));
    if (rec->UserDataLength > n) out += QStringLiteral("...");
    return out.trimmed();
}

// FILETIME(100ns, 1601) -> epoch 毫秒
qint64 filetimeToMs(quint64 ft)
{
    return qint64((ft - 116444736000000000ULL) / 10000ULL);
}

static QString embeddedPath(const EVENT_RECORD* rec)
{
    if (!rec || !rec->UserData || rec->UserDataLength < 8) return {};
    const auto* bytes = static_cast<const unsigned char*>(rec->UserData);
    QString best;
    auto consider = [&best](const QString& s) {
        QString v = s.trimmed();
        if (v.size() < 4) return;
        const QString l = v.toLower();
        if (!(v.contains(QChar('\\')) || v.contains(QStringLiteral(":\\")) ||
              l.startsWith(QStringLiteral("\\device\\")) || l.startsWith(QStringLiteral("\\registry\\")) ||
              l.startsWith(QStringLiteral("hkey_")))) return;
        if (v.size() > best.size()) best = v;
    };
    for (ULONG off = 0; off + 8 <= rec->UserDataLength; ++off) {
        QString candidate;
        for (ULONG p = off; p + 1 < rec->UserDataLength; p += 2) {
            const ushort ch = static_cast<ushort>(bytes[p] | (bytes[p + 1] << 8));
            if (ch == 0) break;
            if (ch < 0x20 || ch > 0x7e && ch < 0x3000) { candidate.clear(); break; }
            candidate += QChar(ch);
            if (candidate.size() > 512) break;
        }
        consider(candidate);
    }
    QString ascii;
    for (ULONG i = 0; i < rec->UserDataLength; ++i) {
        const unsigned char ch = bytes[i];
        if (ch >= 0x20 && ch <= 0x7e) ascii += QChar(ch);
        else { consider(ascii); ascii.clear(); }
    }
    consider(ascii);
    return best;
}

static QString translateEventName(QString name)
{
    const QString lower = name.toLower();
    if (lower.contains(QStringLiteral("processstart")) || lower.contains(QStringLiteral("startprocess"))) return QStringLiteral("进程创建");
    if (lower.contains(QStringLiteral("processstop")) || lower.contains(QStringLiteral("stopprocess"))) return QStringLiteral("进程退出");
    if (lower.contains(QStringLiteral("threadstart")) || lower.contains(QStringLiteral("startthread"))) return QStringLiteral("线程创建");
    if (lower.contains(QStringLiteral("threadstop")) || lower.contains(QStringLiteral("stopthread"))) return QStringLiteral("线程退出");
    if (lower.contains(QStringLiteral("imageload")) || lower.contains(QStringLiteral("imageload"))) return QStringLiteral("镜像加载");
    if (lower.contains(QStringLiteral("filecreate")) || lower.contains(QStringLiteral("createfile"))) return QStringLiteral("文件创建");
    if (lower.contains(QStringLiteral("filedelete")) || lower.contains(QStringLiteral("deletefile"))) return QStringLiteral("文件删除");
    if (lower.contains(QStringLiteral("rename"))) return QStringLiteral("重命名");
    if (lower.contains(QStringLiteral("write"))) return QStringLiteral("写入");
    if (lower.contains(QStringLiteral("read"))) return QStringLiteral("读取");
    if (lower.contains(QStringLiteral("close"))) return QStringLiteral("关闭");
    if (lower.contains(QStringLiteral("connect"))) return QStringLiteral("连接");
    if (lower.contains(QStringLiteral("disconnect"))) return QStringLiteral("断开连接");
    if (lower.contains(QStringLiteral("send"))) return QStringLiteral("发送");
    if (lower.contains(QStringLiteral("recv")) || lower.contains(QStringLiteral("receive"))) return QStringLiteral("接收");
    if (lower.contains(QStringLiteral("regopen"))) return QStringLiteral("注册表打开");
    if (lower.contains(QStringLiteral("regcreate"))) return QStringLiteral("注册表创建");
    if (lower.contains(QStringLiteral("regdelete"))) return QStringLiteral("注册表删除");
    if (lower.contains(QStringLiteral("regset"))) return QStringLiteral("注册表设置值");
    if (lower.contains(QStringLiteral("regquery"))) return QStringLiteral("注册表查询");
    return name;
}

static QString infoString(const TRACE_EVENT_INFO* info, ULONG offset)
{
    if (!info || !offset) return {};
    return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(reinterpret_cast<const unsigned char*>(info) + offset));
}

// 完整解析：返回 provider/事件名/行为分类/详情/属性列表/路径
static void parseEvent(EVENT_RECORD* rec, EtwEventItem* item)
{
    ULONG size = 0;
    TDHSTATUS st = TdhGetEventInformation(rec, 0, nullptr, nullptr, &size);
    std::vector<unsigned char> infoBuf;
    TRACE_EVENT_INFO* info = nullptr;
    if (st == ERROR_INSUFFICIENT_BUFFER && size && size < 4 * 1024 * 1024) {
        infoBuf.resize(size);
        info = reinterpret_cast<TRACE_EVENT_INFO*>(infoBuf.data());
        if (TdhGetEventInformation(rec, 0, nullptr, info, &size) != ERROR_SUCCESS) info = nullptr;
    }
    if (!info) return;

    QString provider = infoString(info, info->ProviderNameOffset);
    const QString guid = guidText(rec->EventHeader.ProviderId).toLower();
    if (provider.isEmpty())
        provider = (guid == guidText(kSystemTraceControlGuid)) ? QStringLiteral("Windows 内核") : guid;
    if (provider == QStringLiteral("Microsoft-Windows-Kernel-Process")) provider = QStringLiteral("Windows 内核·进程");
    else if (provider == QStringLiteral("Microsoft-Windows-Kernel-File")) provider = QStringLiteral("Windows 内核·文件");
    else if (provider == QStringLiteral("Microsoft-Windows-Kernel-Registry")) provider = QStringLiteral("Windows 内核·注册表");
    else if (provider == QStringLiteral("Microsoft-Windows-Kernel-Network")) provider = QStringLiteral("Windows 内核·网络");
    item->provider = provider;

    const QString manifestName = infoString(info, info->EventNameOffset);
    const QString opcodeName = infoString(info, info->OpcodeNameOffset);
    item->eventName = !manifestName.isEmpty() ? manifestName : (!opcodeName.isEmpty() ? opcodeName : QStringLiteral("Opcode %1").arg(rec->EventHeader.EventDescriptor.Opcode));
    item->type = translateEventName(item->eventName);

    const bool is32 = (rec->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) != 0;
    for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i) {
        const auto& pi = info->EventPropertyInfoArray[i];
        if (pi.Flags & PropertyStruct) continue;
        const QString name = infoString(info, pi.NameOffset);
        if (name.isEmpty()) continue;
        std::wstring wname = name.toStdWString();
        PROPERTY_DATA_DESCRIPTOR pd{};
        pd.PropertyName = reinterpret_cast<ULONGLONG>(wname.c_str());
        ULONG cb = 0;
        if (TdhGetPropertySize(rec, 0, nullptr, 1, &pd, &cb) != ERROR_SUCCESS || cb == 0 || cb > 1024 * 1024) continue;
        std::vector<unsigned char> value(cb);
        if (TdhGetProperty(rec, 0, nullptr, 1, &pd, cb, value.data()) != ERROR_SUCCESS) continue;
        ULONG outCb = 4096;
        std::vector<wchar_t> out(outCb / sizeof(wchar_t));
        USHORT consumed = 0;
        const TDHSTATUS fs = TdhFormatProperty(info, nullptr, is32 ? 4 : 8,
            pi.nonStructType.InType, pi.nonStructType.OutType,
            static_cast<USHORT>(cb), static_cast<USHORT>(cb),
            value.data(), &outCb, out.data(), &consumed);
        QString text = fs == ERROR_SUCCESS ? QString::fromWCharArray(out.data()) : hexPayload(rec);
        if (text.isEmpty()) continue;
        item->detail += (item->detail.isEmpty() ? QString() : QStringLiteral("; ")) + name + QStringLiteral("=") + text;
        item->propertyLines << (name + QStringLiteral(" = ") + text);
        const QString lower = name.toLower();
        if (text.size() < 64 &&
            (lower.contains(QStringLiteral("path")) || lower.contains(QStringLiteral("file")) ||
             lower.contains(QStringLiteral("keyname")) || lower.contains(QStringLiteral("imagename")))) {
            if (item->path.isEmpty() || text.size() > item->path.size()) item->path = text;
        }
    }
    if (item->detail.isEmpty()) item->detail = hexPayload(rec);
    if (item->path.isEmpty()) item->path = embeddedPath(rec);
}

} // namespace

// ============================================================================
// 会话管理器：内核会话(NT Kernel Logger) + 私有清单会话
// ============================================================================
struct ProviderSelection {
    GUID guid{};
    ULONGLONG keywords = 0;
    UCHAR level = 5;
    QString name;
};

class EtwSessionManager {
public:
    ~EtwSessionManager() { stop(); }

    bool start(ULONG kernelFlags, const QList<ProviderSelection>& providers, QString& error)
    {
        stop();
        bool ok = false;
        if (kernelFlags) ok = startKernel(kernelFlags, error) || ok;
        if (ok && !providers.isEmpty()) startPrivate(providers, error);
        if (!ok && !kernelFlags) {
            // 只有清单提供程序
            ok = startPrivate(providers, error);
        }
        return m_runningCount > 0;
    }

    bool startKernel(ULONG flags, QString& error)
    {
        const size_t size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * (MAX_PATH + 1) * sizeof(wchar_t);
        m_kernelProps.assign(size, 0);
        auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_kernelProps.data());
        p->Wnode.BufferSize = static_cast<ULONG>(size);
        p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        p->Wnode.Guid = kSystemTraceControlGuid;
        p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
        p->EnableFlags = flags;
        p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        p->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + (MAX_PATH + 1) * sizeof(wchar_t);
        auto* name = reinterpret_cast<wchar_t*>(m_kernelProps.data() + p->LoggerNameOffset);
        wcsncpy_s(name, MAX_PATH, KERNEL_LOGGER_NAME, _TRUNCATE);
        ULONG status = StartTraceW(&m_kernelSession, name, p);
        if (status == ERROR_ALREADY_EXISTS) {
            ControlTraceW(0, name, p, EVENT_TRACE_CONTROL_STOP);
            m_kernelSession = 0;
            status = StartTraceW(&m_kernelSession, name, p);
        }
        if (status != ERROR_SUCCESS) {
            error = QStringLiteral("内核会话启动失败: %1 (NT Kernel Logger 可能被其他工具占用)").arg(status);
            m_kernelSession = 0;
            return false;
        }
        if (!openConsumer(m_kernelSession, KERNEL_LOGGER_NAME)) {
            error = QStringLiteral("内核会话 OpenTrace 失败");
            ControlTraceW(m_kernelSession, name, p, EVENT_TRACE_CONTROL_STOP);
            m_kernelSession = 0;
            return false;
        }
        m_runningCount++;
        return true;
    }

    bool startPrivate(const QList<ProviderSelection>& providers, QString& error)
    {
        const size_t size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * (MAX_PATH + 1) * sizeof(wchar_t);
        m_privProps.assign(size, 0);
        auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_privProps.data());
        p->Wnode.BufferSize = static_cast<ULONG>(size);
        p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        p->Wnode.Guid = kEfwSessionGuid;
        p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        p->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + (MAX_PATH + 1) * sizeof(wchar_t);
        auto* name = reinterpret_cast<wchar_t*>(m_privProps.data() + p->LoggerNameOffset);
        wcsncpy_s(name, MAX_PATH, kPrivateSessionName, _TRUNCATE);
        ULONG status = StartTraceW(&m_privSession, name, p);
        if (status == ERROR_ALREADY_EXISTS) {
            ControlTraceW(0, name, p, EVENT_TRACE_CONTROL_STOP);
            m_privSession = 0;
            status = StartTraceW(&m_privSession, name, p);
        }
        if (status != ERROR_SUCCESS) {
            error = QStringLiteral("清单会话启动失败: %1").arg(status);
            m_privSession = 0;
            return false;
        }
        for (const auto& sel : providers) {
            EnableTraceEx2(m_privSession, &sel.guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                           sel.level, sel.keywords, 0, 0, nullptr);
        }
        if (!openConsumer(m_privSession, kPrivateSessionName)) {
            error = QStringLiteral("清单会话 OpenTrace 失败");
            ControlTraceW(m_privSession, name, p, EVENT_TRACE_CONTROL_STOP);
            m_privSession = 0;
            return false;
        }
        m_runningCount++;
        return true;
    }

    void stop()
    {
        // CloseTrace 让 ProcessTrace 返回，然后 join，再 ControlTrace 停会话
        if (m_kernelTrace != INVALID_PROCESSTRACE_HANDLE) { CloseTrace(m_kernelTrace); m_kernelTrace = INVALID_PROCESSTRACE_HANDLE; }
        if (m_privTrace != INVALID_PROCESSTRACE_HANDLE) { CloseTrace(m_privTrace); m_privTrace = INVALID_PROCESSTRACE_HANDLE; }
        for (auto& t : m_threads) if (t.joinable()) t.join();
        m_threads.clear();
        if (m_kernelSession) {
            auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_kernelProps.data());
            ControlTraceW(m_kernelSession, reinterpret_cast<wchar_t*>(m_kernelProps.data() + p->LoggerNameOffset), p, EVENT_TRACE_CONTROL_STOP);
            m_kernelSession = 0;
        }
        if (m_privSession) {
            auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(m_privProps.data());
            ControlTraceW(m_privSession, reinterpret_cast<wchar_t*>(m_privProps.data() + p->LoggerNameOffset), p, EVENT_TRACE_CONTROL_STOP);
            m_privSession = 0;
        }
        m_kernelProps.clear();
        m_privProps.clear();
        m_runningCount = 0;
    }

    bool running() const { return m_runningCount > 0; }

    quint64 totalEnqueued() const { return m_total; }
    quint64 dropped() const { return m_dropped; }

    QVector<EtwEventItem> takeEvents()
    {
        QMutexLocker lock(&m_mutex);
        QVector<EtwEventItem> result;
        result.reserve(int(m_events.size()));
        while (!m_events.empty()) { result.push_back(std::move(m_events.front())); m_events.pop_front(); }
        return result;
    }

private:
    bool openConsumer(TRACEHANDLE session, const wchar_t* name)
    {
        EVENT_TRACE_LOGFILEW log{};
        log.LoggerName = const_cast<LPWSTR>(name);
        log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        log.EventRecordCallback = &EtwSessionManager::eventCallback;
        log.Context = this;
        const TRACEHANDLE h = OpenTraceW(&log);
        if (h == INVALID_PROCESSTRACE_HANDLE) return false;
        if (session == m_kernelSession) m_kernelTrace = h; else m_privTrace = h;
        m_threads.emplace_back([this, h] {
            TRACEHANDLE hh = h;
            ProcessTrace(&hh, 1, nullptr, nullptr);
        });
        return true;
    }

    static void WINAPI eventCallback(EVENT_RECORD* rec)
    {
        auto* self = static_cast<EtwSessionManager*>(rec->UserContext);
        if (self) self->onEvent(rec);
    }

    void onEvent(EVENT_RECORD* rec)
    {
        EtwEventItem item;
        item.timestamp = rec->EventHeader.TimeStamp.QuadPart;
        item.processId = rec->EventHeader.ProcessId;
        item.threadId = rec->EventHeader.ThreadId;
        if (rec->EventHeader.EventDescriptor.Opcode == 1 && rec->UserData && rec->UserDataLength >= 8) {
            const auto* words = static_cast<const ULONG*>(rec->UserData);
            item.parentProcessId = words[1];
        }
        item.id = rec->EventHeader.EventDescriptor.Id;
        item.opcode = rec->EventHeader.EventDescriptor.Opcode;
        item.level = rec->EventHeader.EventDescriptor.Level;
        item.keyword = rec->EventHeader.EventDescriptor.Keyword;
        if (rec->UserData && rec->UserDataLength) {
            item.userData = QByteArray(reinterpret_cast<const char*>(rec->UserData),
                                       int(qMin<ULONG>(rec->UserDataLength, 4096)));
        }
        parseEvent(rec, &item);

        QMutexLocker lock(&m_mutex);
        if (m_events.size() < 30000) {
            item.sequence = ++m_seq;
            m_events.push_back(std::move(item));
        } else {
            m_dropped++;
        }
        m_total++;
    }

    std::vector<unsigned char> m_kernelProps;
    std::vector<unsigned char> m_privProps;
    TRACEHANDLE m_kernelSession = 0;
    TRACEHANDLE m_privSession = 0;
    TRACEHANDLE m_kernelTrace = INVALID_PROCESSTRACE_HANDLE;
    TRACEHANDLE m_privTrace = INVALID_PROCESSTRACE_HANDLE;
    std::vector<std::thread> m_threads;
    int m_runningCount = 0;
    QMutex m_mutex;
    std::deque<EtwEventItem> m_events;
    std::atomic<quint64> m_seq{ 0 };
    std::atomic<quint64> m_total{ 0 };
    std::atomic<quint64> m_dropped{ 0 };
};

// ============================================================================
// 页面
// ============================================================================
EtwMonitorPage::EtwMonitorPage(QWidget* parent)
    : QWidget(parent), m_session(new EtwSessionManager())
{
    auto* root = new QHBoxLayout(this);

    // ================= 左侧：监视源 + 进程 =================
    auto* leftSplit = new QSplitter(Qt::Vertical, this);
    leftSplit->setMaximumWidth(300);

    m_typeTree = new QTreeWidget(leftSplit);
    m_typeTree->setHeaderLabel(QStringLiteral("ETW 监视源（勾选后启动；提供程序双击可改关键字）"));
    buildKernelFlagsTree();
    buildProviderCatalog();
    leftSplit->addWidget(m_typeTree);

    auto* providerPane = new QWidget(leftSplit);
    auto* provLayout = new QVBoxLayout(providerPane);
    provLayout->setContentsMargins(0, 0, 0, 0);
    provLayout->addWidget(new QLabel(QStringLiteral("关键字掩码(hex, 0=无关键字事件):"), providerPane));
    m_keywordEdit = new QLineEdit(providerPane);
    m_keywordEdit->setPlaceholderText(QStringLiteral("如 0xFFFFFFFFFFFFFFFF"));
    m_keywordEdit->setText(QStringLiteral("0"));
    provLayout->addWidget(m_keywordEdit);
    provLayout->addWidget(new QLabel(QStringLiteral("级别:"), providerPane));
    m_levelCombo = new QComboBox(providerPane);
    m_levelCombo->addItems({ QStringLiteral("详细(5)"), QStringLiteral("信息(4)"), QStringLiteral("警告(3)"), QStringLiteral("错误(2)"), QStringLiteral("关键(1)") });
    provLayout->addWidget(m_levelCombo);
    m_providerSearchEdit = new QLineEdit(providerPane);
    m_providerSearchEdit->setPlaceholderText(QStringLiteral("搜索全部系统提供程序..."));
    provLayout->addWidget(m_providerSearchEdit);
    auto* refreshProvBtn = new QPushButton(QStringLiteral("重新枚举系统提供程序"), providerPane);
    provLayout->addWidget(refreshProvBtn);
    providerPane->setLayout(provLayout);
    leftSplit->addWidget(providerPane);

    m_processTree = new QTreeWidget(leftSplit);
    m_processTree->setHeaderLabels({ QStringLiteral("监视进程（多选）"), QStringLiteral("PID") });
    leftSplit->addWidget(m_processTree);
    leftSplit->setStretchFactor(0, 3);
    leftSplit->setStretchFactor(2, 2);

    auto* leftBox = new QVBoxLayout();
    leftBox->addWidget(leftSplit, 1);
    m_childCheck = new QCheckBox(QStringLiteral("包含子进程"), this);
    m_childCheck->setChecked(true);
    leftBox->addWidget(m_childCheck);
    auto* procBtnRow = new QHBoxLayout();
    auto* refreshProc = new QPushButton(QStringLiteral("刷新进程"), this);
    auto* launchProc = new QPushButton(QStringLiteral("启动并监视"), this);
    refreshProc->setMaximumHeight(26);
    launchProc->setMaximumHeight(26);
    procBtnRow->addWidget(refreshProc);
    procBtnRow->addWidget(launchProc);
    leftBox->addLayout(procBtnRow);
    auto* leftWidget = new QWidget(this);
    leftWidget->setLayout(leftBox);
    leftWidget->setMaximumWidth(310);
    root->addWidget(leftWidget);

    // ================= 右侧：工具栏 + 表格 + 详情 =================
    auto* rightBox = new QVBoxLayout();
    auto* controls = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("启动监视"), this);
    m_startButton->setMaximumHeight(28);
    controls->addWidget(m_startButton);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索 PID/进程/路径/事件/数据"));
    m_searchEdit->setMaximumWidth(240);
    controls->addWidget(m_searchEdit);
    m_autoScrollCheck = new QCheckBox(QStringLiteral("自动滚动"), this);
    m_autoScrollCheck->setChecked(true);
    controls->addWidget(m_autoScrollCheck);
    auto* clear = new QPushButton(QStringLiteral("清空"), this);
    clear->setMaximumHeight(26);
    controls->addWidget(clear);
    auto* exportBtn = new QPushButton(QStringLiteral("导出CSV"), this);
    exportBtn->setMaximumHeight(26);
    controls->addWidget(exportBtn);
    controls->addStretch();
    controls->setSpacing(4);
    rightBox->addLayout(controls);

    m_statsLabel = new QLabel(QStringLiteral("未启动"), this);
    m_statsLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightBox->addWidget(m_statsLabel);

    auto* vSplit = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(vSplit);
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("PID"), QStringLiteral("TID"),
        QStringLiteral("提供程序"), QStringLiteral("事件"), QStringLiteral("行为"),
        QStringLiteral("ID"), QStringLiteral("路径/对象"), QStringLiteral("内容") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setColumnWidth(0, 92);
    m_table->setColumnWidth(1, 62);
    m_table->setColumnWidth(2, 62);
    m_table->setColumnWidth(3, 170);
    m_table->setColumnWidth(4, 180);
    m_table->setColumnWidth(5, 100);
    m_table->setColumnWidth(6, 52);
    m_table->setColumnWidth(7, 220);
    m_table->horizontalHeader()->setStretchLastSection(true);

    auto* detailTabs = new QTabWidget(vSplit);
    m_detailTree = new QTreeWidget(detailTabs);
    m_detailTree->setHeaderLabels({ QStringLiteral("属性"), QStringLiteral("值") });
    m_detailTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_hexView = new QPlainTextEdit(detailTabs);
    m_hexView->setReadOnly(true);
    m_hexView->setFont(QFont(QStringLiteral("Consolas"), 9));
    detailTabs->addTab(m_detailTree, QStringLiteral("事件属性"));
    detailTabs->addTab(m_hexView, QStringLiteral("原始数据"));
    detailTabs->setMinimumHeight(120);

    vSplit->addWidget(m_table);
    vSplit->addWidget(detailTabs);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 1);
    rightBox->addWidget(vSplit, 1);

    auto* rightWidget = new QWidget(this);
    rightWidget->setLayout(rightBox);
    root->addWidget(rightWidget, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &EtwMonitorPage::pollEvents);
    m_timer->start();

    connect(m_startButton, &QPushButton::clicked, this, &EtwMonitorPage::startStop);
    connect(clear, &QPushButton::clicked, this, &EtwMonitorPage::clearEvents);
    connect(exportBtn, &QPushButton::clicked, this, &EtwMonitorPage::exportCsv);
    connect(refreshProc, &QPushButton::clicked, this, &EtwMonitorPage::refreshProcesses);
    connect(launchProc, &QPushButton::clicked, this, &EtwMonitorPage::launchAndMonitor);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { m_table->setRowCount(0); });
    connect(m_providerSearchEdit, &QLineEdit::textChanged, this, &EtwMonitorPage::refreshSystemProviders);
    connect(refreshProvBtn, &QPushButton::clicked, this, &EtwMonitorPage::refreshSystemProviders);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &EtwMonitorPage::onSelectionChanged);
    connect(m_typeTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item->data(0, Qt::UserRole + 1).toString() != QStringLiteral("provider")) return;
        bool ok = false;
        const QString cur = item->data(0, Qt::UserRole + 2).toString();
        const QString txt = QInputDialog::getText(this, QStringLiteral("关键字掩码"),
            QStringLiteral("为 %1 设置关键字掩码(hex，0=只收无关键字事件):").arg(item->text(0)),
            QLineEdit::Normal, cur.isEmpty() ? QStringLiteral("0") : cur, &ok);
        if (ok) item->setData(0, Qt::UserRole + 2, txt.trimmed());
    });
    refreshProcesses();
    refreshSystemProviders();
}

EtwMonitorPage::~EtwMonitorPage()
{
    if (m_session) m_session->stop();
    delete m_session;
}

// ---------------------------------------------------------------------------
void EtwMonitorPage::buildKernelFlagsTree()
{
    auto* kernelRoot = new QTreeWidgetItem(m_typeTree, { QStringLiteral("内核提供程序 (NT Kernel Logger)") });
    kernelRoot->setData(0, Qt::UserRole + 1, QStringLiteral("kernelRoot"));
    for (const auto& group : kKernelFlagGroups) {
        auto* cat = new QTreeWidgetItem(kernelRoot, { QString::fromUtf8(group.category) });
        for (const auto& f : group.flags) {
            auto* leaf = new QTreeWidgetItem(cat, { QString::fromUtf8(f.name) });
            leaf->setCheckState(0, Qt::Unchecked);
            leaf->setData(0, Qt::UserRole, QVariant(static_cast<uint>(f.flag)));
            leaf->setData(0, Qt::UserRole + 1, QStringLiteral("flag"));
        }
    }
    m_typeTree->expandAll();
}

void EtwMonitorPage::buildProviderCatalog()
{
    auto* root = new QTreeWidgetItem(m_typeTree, { QStringLiteral("常用清单提供程序") });
    for (const auto& p : kProviderCatalog) {
        auto* leaf = new QTreeWidgetItem(root, { QString::fromUtf8(p.name) });
        leaf->setCheckState(0, Qt::Unchecked);
        leaf->setToolTip(0, QStringLiteral("%1\n关键字: %2 (双击修改)").arg(QString::fromUtf8(p.desc)).arg(QString::fromUtf8(p.keywords)));
        leaf->setData(0, Qt::UserRole, QString::fromUtf8(p.guid));
        leaf->setData(0, Qt::UserRole + 1, QStringLiteral("provider"));
        leaf->setData(0, Qt::UserRole + 2, QString::fromUtf8(p.keywords));
    }
}

void EtwMonitorPage::refreshSystemProviders()
{
    // 找到/创建系统提供程序根节点
    QTreeWidgetItem* root = nullptr;
    for (int i = 0; i < m_typeTree->topLevelItemCount(); ++i) {
        if (m_typeTree->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("sysRoot")) {
            root = m_typeTree->topLevelItem(i);
            break;
        }
    }
    if (!root) {
        root = new QTreeWidgetItem(m_typeTree, { QStringLiteral("系统提供程序 (全部)") });
        root->setData(0, Qt::UserRole + 1, QStringLiteral("sysRoot"));
    }

    const ULONG FILTER_MAX = 4000;
    ULONG size = 0;
    TDHSTATUS st = TdhEnumerateProviders(nullptr, &size);
    if (st != ERROR_INSUFFICIENT_BUFFER || !size || size > 64 * 1024 * 1024) return;
    std::vector<unsigned char> buf(size);
    auto* enumInfo = reinterpret_cast<PROVIDER_ENUMERATION_INFO*>(buf.data());
    if (TdhEnumerateProviders(enumInfo, &size) != ERROR_SUCCESS) return;

    QSet<QString> keepChecked;   // 保持已勾选状态
    for (int i = 0; i < root->childCount(); ++i)
        if (root->child(i)->checkState(0) == Qt::Checked) keepChecked.insert(root->child(i)->text(0));

    const QString filter = m_providerSearchEdit ? m_providerSearchEdit->text().trimmed() : QString();
    root->takeChildren();
    ULONG added = 0;
    for (ULONG i = 0; i < enumInfo->NumberOfProviders && added < FILTER_MAX; ++i) {
        const auto& pi = enumInfo->TraceProviderInfoArray[i];
        const QString name = infoString(reinterpret_cast<TRACE_EVENT_INFO*>(enumInfo), pi.ProviderNameOffset);
        if (name.isEmpty()) continue;
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)) continue;
        auto* leaf = new QTreeWidgetItem(root, { name });
        leaf->setCheckState(0, keepChecked.contains(name) ? Qt::Checked : Qt::Unchecked);
        leaf->setData(0, Qt::UserRole, guidText(pi.ProviderGuid));
        leaf->setData(0, Qt::UserRole + 1, QStringLiteral("provider"));
        leaf->setData(0, Qt::UserRole + 2, QString());   // 空 = 使用全局关键字
        ++added;
    }
    if (added == FILTER_MAX) {
        auto* note = new QTreeWidgetItem(root, { QStringLiteral("(已达显示上限 %1，请用搜索过滤)").arg(FILTER_MAX) });
        note->setDisabled(true);
    }
}

QSet<quint32> etwSelectedPids(const QTreeWidget* tree)
{
    QSet<quint32> out;
    if (!tree) return out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* it = tree->topLevelItem(i);
        if (it->checkState(0) == Qt::Checked) out.insert(it->text(1).toUInt());
    }
    return out;
}

bool EtwMonitorPage::matchesFilter(const EtwEventItem& event) const
{
    const auto selected = etwSelectedPids(m_processTree);
    if (!selected.isEmpty() || !m_launchPids.isEmpty())
        return selected.contains(event.processId) || m_launchPids.contains(event.processId) ||
               (m_childCheck->isChecked() && m_trackedPids.contains(event.processId));
    return true;
}

void EtwMonitorPage::startStop()
{
    if (m_session->running()) {
        m_session->stop();
        m_startButton->setText(QStringLiteral("启动监视"));
        updateStats();
        return;
    }
    // 收集内核标志
    ULONG flags = 0;
    QList<ProviderSelection> providers;
    for (int i = 0; i < m_typeTree->topLevelItemCount(); ++i) {
        auto* root = m_typeTree->topLevelItem(i);
        const QString kind = root->data(0, Qt::UserRole + 1).toString();
        for (int j = 0; j < root->childCount(); ++j) {
            auto* cat = root->child(j);
            if (kind == QStringLiteral("kernelRoot")) {
                for (int k = 0; k < cat->childCount(); ++k) {
                    auto* leaf = cat->child(k);
                    if (leaf->checkState(0) == Qt::Checked) flags |= leaf->data(0, Qt::UserRole).toUInt();
                }
            } else if (cat->checkState(0) == Qt::Checked) {
                ProviderSelection sel;
                if (!guidFromString(cat->data(0, Qt::UserRole).toString(), &sel.guid)) continue;
                sel.name = cat->text(0);
                bool ok = false;
                const QString kw = cat->data(0, Qt::UserRole + 2).toString();
                if (!kw.isEmpty()) {
                    sel.keywords = kw.toULongLong(&ok, 16);
                    if (!ok) sel.keywords = 0;
                } else {
                    sel.keywords = m_keywordEdit->text().trimmed().toULongLong(&ok, 16);
                    if (!ok) sel.keywords = 0;
                }
                sel.level = UCHAR(5 - m_levelCombo->currentIndex());
                providers.append(sel);
            }
        }
    }
    if (!flags && providers.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("ETW"), QStringLiteral("请先勾选要监视的 ETW 类型/提供程序"));
        return;
    }
    QString error;
    if (!m_session->start(flags, providers, error)) {
        QMessageBox::warning(this, QStringLiteral("ETW"), error);
        return;
    }
    m_startButton->setText(QStringLiteral("停止监视"));
}

void EtwMonitorPage::pollEvents()
{
    const auto pending = m_session->takeEvents();
    int shown = 0;
    for (const auto& e : pending) {
        const auto selected = etwSelectedPids(m_processTree);
        if (!selected.isEmpty() && selected.contains(e.parentProcessId)) m_trackedPids.insert(e.processId);
        if (matchesFilter(e)) {
            ++shown;
            if (shown <= 300) {
                appendEvent(e);
                if (m_bySeq.size() > 2000) {
                    // 淘汰最早的缓存
                    quint64 minSeq = ~0ULL;
                    for (quint64 k : m_bySeq.keys()) minSeq = qMin(minSeq, k);
                    if (minSeq != ~0ULL) m_bySeq.remove(minSeq);
                }
                m_bySeq.insert(e.sequence, e);
            }
        }
    }
    updateStats();
    if (!m_session->running() && m_startButton->text() == QStringLiteral("停止监视"))
        m_startButton->setText(QStringLiteral("启动监视"));
}

void EtwMonitorPage::updateStats()
{
    const quint64 total = m_session->totalEnqueued();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QString rateText;
    if (m_lastStatsTick && now > m_lastStatsTick) {
        const double rate = double(total >= m_lastEventCount ? total - m_lastEventCount : 0) * 1000.0 / double(now - m_lastStatsTick);
        rateText = QStringLiteral("速率 %1 事件/秒").arg(rate, 0, 'f', 1);
    }
    m_lastEventCount = total;
    m_lastStatsTick = now;
    const QString drop = m_session->dropped() ? QStringLiteral("  (已丢弃 %1, 缓冲区满)").arg(m_session->dropped()) : QString();
    m_statsLabel->setText(m_session->running()
        ? QStringLiteral("运行中: 已接收 %1 事件  %2%3").arg(total).arg(rateText).arg(drop)
        : QStringLiteral("已停止: 共接收 %1 事件%2").arg(total).arg(drop));
}

void EtwMonitorPage::clearEvents() { m_table->setRowCount(0); m_bySeq.clear(); m_detailTree->clear(); m_hexView->clear(); }

void EtwMonitorPage::refreshProcesses()
{
    if (!m_processTree) return;
    QSet<quint32> checked = etwSelectedPids(m_processTree);
    m_processTree->clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) do {
        auto* i = new QTreeWidgetItem(m_processTree, { QString::fromWCharArray(pe.szExeFile), QString::number(pe.th32ProcessID) });
        i->setCheckState(0, checked.contains(pe.th32ProcessID) ? Qt::Checked : Qt::Unchecked);
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
}

void EtwMonitorPage::launchAndMonitor()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要启动的程序"), QString(), QStringLiteral("程序 (*.exe);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    const QStringList perms{ QStringLiteral("继承当前权限"), QStringLiteral("管理员权限（UAC）"), QStringLiteral("标准用户权限") };
    bool permOk = false;
    const QString perm = QInputDialog::getItem(this, QStringLiteral("启动权限"), QStringLiteral("选择目标进程启动权限"), perms, 0, false, &permOk);
    if (!permOk) return;
    if (!m_session->running()) {
        startStop();
        if (!m_session->running()) return;   // 启动失败已在 startStop 弹窗
    }
    DWORD pid = 0;
    if (perm == perms.at(1)) {
        SHELLEXECUTEINFOW sx{};
        sx.cbSize = sizeof(sx); sx.fMask = SEE_MASK_NOCLOSEPROCESS; sx.lpVerb = L"runas";
        const std::wstring wpath = path.toStdWString(); sx.lpFile = wpath.c_str(); sx.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sx)) { QMessageBox::warning(this, QStringLiteral("启动进程"), QStringLiteral("管理员启动失败: %1").arg(GetLastError())); return; }
        pid = GetProcessId(sx.hProcess); CloseHandle(sx.hProcess);
    } else {
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        std::wstring cmd = path.toStdWString();
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            QMessageBox::warning(this, QStringLiteral("启动进程"), QStringLiteral("CreateProcess失败: %1").arg(GetLastError())); return;
        }
        pid = pi.dwProcessId; CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    if (pid) {
        m_launchPids.insert(pid);
        refreshProcesses();
        for (int i = 0; i < m_processTree->topLevelItemCount(); ++i) {
            auto* it = m_processTree->topLevelItem(i);
            if (it->text(1).toUInt() == pid) it->setCheckState(0, Qt::Checked);
        }
    }
}

void EtwMonitorPage::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出ETW CSV"), QStringLiteral("etw_events.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream s(&f);
    s << "Time,PID,TID,Provider,Event,Behavior,EventID,Level,Path,Data\n";
    for (int r = 0; r < m_table->rowCount(); ++r) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (c) s << ',';
            QString v = m_table->item(r, c) ? m_table->item(r, c)->text() : QString();
            v.replace('"', "\"\"");
            s << '"' << v << '"';
        }
        s << '\n';
    }
}

void EtwMonitorPage::onSelectionChanged()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    auto* it = m_table->item(row, 0);
    if (!it) return;
    bool ok = false;
    const quint64 seq = it->data(Qt::UserRole).toULongLong(&ok);
    if (!ok) return;
    if (m_bySeq.contains(seq)) showDetail(m_bySeq.value(seq));
}

void EtwMonitorPage::showDetail(const EtwEventItem& e)
{
    m_detailTree->clear();
    auto add = [this](const QString& k, const QString& v) { new QTreeWidgetItem(m_detailTree, { k, v }); };
    add(QStringLiteral("提供程序"), e.provider);
    add(QStringLiteral("事件名"), e.eventName);
    add(QStringLiteral("行为分类"), e.type);
    add(QStringLiteral("事件ID / Opcode"), QStringLiteral("%1 / %2").arg(e.id).arg(e.opcode));
    add(QStringLiteral("级别 / 关键字"), QStringLiteral("%1 / 0x%2").arg(e.level).arg(e.keyword, 0, 16));
    add(QStringLiteral("PID / TID"), QStringLiteral("%1 / %2").arg(e.processId).arg(e.threadId));
    add(QStringLiteral("路径/对象"), e.path.isEmpty() ? QStringLiteral("(无)") : e.path);
    add(QStringLiteral("内容"), e.detail);
    for (const QString& line : e.propertyLines) {
        const int eq = line.indexOf(QStringLiteral(" = "));
        if (eq > 0) add(line.left(eq), line.mid(eq + 3));
        else add(line, QString());
    }
    m_detailTree->expandAll();

    QString hex;
    const QByteArray& d = e.userData;
    const int total = qMin<int>(d.size(), 2048);
    for (int i = 0; i < total; i += 16) {
        hex += QStringLiteral("%1  ").arg(i, 4, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < total) {
                const quint8 b = quint8(d[i + j]);
                hex += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
            } else hex += QStringLiteral("   ");
            if (j == 7) hex += QChar(' ');
        }
        hex += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    if (d.size() > total) hex += QStringLiteral("... (共 %1 字节)").arg(d.size());
    m_hexView->setPlainText(hex);
}

void EtwMonitorPage::appendEvent(const EtwEventItem& e)
{
    const QString q = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (!q.isEmpty() && !QStringLiteral("%1 %2 %3 %4 %5 %6 %7").arg(e.processId).arg(e.threadId).arg(e.type).arg(e.id).arg(e.provider).arg(e.path).arg(e.detail).contains(q, Qt::CaseInsensitive)) return;
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QString ts = QDateTime::fromMSecsSinceEpoch(filetimeToMs(e.timestamp)).toString(QStringLiteral("HH:mm:ss.zzz"));
    auto* first = new QTableWidgetItem(ts);
    first->setData(Qt::UserRole, QVariant::fromValue<quint64>(e.sequence));
    m_table->setItem(row, 0, first);
    const QStringList values{
        QString(), QString::number(e.processId), QString::number(e.threadId),
        e.provider, e.eventName, e.type, QString::number(e.id),
        e.path.isEmpty() ? QStringLiteral("(无路径/非路径事件)") : e.path, e.detail };
    for (int i = 1; i < values.size(); ++i) m_table->setItem(row, i, new QTableWidgetItem(values[i]));
    // 着色: 开始/结束事件
    QColor c;
    if (e.opcode == 1) c = QColor(220, 250, 220);
    else if (e.opcode == 2) c = QColor(255, 225, 225);
    else if (e.level && e.level <= 2) c = QColor(255, 235, 205);   // 错误/关键
    if (c.isValid())
        for (int i = 0; i < m_table->columnCount(); ++i)
            if (m_table->item(row, i)) m_table->item(row, i)->setBackground(QBrush(c));
    if (m_table->rowCount() > 5000) m_table->removeRow(0);
    if (m_autoScrollCheck->isChecked()) m_table->scrollToBottom();
}
