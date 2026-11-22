#include "NetMonitorPage.h"
#include "AppContext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QBrush>
#include <QTimer>
#include <QFileDialog>
#include <QFile>
#include <QDataStream>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QDateTime>

#include <windows.h>
#include <mstcpip.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <thread>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// ============================================================================
// 抓包数据结构
// ============================================================================
struct CapturedPacket {
    quint64 seq = 0;
    qint64 tsMs = 0;
    quint32 pid = 0;
    quint32 len = 0;            // IP 包总长
    int family = AF_INET;
    quint8 ipProto = 0;
    quint16 srcPort = 0;
    quint16 dstPort = 0;
    QString srcAddr;
    QString dstAddr;
    QString procName;
    QByteArray data;            // 完整 IP 包
};

// 网卡条目（文件作用域，便于在 combo 里传索引）
struct AdapterEntry {
    QString name;
    QString ip;     // IPv4 (可能为空)
    QString ip6;    // IPv6 (可能为空)
};
static QVector<AdapterEntry> g_netAdapters;

static QString addr4Text(const quint8* b)
{
    return QStringLiteral("%1.%2.%3.%4").arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3]);
}

static QString addr6Text(const quint8* b)
{
    char buf[64] = { 0 };
    sockaddr_in6 sa{};
    sa.sin6_family = AF_INET6;
    memcpy(&sa.sin6_addr, b, 16);
    DWORD n = 64;
    if (WSAAddressToStringA(reinterpret_cast<sockaddr*>(&sa), sizeof(sa), nullptr, buf, &n) == 0)
        return QString::fromLatin1(buf);
    return QStringLiteral("(ipv6)");
}

// ============================================================================
// 解码辅助
// ============================================================================
namespace PacketDecode {

static const char* ipProtoName(quint8 p)
{
    switch (p) {
    case 1: return "ICMP"; case 2: return "IGMP"; case 6: return "TCP";
    case 17: return "UDP"; case 47: return "GRE"; case 58: return "ICMPv6";
    case 89: return "OSPF"; case 132: return "SCTP";
    default: return nullptr;
    }
}

static void tcpFlagsText(quint8 f, QString& out)
{
    if (f & 0x02) out += QStringLiteral("SYN,");
    if (f & 0x10) out += QStringLiteral("ACK,");
    if (f & 0x01) out += QStringLiteral("FIN,");
    if (f & 0x04) out += QStringLiteral("RST,");
    if (f & 0x08) out += QStringLiteral("PSH,");
    if (f & 0x20) out += QStringLiteral("URG,");
    if (out.endsWith(QChar(','))) out.chop(1);
}

// 返回应用层 payload（跳过 IPv4/IPv6 及扩展头、TCP/UDP 头），附带传输层信息
static QByteArray transportPayload(const QByteArray& d, quint8 ipProto, quint16* srcPort,
                                   quint16* dstPort, quint32* seq, quint8* tcpFlags)
{
    if (srcPort) *srcPort = 0;
    if (dstPort) *dstPort = 0;
    if (seq) *seq = 0;
    if (tcpFlags) *tcpFlags = 0;
    if (d.isEmpty()) return {};
    const quint8 ver = d[0] >> 4;
    int off = 0;
    quint8 proto = 0;
    if (ver == 4) {
        const int ihl = (d[0] & 0x0F) * 4;
        if (ihl < 20 || d.size() < ihl) return {};
        proto = d[9];
        off = ihl;
    } else if (ver == 6) {
        if (d.size() < 40) return {};
        proto = d[6];
        off = 40;
        // 跳过 IPv6 扩展头
        for (int hop = 0; hop < 8; ++hop) {
            if (proto == 0 || proto == 43 || proto == 60 || proto == 51) {
                if (d.size() < off + 8) return {};
                proto = d[off];
                off += 8 + d[off + 1] * 8;
            } else if (proto == 44) {
                if (d.size() < off + 8) return {};
                proto = d[off];
                off += 8 + 8 * d[off + 4];
            } else break;
        }
    } else {
        return {};
    }
    if (proto != ipProto) return {};

    if (proto == 6 && d.size() >= off + 20) {
        if (srcPort) *srcPort = (quint8(d[off]) << 8) | quint8(d[off + 1]);
        if (dstPort) *dstPort = (quint8(d[off + 2]) << 8) | quint8(d[off + 3]);
        if (seq) *seq = (quint32(quint8(d[off + 4])) << 24) | (quint32(quint8(d[off + 5])) << 16)
            | (quint32(quint8(d[off + 6])) << 8) | quint32(quint8(d[off + 7]));
        if (tcpFlags) *tcpFlags = d[off + 13];
        const int dataOff = (d[off + 12] >> 4) * 4;
        if (dataOff < 20) return {};
        return d.mid(off + dataOff);
    }
    if (proto == 17 && d.size() >= off + 8) {
        if (srcPort) *srcPort = (quint8(d[off]) << 8) | quint8(d[off + 1]);
        if (dstPort) *dstPort = (quint8(d[off + 2]) << 8) | quint8(d[off + 3]);
        return d.mid(off + 8);
    }
    if ((proto == 1 || proto == 58) && d.size() >= off + 8) {
        return d.mid(off + 8);
    }
    return {};
}

// DNS: 取查询名（前 2 个）
static QString dnsQueryNames(const QByteArray& p)
{
    if (p.size() < 12) return {};
    quint16 qd = (quint8(p[4]) << 8) | quint8(p[5]);
    if (!qd) return {};
    int pos = 12;
    QString first;
    for (int q = 0; q < qMin<quint16>(qd, 4); ++q) {
        QString name;
        int guard = 0;
        while (pos < p.size() && guard++ < 64) {
            const int len = quint8(p[pos++]);
            if (!len) break;
            if (len & 0xC0) { pos += 1; break; }
            if (pos + len > p.size()) return first;
            name += QString::fromLatin1(p.mid(pos, len)) + QChar('.');
            pos += len;
        }
        if (!q) first = name;
    }
    return first;
}

// TLS ClientHello SNI
static QString tlsSni(const QByteArray& p)
{
    if (p.size() < 43 || quint8(p[0]) != 0x16) return {};   // handshake record
    if (quint8(p[5]) != 0x01) return {};                    // ClientHello
    int pos = 43;                                            // skip header+session+ciphers+compression 可变, 先粗定位
    // 逐字段解析
    pos = 43;
    // Session ID
    if (pos + 1 > p.size()) return {};
    const int sidLen = quint8(p[32 + 0]) ; // session id 在 43 处之后才对——改为正式解析
    Q_UNUSED(sidLen);
    pos = 43;
    const int sessionIdLen = quint8(p[pos]); pos += 1 + sessionIdLen;
    if (pos + 2 > p.size()) return {};
    const int cipherLen = (quint8(p[pos]) << 8) | quint8(p[pos + 1]); pos += 2 + cipherLen;
    if (pos + 1 > p.size()) return {};
    const int compLen = quint8(p[pos]); pos += 1 + compLen;
    if (pos + 2 > p.size()) return {};
    // 扩展区
    pos += 2;   // extensions length
    while (pos + 4 <= p.size()) {
        const int extType = (quint8(p[pos]) << 8) | quint8(p[pos + 1]);
        const int extLen = (quint8(p[pos + 2]) << 8) | quint8(p[pos + 3]);
        pos += 4;
        if (extType == 0x0000 && pos + 5 <= p.size()) {   // server_name
            const int listLen = (quint8(p[pos]) << 8) | quint8(p[pos + 1]);
            Q_UNUSED(listLen);
            if (quint8(p[pos + 2]) == 0x00) {
                const int nameLen = (quint8(p[pos + 3]) << 8) | quint8(p[pos + 4]);
                if (pos + 5 + nameLen <= p.size())
                    return QString::fromLatin1(p.mid(pos + 5, nameLen));
            }
            return {};
        }
        pos += extLen;
    }
    return {};
}

static QString httpFirstLine(const QByteArray& p)
{
    const QList<QByteArray> methods = { "GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ", "HTTP/1." };
    for (const auto& m : methods) {
        if (p.startsWith(m)) {
            const int eol = p.indexOf('\n');
            return QString::fromLatin1(p.left(eol < 0 ? qMin<int>(p.size(), 128) : eol)).trimmed();
        }
    }
    return {};
}

static QString hexDump(const QByteArray& d)
{
    QString out;
    const int total = qMin<int>(d.size(), 2048);
    for (int i = 0; i < total; i += 16) {
        out += QStringLiteral("%1  ").arg(i, 4, 16, QLatin1Char('0'));
        QString ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < total) {
                const quint8 b = quint8(d[i + j]);
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7F) ? QChar(b) : QChar('.');
            } else {
                out += QStringLiteral("   ");
            }
            if (j == 7) out += QStringLiteral(" ");
        }
        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    if (d.size() > total) out += QStringLiteral("... (共 %1 字节)\n").arg(d.size());
    return out;
}

static QString asciiDump(const QByteArray& d)
{
    QString out;
    const int total = qMin<int>(d.size(), 8192);
    for (int i = 0; i < total; ++i) {
        const quint8 b = quint8(d[i]);
        out += (b >= 0x20 && b < 0x7F) ? QChar(b) : (b == '\n' || b == '\r' || b == '\t' ? QChar(b) : QChar('.'));
    }
    return out;
}

// 构建应用层概要（HTTP/DNS/TLS SNI）
static QString appSummary(quint16 srcPort, quint16 dstPort, const QByteArray& payload)
{
    if (payload.isEmpty()) return {};
    QString s = httpFirstLine(payload);
    if (!s.isEmpty()) return QStringLiteral("HTTP: %1").arg(s);
    if (srcPort == 53 || dstPort == 53) {
        const QString n = dnsQueryNames(payload);
        if (!n.isEmpty()) return QStringLiteral("DNS 查询: %1").arg(n);
    }
    const QString sni = tlsSni(payload);
    if (!sni.isEmpty()) return QStringLiteral("TLS SNI: %1").arg(sni);
    return {};
}

// 构建分层解析树
static void buildTree(QTreeWidget* tree, const QByteArray& d)
{
    tree->clear();
    if (d.isEmpty()) return;
    const quint8 ver = d[0] >> 4;
    if (ver == 4 && d.size() >= 20) {
        const int ihl = (d[0] & 0x0F) * 4;
        const quint8 proto = d[9];
        auto* ip = new QTreeWidgetItem(tree, { QStringLiteral("IPv4"),
            QStringLiteral("%1 -> %2").arg(addr4Text((const quint8*)d.data() + 12)).arg(addr4Text((const quint8*)d.data() + 16)) });
        new QTreeWidgetItem(ip, { QStringLiteral("版本/头长"), QStringLiteral("%1 / %2 字节").arg(ver).arg(ihl) });
        new QTreeWidgetItem(ip, { QStringLiteral("总长"), QString::number((quint8(d[2]) << 8) | quint8(d[3])) });
        new QTreeWidgetItem(ip, { QStringLiteral("TTL"), QString::number(d[8]) });
        const char* pn = ipProtoName(proto);
        new QTreeWidgetItem(ip, { QStringLiteral("协议"), pn ? QStringLiteral("%1 (%2)").arg(proto).arg(pn) : QString::number(proto) });

        if (proto == 6 && d.size() >= ihl + 20) {
            const int o = ihl;
            auto* tcp = new QTreeWidgetItem(ip, { QStringLiteral("TCP"),
                QStringLiteral("%1 -> %2").arg((quint8(d[o]) << 8) | quint8(d[o + 1])).arg((quint8(d[o + 2]) << 8) | quint8(d[o + 3])) });
            new QTreeWidgetItem(tcp, { QStringLiteral("源端口"), QString::number((quint8(d[o]) << 8) | quint8(d[o + 1])) });
            new QTreeWidgetItem(tcp, { QStringLiteral("目的端口"), QString::number((quint8(d[o + 2]) << 8) | quint8(d[o + 3])) });
            quint32 seq = (quint32(quint8(d[o + 4])) << 24) | (quint32(quint8(d[o + 5])) << 16) | (quint32(quint8(d[o + 6])) << 8) | quint32(quint8(d[o + 7]));
            quint32 ack = (quint32(quint8(d[o + 8])) << 24) | (quint32(quint8(d[o + 9])) << 16) | (quint32(quint8(d[o + 10])) << 8) | quint32(quint8(d[o + 11]));
            new QTreeWidgetItem(tcp, { QStringLiteral("Seq / Ack"), QStringLiteral("%1 / %2").arg(seq).arg(ack) });
            QString fl;
            tcpFlagsText(d[o + 13], fl);
            new QTreeWidgetItem(tcp, { QStringLiteral("Flags"), fl });
            new QTreeWidgetItem(tcp, { QStringLiteral("窗口"), QString::number((quint8(d[o + 14]) << 8) | quint8(d[o + 15])) });
            const QByteArray payload = transportPayload(d, 6, nullptr, nullptr, nullptr, nullptr);
            if (!payload.isEmpty()) {
                const QString app = appSummary(0, 0, payload);
                new QTreeWidgetItem(tcp, { QStringLiteral("应用层"),
                    app.isEmpty() ? QStringLiteral("%1 字节数据").arg(payload.size()) : app });
            }
        } else if (proto == 17 && d.size() >= ihl + 8) {
            const int o = ihl;
            auto* udp = new QTreeWidgetItem(ip, { QStringLiteral("UDP"),
                QStringLiteral("%1 -> %2").arg((quint8(d[o]) << 8) | quint8(d[o + 1])).arg((quint8(d[o + 2]) << 8) | quint8(d[o + 3])) });
            const QByteArray payload = transportPayload(d, 17, nullptr, nullptr, nullptr, nullptr);
            const QString app = appSummary((quint8(d[o]) << 8) | quint8(d[o + 1]), (quint8(d[o + 2]) << 8) | quint8(d[o + 3]), payload);
            new QTreeWidgetItem(udp, { QStringLiteral("应用层"),
                app.isEmpty() ? QStringLiteral("%1 字节数据").arg(qMax(0, payload.size())) : app });
        }
    } else if (ver == 6 && d.size() >= 40) {
        auto* ip = new QTreeWidgetItem(tree, { QStringLiteral("IPv6"),
            QStringLiteral("%1 -> %2").arg(addr6Text((const quint8*)d.data() + 8)).arg(addr6Text((const quint8*)d.data() + 24)) });
        new QTreeWidgetItem(ip, { QStringLiteral("跳数限制"), QString::number(d[7]) });
        const char* pn = ipProtoName(d[6]);
        new QTreeWidgetItem(ip, { QStringLiteral("下一头"), pn ? QString::fromLatin1(pn) : QString::number(d[6]) });
    } else {
        new QTreeWidgetItem(tree, { QStringLiteral("未知包"), QStringLiteral("%1 字节").arg(d.size()) });
    }
    tree->expandAll();
}

} // namespace PacketDecode

// ============================================================================
// CaptureEngine: 原始套接字抓包线程
// ============================================================================
class NetMonitorPage::CaptureEngine
{
public:
    using AdapterEntry = ::AdapterEntry;

    CaptureEngine()
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~CaptureEngine() { stop(); }

    static QVector<AdapterEntry> enumAdapters()
    {
        QVector<AdapterEntry> out;
        ULONG size = 16 * 1024;
        QByteArray buf;
        for (int attempt = 0; attempt < 3; ++attempt) {
            buf.resize(size);
            const ULONG r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                                                 nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &size);
            if (r == NO_ERROR) break;
            if (r != ERROR_BUFFER_OVERFLOW) return out;
        }
        for (auto* a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            AdapterEntry e;
            e.name = QString::fromWCharArray(a->FriendlyName);
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr) continue;
                if (u->Address.lpSockaddr->sa_family == AF_INET) {
                    e.ip = addr4Text((const quint8*)&reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr);
                } else if (u->Address.lpSockaddr->sa_family == AF_INET6 && e.ip6.isEmpty()) {
                    e.ip6 = addr6Text((const quint8*)&reinterpret_cast<sockaddr_in6*>(u->Address.lpSockaddr)->sin6_addr);
                }
            }
            if (!e.ip.isEmpty() || !e.ip6.isEmpty()) out.append(e);
        }
        return out;
    }

    bool start(const AdapterEntry& adapter)
    {
        if (m_running) return true;
        m_adapter = adapter;
        m_running = true;
        m_thread = std::thread([this] { captureLoop(); });
        return true;
    }

    void stop()
    {
        if (!m_running.exchange(false)) {
            if (m_thread.joinable()) m_thread.join();
            return;
        }
        if (m_thread.joinable()) m_thread.join();
    }

    bool running() const { return m_running; }

    void drain(QVector<CapturedPacket>& out)
    {
        QMutexLocker lock(&m_mutex);
        out.reserve(out.size() + m_queue.size());
        while (!m_queue.empty()) { out.push_back(std::move(m_queue.front())); m_queue.pop_front(); }
    }

    void snapshotHistory(QVector<CapturedPacket>& out)
    {
        QMutexLocker lock(&m_mutex);
        out = m_history;
    }

    quint64 total() const { return m_total; }
    quint64 tcpCount() const { return m_tcp; }
    quint64 udpCount() const { return m_udp; }
    quint64 icmpCount() const { return m_icmp; }
    quint64 byteCount() const { return m_bytes; }

private:
    bool openSocket(const sockaddr* bindAddr, int bindLen, int af, int proto)
    {
        SOCKET s = socket(af, SOCK_RAW, proto);
        if (s == INVALID_SOCKET) return false;
        BOOL in = TRUE;
        setsockopt(s, IPPROTO_IP, IP_HDRINCL, (const char*)&in, sizeof(in));
        if (bind(s, bindAddr, bindLen) != 0) { closesocket(s); return false; }
        DWORD flag = RCVALL_ON;
        DWORD ret = 0;
        DWORD level = af == AF_INET6 ? IPPROTO_IPV6 : IPPROTO_IP;
        if (WSAIoctl(s, SIO_RCVALL, &flag, sizeof(flag), nullptr, 0, &ret, nullptr, nullptr) != 0) {
            closesocket(s);
            return false;
        }
        Q_UNUSED(level);
        m_socks.push_back(s);
        return true;
    }

    void captureLoop()
    {
        m_socks.clear();
        if (!m_adapter.ip.isEmpty()) {
            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = inet_addr(m_adapter.ip.toLatin1().constData());
            openSocket(reinterpret_cast<const sockaddr*>(&sa), sizeof(sa), AF_INET, IPPROTO_IP);
        }
        if (!m_adapter.ip6.isEmpty()) {
            sockaddr_in6 sa6{};
            sa6.sin6_family = AF_INET6;
            sa6.sin6_addr = in6addr_any;
            openSocket(reinterpret_cast<const sockaddr*>(&sa6), sizeof(sa6), AF_INET6, IPPROTO_IPV6);
        }
        if (m_socks.empty()) { m_running = false; return; }

        std::vector<char> buf(65535);
        while (m_running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            SOCKET maxS = 0;
            for (SOCKET s : m_socks) { FD_SET(s, &readSet); if (s > maxS) maxS = s; }
            timeval tv{ 0, 300 * 1000 };
            const int ready = select((int)maxS + 1, &readSet, nullptr, nullptr, &tv);
            if (ready <= 0) continue;
            for (SOCKET s : m_socks) {
                if (!FD_ISSET(s, &readSet)) continue;
                sockaddr_in from{};
                int fromLen = sizeof(from);
                const int n = recvfrom(s, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
                if (n <= 0) continue;
                CapturedPacket pkt;
                pkt.tsMs = QDateTime::currentMSecsSinceEpoch();
                pkt.len = (quint32)n;
                pkt.data = QByteArray::fromRawData(buf.data(), n);
                const quint8 ver = quint8(buf[0]) >> 4;
                pkt.family = ver == 6 ? AF_INET6 : AF_INET;
                pkt.data.detach();
                if (ver == 4 && n >= 20) {
                    pkt.ipProto = quint8(buf[9]);
                    pkt.srcAddr = addr4Text((const quint8*)buf.data() + 12);
                    pkt.dstAddr = addr4Text((const quint8*)buf.data() + 16);
                } else if (ver == 6 && n >= 40) {
                    pkt.ipProto = quint8(buf[6]);
                    pkt.srcAddr = addr6Text((const quint8*)buf.data() + 8);
                    pkt.dstAddr = addr6Text((const quint8*)buf.data() + 24);
                } else {
                    continue;
                }
                quint16 sp = 0, dp = 0;
                PacketDecode::transportPayload(pkt.data, pkt.ipProto, &sp, &dp, nullptr, nullptr);
                pkt.srcPort = sp; pkt.dstPort = dp;

                if (pkt.ipProto == 6) m_tcp++; else if (pkt.ipProto == 17) m_udp++;
                else if (pkt.ipProto == 1 || pkt.ipProto == 58) m_icmp++;
                m_bytes += n;

                QMutexLocker lock(&m_mutex);
                pkt.seq = ++m_seq;
                if (m_queue.size() < 20000) m_queue.push_back(pkt);
                if (m_history.size() >= 20000) m_history.remove(0, 5000);
                m_history.append(pkt);
                m_total++;
            }
        }
        for (SOCKET s : m_socks) closesocket(s);
        m_socks.clear();
    }

    AdapterEntry m_adapter;
    std::vector<SOCKET> m_socks;
    std::thread m_thread;
    std::atomic_bool m_running{ false };
    QMutex m_mutex;
    std::deque<CapturedPacket> m_queue;
    QVector<CapturedPacket> m_history;
    std::atomic<quint64> m_seq{ 0 }, m_total{ 0 }, m_tcp{ 0 }, m_udp{ 0 }, m_icmp{ 0 }, m_bytes{ 0 };
};

// ============================================================================
// 页面
// ============================================================================

// PID -> 进程名
static QString pidToName(quint32 pid)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return QString();
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    QString result;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                result = QString::fromWCharArray(pe.szExeFile);
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

enum NetColumn {
    NC_PROTO = 0,
    NC_LOCAL,
    NC_REMOTE,
    NC_STATE,
    NC_PID,
    NC_PROCNAME,
    NC_Count
};

enum PktColumn {
    PC_NO = 0,
    PC_TIME,
    PC_PROTO,
    PC_SRC,
    PC_DST,
    PC_PID,
    PC_PROC,
    PC_LEN,
    PC_INFO,
    PC_Count
};

NetMonitorPage::NetMonitorPage(QWidget* parent) : QWidget(parent)
{
    m_engine.reset(new CaptureEngine());
    setupUi();
    refreshConnections();
    refreshAdapters();
    refreshProcessList();

    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(200);
    connect(m_drainTimer, &QTimer::timeout, this, &NetMonitorPage::onDrainPackets);
    m_drainTimer->start();
}

NetMonitorPage::~NetMonitorPage() = default;

void NetMonitorPage::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    m_rootTabs = new QTabWidget(this);
    layout->addWidget(m_rootTabs);
    setupCaptureTab();
    setupConnectionTab();
}

// ---------------------------------------------------------------------------
// Tab: 数据包捕获
// ---------------------------------------------------------------------------
void NetMonitorPage::setupCaptureTab()
{
    auto* tab = new QWidget(this);
    m_rootTabs->addTab(tab, QStringLiteral("数据包捕获"));
    auto* layout = new QHBoxLayout(tab);

    // ---- 左侧: 进程多选
    auto* leftBox = new QVBoxLayout();
    leftBox->addWidget(new QLabel(QStringLiteral("监视进程（多选，空=全部）"), tab));
    m_procSearchEdit = new QLineEdit(tab);
    m_procSearchEdit->setPlaceholderText(QStringLiteral("搜索进程..."));
    leftBox->addWidget(m_procSearchEdit);
    m_processList = new QListWidget(tab);
    leftBox->addWidget(m_processList, 1);
    auto* leftBtnRow = new QHBoxLayout();
    m_procRefreshBtn = new QPushButton(QStringLiteral("刷新进程"), tab);
    leftBtnRow->addWidget(m_procRefreshBtn);
    auto* checkAllBtn = new QPushButton(QStringLiteral("全选/清空"), tab);
    leftBtnRow->addWidget(checkAllBtn);
    leftBox->addLayout(leftBtnRow);
    auto* leftWidget = new QWidget(tab);
    leftWidget->setLayout(leftBox);
    leftWidget->setMaximumWidth(240);
    layout->addWidget(leftWidget);

    // ---- 右侧
    auto* rightBox = new QVBoxLayout();

    auto* toolRow = new QHBoxLayout();
    m_adapterCombo = new QComboBox(tab);
    m_adapterCombo->setMinimumWidth(180);
    toolRow->addWidget(new QLabel(QStringLiteral("网卡:"), tab));
    toolRow->addWidget(m_adapterCombo);
    m_captureBtn = new QPushButton(QStringLiteral("开始抓包"), tab);
    m_captureBtn->setMaximumHeight(26);
    toolRow->addWidget(m_captureBtn);
    m_pauseCheck = new QCheckBox(QStringLiteral("暂停显示"), tab);
    toolRow->addWidget(m_pauseCheck);
    m_clearBtn = new QPushButton(QStringLiteral("清空"), tab);
    m_clearBtn->setMaximumHeight(26);
    toolRow->addWidget(m_clearBtn);
    m_pcapExportBtn = new QPushButton(QStringLiteral("导出PCAP"), tab);
    m_pcapExportBtn->setMaximumHeight(26);
    toolRow->addWidget(m_pcapExportBtn);
    m_autoScrollCheck = new QCheckBox(QStringLiteral("自动滚动"), tab);
    m_autoScrollCheck->setChecked(true);
    toolRow->addWidget(m_autoScrollCheck);
    m_pktFilterEdit = new QLineEdit(tab);
    m_pktFilterEdit->setPlaceholderText(QStringLiteral("按 IP/端口/进程/概要 过滤"));
    m_pktFilterEdit->setMaximumWidth(220);
    toolRow->addWidget(m_pktFilterEdit);
    toolRow->addStretch();
    rightBox->addLayout(toolRow);

    m_pktStatsLabel = new QLabel(QStringLiteral("未开始抓包"), tab);
    m_pktStatsLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightBox->addWidget(m_pktStatsLabel);

    auto* vSplit = new QSplitter(Qt::Vertical, tab);
    m_pktTable = new QTableWidget(vSplit);
    m_pktTable->setColumnCount(PC_Count);
    m_pktTable->setHorizontalHeaderLabels({
        QStringLiteral("#"), QStringLiteral("时间"), QStringLiteral("协议"),
        QStringLiteral("源地址:端口"), QStringLiteral("目的地址:端口"), QStringLiteral("PID"),
        QStringLiteral("进程"), QStringLiteral("长度"), QStringLiteral("概要 (HTTP/DNS/TLS)") });
    m_pktTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pktTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pktTable->setAlternatingRowColors(true);
    m_pktTable->verticalHeader()->setVisible(false);
    m_pktTable->setColumnWidth(PC_NO, 64);
    m_pktTable->setColumnWidth(PC_TIME, 92);
    m_pktTable->setColumnWidth(PC_PROTO, 72);
    m_pktTable->setColumnWidth(PC_SRC, 170);
    m_pktTable->setColumnWidth(PC_DST, 170);
    m_pktTable->setColumnWidth(PC_PID, 64);
    m_pktTable->setColumnWidth(PC_PROC, 110);
    m_pktTable->setColumnWidth(PC_LEN, 64);
    m_pktTable->horizontalHeader()->setStretchLastSection(true);
    m_pktTable->setSortingEnabled(false);

    m_detailTabs = new QTabWidget(vSplit);
    m_decodeTree = new QTreeWidget(m_detailTabs);
    m_decodeTree->setHeaderLabels({ QStringLiteral("字段"), QStringLiteral("值") });
    m_decodeTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_hexView = new QPlainTextEdit(m_detailTabs);
    m_hexView->setReadOnly(true);
    m_hexView->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_asciiView = new QPlainTextEdit(m_detailTabs);
    m_asciiView->setReadOnly(true);
    m_asciiView->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_detailTabs->addTab(m_decodeTree, QStringLiteral("分层解析"));
    m_detailTabs->addTab(m_hexView, QStringLiteral("十六进制"));
    m_detailTabs->addTab(m_asciiView, QStringLiteral("ASCII"));
    m_detailTabs->setMinimumHeight(140);

    vSplit->addWidget(m_pktTable);
    vSplit->addWidget(m_detailTabs);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 1);
    rightBox->addWidget(vSplit, 1);

    auto* hint = new QLabel(
        QStringLiteral("原始套接字抓包 (SIO_RCVALL)，需要管理员权限；回环(localhost)流量不可见。"
                       "通过连接表把数据包归属到进程；HTTPS 内容加密，仅能显示 TLS SNI 域名。"), tab);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    rightBox->addWidget(hint);

    auto* rightWidget = new QWidget(tab);
    rightWidget->setLayout(rightBox);
    layout->addWidget(rightWidget, 1);

    connect(m_captureBtn, &QPushButton::clicked, this, &NetMonitorPage::onStartStopCapture);
    connect(m_clearBtn, &QPushButton::clicked, this, &NetMonitorPage::onClearPackets);
    connect(m_pcapExportBtn, &QPushButton::clicked, this, &NetMonitorPage::onExportPcap);
    connect(m_procRefreshBtn, &QPushButton::clicked, this, &NetMonitorPage::refreshProcessList);
    connect(m_procSearchEdit, &QLineEdit::textChanged, this, [this] {
        const QString q = m_procSearchEdit->text().trimmed();
        for (int i = 0; i < m_processList->count(); ++i) {
            auto* it = m_processList->item(i);
            it->setHidden(!q.isEmpty() && !it->text().contains(q, Qt::CaseInsensitive));
        }
    });
    connect(m_processList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { m_pktTable->setRowCount(0); });
    connect(m_pktFilterEdit, &QLineEdit::textChanged, this, [this] { m_pktTable->setRowCount(0); });
    connect(m_pktTable, &QTableWidget::itemSelectionChanged, this, &NetMonitorPage::onPacketSelected);
    connect(m_adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_engine->running()) onStartStopCapture();   // 换网卡 = 停止
    });
}

// ---------------------------------------------------------------------------
// Tab: 连接列表（原功能）
// ---------------------------------------------------------------------------
void NetMonitorPage::setupConnectionTab()
{
    auto* tab = new QWidget(this);
    m_rootTabs->addTab(tab, QStringLiteral("连接列表"));
    auto* layout = new QVBoxLayout(tab);

    auto* toolRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton(QStringLiteral("刷新"), tab);
    connect(m_refreshBtn, &QPushButton::clicked, this, &NetMonitorPage::refreshConnections);
    m_udpCheck = new QCheckBox(QStringLiteral("包含 UDP"), tab);
    m_udpCheck->setChecked(true);
    connect(m_udpCheck, &QCheckBox::toggled, this, &NetMonitorPage::refreshConnections);
    m_autoCheck = new QCheckBox(QStringLiteral("自动刷新 (2s)"), tab);
    connect(m_autoCheck, &QCheckBox::toggled, this, &NetMonitorPage::onAutoRefreshToggled);
    m_filterEdit = new QLineEdit(tab);
    m_filterEdit->setPlaceholderText(QStringLiteral("按 进程名/PID/地址/端口 过滤"));
    m_filterEdit->setMaximumWidth(260);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &NetMonitorPage::refreshConnections);
    m_capturePidEdit = new QLineEdit(tab);
    m_capturePidEdit->setPlaceholderText(QStringLiteral("仅显示 PID（逗号分隔，留空=全部）"));
    m_capturePidEdit->setMaximumWidth(210);
    connect(m_capturePidEdit, &QLineEdit::textChanged, this, &NetMonitorPage::refreshConnections);

    toolRow->addWidget(m_refreshBtn);
    toolRow->addWidget(m_udpCheck);
    toolRow->addWidget(m_autoCheck);
    toolRow->addWidget(m_filterEdit);
    toolRow->addWidget(m_capturePidEdit);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    auto* hint = new QLabel(
        QStringLiteral("全局 TCP/UDP 连接监控 (GetExtendedTcpTable/GetExtendedUdpTable)。"
                       "右键可结束占用连接的进程。"), tab);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    m_table = new QTableWidget(tab);
    m_table->setColumnCount(NC_Count);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("协议"), QStringLiteral("本地地址:端口"), QStringLiteral("远程地址:端口"),
        QStringLiteral("状态"), QStringLiteral("PID"), QStringLiteral("进程名") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &NetMonitorPage::onContextMenu);
    layout->addWidget(m_table, 1);

    m_statusLabel = new QLabel(QStringLiteral("就绪"), tab);
    layout->addWidget(m_statusLabel);

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &NetMonitorPage::refreshConnections);

    m_contextMenu = new QMenu(this);
    connect(m_contextMenu->addAction(QStringLiteral("断开连接")), &QAction::triggered, this, &NetMonitorPage::onDisconnectConnection);
    connect(m_contextMenu->addAction(QStringLiteral("结束该进程")), &QAction::triggered, this, &NetMonitorPage::onKillProcess);
    m_contextMenu->addSeparator();
    connect(m_contextMenu->addAction(QStringLiteral("复制整行")), &QAction::triggered, this, &NetMonitorPage::onCopyRow);
}

void NetMonitorPage::refreshAdapters()
{
    m_adapterCombo->blockSignals(true);
    m_adapterCombo->clear();
    g_netAdapters = CaptureEngine::enumAdapters();
    for (int i = 0; i < g_netAdapters.size(); ++i) {
        const auto& a = g_netAdapters[i];
        QString label = a.name + QStringLiteral("  [");
        if (!a.ip.isEmpty()) label += a.ip;
        if (!a.ip6.isEmpty()) label += QStringLiteral(" / ") + a.ip6;
        label += QChar(']');
        m_adapterCombo->addItem(label);
        m_adapterCombo->setItemData(m_adapterCombo->count() - 1, i);
    }
    if (g_netAdapters.isEmpty()) m_adapterCombo->addItem(QStringLiteral("(未找到网卡)"));
    m_adapterCombo->blockSignals(false);
}

void NetMonitorPage::refreshProcessList()
{
    QSet<quint32> checked;
    for (int i = 0; i < m_processList->count(); ++i)
        if (m_processList->item(i)->checkState() == Qt::Checked)
            checked.insert(quint32(m_processList->item(i)->data(Qt::UserRole).toUInt()));

    m_processList->blockSignals(true);
    m_processList->clear();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                auto* it = new QListWidgetItem(QString::fromWCharArray(pe.szExeFile), m_processList);
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
                it->setCheckState(checked.contains(pe.th32ProcessID) ? Qt::Checked : Qt::Unchecked);
                it->setData(Qt::UserRole, QVariant(static_cast<uint>(pe.th32ProcessID)));
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    m_processList->blockSignals(false);
}

static QSet<quint32> netSelectedPids(const QListWidget* list)
{
    QSet<quint32> out;
    if (!list) return out;
    for (int i = 0; i < list->count(); ++i) {
        auto* it = list->item(i);
        if (it->checkState() == Qt::Checked) out.insert(quint32(it->data(Qt::UserRole).toUInt()));
    }
    return out;
}

void NetMonitorPage::onStartStopCapture()
{
    if (m_engine->running()) {
        m_engine->stop();
        m_captureBtn->setText(QStringLiteral("开始抓包"));
        refreshAdapters();
        m_pktStatsLabel->setText(QStringLiteral("抓包已停止 (共捕获 %1 包)").arg(m_engine->total()));
        return;
    }
    const int adapterIdx = m_adapterCombo->currentData().toInt();
    if (adapterIdx < 0 || adapterIdx >= g_netAdapters.size()) {
        QMessageBox::warning(this, QStringLiteral("抓包"), QStringLiteral("未找到可用网卡。"));
        return;
    }
    const AdapterEntry& adapter = g_netAdapters[adapterIdx];
    const bool ok = m_engine->start(adapter);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("抓包"), QStringLiteral("启动抓包线程失败。"));
        return;
    }
    m_captureBtn->setText(QStringLiteral("停止抓包"));
    m_pktStatsLabel->setText(QStringLiteral("正在抓包... (%1)").arg(adapter.name));
}

void NetMonitorPage::onDrainPackets()
{
    const bool running = m_engine->running();
    QVector<CapturedPacket> packets;
    m_engine->drain(packets);

    // PID 归属: 用连接表匹配（每 tick 刷新一次连接表）
    const bool needMatch = !packets.isEmpty();
    static QVector<NetConnInfo> connTable;
    static qint64 lastTableRefresh = 0;
    if (needMatch && QDateTime::currentMSecsSinceEpoch() - lastTableRefresh > 800) {
        connTable = enumNetConnections(0, true);
        lastTableRefresh = QDateTime::currentMSecsSinceEpoch();
    }
    QHash<quint32, QString> nameCache;

    if (running || !m_pauseCheck->isChecked()) {
        const QSet<quint32> selectedPids = netSelectedPids(m_processList);
        const QString filter = m_pktFilterEdit->text().trimmed();
        int added = 0;
        for (auto& pkt : packets) {
            // PID 归属（TCP 双向匹配 / UDP 端点匹配）
            for (const auto& c : connTable) {
                if (c.proto == 6) {
                    const bool a = c.localAddr == pkt.srcAddr && c.localPort == pkt.srcPort && c.remoteAddr == pkt.dstAddr && c.remotePort == pkt.dstPort;
                    const bool b = c.localAddr == pkt.dstAddr && c.localPort == pkt.dstPort && c.remoteAddr == pkt.srcAddr && c.remotePort == pkt.srcPort;
                    if (a || b) { pkt.pid = c.pid; break; }
                } else if (c.proto == 17) {
                    if ((c.localAddr == pkt.srcAddr && c.localPort == pkt.srcPort) ||
                        (c.localAddr == pkt.dstAddr && c.localPort == pkt.dstPort)) { pkt.pid = c.pid; break; }
                }
            }
            if (pkt.pid && !nameCache.contains(pkt.pid)) nameCache.insert(pkt.pid, pidToName(pkt.pid));
            pkt.procName = pkt.pid ? nameCache.value(pkt.pid) : QString();

            // 显示过滤
            if (!selectedPids.isEmpty() && (!pkt.pid || !selectedPids.contains(pkt.pid))) continue;
            if (!filter.isEmpty()) {
                const QString hay = QStringLiteral("%1 %2 %3 %4 %5 %6")
                    .arg(pkt.srcAddr).arg(pkt.dstAddr).arg(pkt.srcPort).arg(pkt.dstPort)
                    .arg(pkt.procName).arg(pkt.pid);
                if (!hay.contains(filter, Qt::CaseInsensitive)) continue;
            }
            if (m_pauseCheck->isChecked()) continue;

            const int r = m_pktTable->rowCount();
            m_pktTable->insertRow(r);
            auto* noItem = new QTableWidgetItem(QString::number(pkt.seq));
            noItem->setData(Qt::UserRole, QVariant::fromValue<quint64>(pkt.seq));
            m_pktTable->setItem(r, PC_NO, noItem);
            m_pktTable->setItem(r, PC_TIME, new QTableWidgetItem(
                QDateTime::fromMSecsSinceEpoch(pkt.tsMs).toString(QStringLiteral("HH:mm:ss.zzz"))));
            const char* pn = PacketDecode::ipProtoName(pkt.ipProto);
            m_pktTable->setItem(r, PC_PROTO, new QTableWidgetItem(
                pn ? QString::fromLatin1(pn) : QStringLiteral("IP(%1)").arg(pkt.ipProto)));
            m_pktTable->setItem(r, PC_SRC, new QTableWidgetItem(pkt.srcPort
                ? QStringLiteral("%1:%2").arg(pkt.srcAddr).arg(pkt.srcPort) : pkt.srcAddr));
            m_pktTable->setItem(r, PC_DST, new QTableWidgetItem(pkt.dstPort
                ? QStringLiteral("%1:%2").arg(pkt.dstAddr).arg(pkt.dstPort) : pkt.dstAddr));
            m_pktTable->setItem(r, PC_PID, new QTableWidgetItem(pkt.pid ? QString::number(pkt.pid) : QStringLiteral("-")));
            m_pktTable->setItem(r, PC_PROC, new QTableWidgetItem(pkt.procName));
            m_pktTable->setItem(r, PC_LEN, new QTableWidgetItem(QString::number(pkt.len)));

            // 概要: 应用层信息或传输层信息
            quint32 seq = 0; quint8 flags = 0;
            const QByteArray payload = PacketDecode::transportPayload(pkt.data, pkt.ipProto, nullptr, nullptr, &seq, &flags);
            QString info = PacketDecode::appSummary(pkt.srcPort, pkt.dstPort, payload);
            if (info.isEmpty() && pkt.ipProto == 6 && flags) {
                QString fl;
                PacketDecode::tcpFlagsText(flags, fl);
                info = QStringLiteral("[%1] Seq=%2").arg(fl).arg(seq);
            }
            if (info.isEmpty() && (pkt.ipProto == 1 || pkt.ipProto == 58) && !payload.isEmpty())
                info = QStringLiteral("ICMP type=%1 code=%2").arg(quint8(payload[0])).arg(quint8(payload[1]));
            m_pktTable->setItem(r, PC_INFO, new QTableWidgetItem(info));

            // 着色: 选中进程绿色, HTTP/TLS/DNS 蓝色
            QColor color;
            if (!selectedPids.isEmpty() && selectedPids.contains(pkt.pid)) color = QColor(210, 245, 210);
            else if (info.startsWith(QStringLiteral("HTTP")) || info.startsWith(QStringLiteral("TLS")) || info.startsWith(QStringLiteral("DNS")))
                color = QColor(215, 232, 252);
            if (color.isValid())
                for (int cIdx = 0; cIdx < PC_Count; ++cIdx)
                    if (m_pktTable->item(r, cIdx)) m_pktTable->item(r, cIdx)->setBackground(QBrush(color));
            ++added;

            if (m_pktTable->rowCount() > 5000) m_pktTable->removeRow(0);
        }
        if (added && m_autoScrollCheck->isChecked() && !m_pauseCheck->isChecked())
            m_pktTable->scrollToBottom();
    }

    // 统计
    if (running) {
        static quint64 lastBytes = 0;
        static qint64 lastTick = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const quint64 bps = lastTick && now > lastTick ? quint64(double(m_engine->byteCount() - lastBytes) * 1000.0 / double(now - lastTick)) : 0;
        lastBytes = m_engine->byteCount();
        lastTick = now;
        m_pktStatsLabel->setText(QStringLiteral("正在抓包: 总计 %1 包 (TCP %2 / UDP %3 / ICMP %4)  共 %5 字节  ≈ %6 B/s")
            .arg(m_engine->total()).arg(m_engine->tcpCount()).arg(m_engine->udpCount())
            .arg(m_engine->icmpCount()).arg(m_engine->byteCount()).arg(bps));
    }
}

void NetMonitorPage::onPacketSelected()
{
    const int row = m_pktTable->currentRow();
    if (row < 0) return;
    auto* it = m_pktTable->item(row, PC_NO);
    if (!it) return;
    const quint64 seq = it->data(Qt::UserRole).toULongLong();
    QVector<CapturedPacket> hist;
    m_engine->snapshotHistory(hist);
    for (const auto& pkt : hist) {
        if (pkt.seq == seq) { showPacketDetail(pkt); return; }
    }
}

void NetMonitorPage::showPacketDetail(const CapturedPacket& pkt)
{
    PacketDecode::buildTree(m_decodeTree, pkt.data);
    m_hexView->setPlainText(PacketDecode::hexDump(pkt.data));
    m_asciiView->setPlainText(PacketDecode::asciiDump(pkt.data));
}

void NetMonitorPage::onClearPackets()
{
    m_pktTable->setRowCount(0);
    m_decodeTree->clear();
    m_hexView->clear();
    m_asciiView->clear();
}

void NetMonitorPage::onExportPcap()
{
    QVector<CapturedPacket> hist;
    m_engine->snapshotHistory(hist);
    if (hist.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("没有可导出的数据包。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 PCAP"),
        QStringLiteral("efch_capture.pcap"), QStringLiteral("PCAP (*.pcap)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("导出"), QStringLiteral("无法创建文件: %1").arg(path));
        return;
    }
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    // pcap 全局头 (linktype = 101, LINKTYPE_RAW, 直接 IP 包)
    out << quint32(0xa1b2c3d4) << quint16(2) << quint16(4) << qint32(0) << quint32(0)
        << quint32(65535) << quint32(101);
    for (const auto& pkt : hist) {
        const qint64 sec = pkt.tsMs / 1000;
        const qint64 usec = (pkt.tsMs % 1000) * 1000;
        out << quint32(sec) << quint32(usec) << quint32(pkt.data.size()) << quint32(pkt.len);
        f.write(pkt.data);
    }
    f.close();
    QMessageBox::information(this, QStringLiteral("导出"),
        QStringLiteral("已导出 %1 个数据包到:\n%2\n(linktype=RAW, 可用 Wireshark 打开)").arg(hist.size()).arg(path));
}

// ---------------------------------------------------------------------------
// 连接列表（原功能）
// ---------------------------------------------------------------------------
void NetMonitorPage::refreshConnections()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    bool includeUdp = m_udpCheck ? m_udpCheck->isChecked() : true;
    m_conns = enumNetConnections(0, includeUdp);

    QHash<quint32, QString> nameMap;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                nameMap.insert(pe.th32ProcessID, QString::fromWCharArray(pe.szExeFile));
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    QString filter = m_filterEdit ? m_filterEdit->text().trimmed() : QString();
    QSet<quint32> capturePids;
    if (m_capturePidEdit) {
        for (const QString& token : m_capturePidEdit->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
            bool ok = false; const quint32 pid = token.toUInt(&ok); if (ok && pid) capturePids.insert(pid);
        }
    }
    int shown = 0;
    for (int ci = 0; ci < m_conns.size(); ++ci) {
        const NetConnInfo& c = m_conns[ci];
        if (!capturePids.isEmpty() && !capturePids.contains(c.pid)) continue;
        QString procName = nameMap.value(c.pid);
        if (!filter.isEmpty()) {
            bool match = procName.contains(filter, Qt::CaseInsensitive)
                || QString::number(c.pid).contains(filter)
                || c.localAddr.contains(filter)
                || c.remoteAddr.contains(filter)
                || QString::number(c.localPort).contains(filter)
                || QString::number(c.remotePort).contains(filter);
            if (!match) continue;
        }
        int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, NC_PROTO, new QTableWidgetItem(
            c.proto == 6 ? QStringLiteral("TCP") : QStringLiteral("UDP")));
        m_table->setItem(r, NC_LOCAL, new QTableWidgetItem(
            QStringLiteral("%1:%2").arg(c.localAddr).arg(c.localPort)));
        m_table->setItem(r, NC_REMOTE, new QTableWidgetItem(
            c.proto == 6 ? QStringLiteral("%1:%2").arg(c.remoteAddr).arg(c.remotePort)
                         : QStringLiteral("*:*")));
        auto* stateItem = new QTableWidgetItem(
            c.proto == 6 ? tcpStateToString(c.state) : QStringLiteral("-"));
        if (c.proto == 6 && c.state == 5) {
            stateItem->setForeground(QBrush(Qt::darkGreen));
        }
        m_table->setItem(r, NC_STATE, stateItem);
        auto* pidItem = new QTableWidgetItem(QString::number(c.pid));
        pidItem->setData(Qt::UserRole, ci);
        m_table->setItem(r, NC_PID, pidItem);
        m_table->setItem(r, NC_PROCNAME, new QTableWidgetItem(procName));
        ++shown;
    }
    m_table->setSortingEnabled(true);
    m_statusLabel->setText(
        QStringLiteral("共 %1 条连接 (显示 %2)").arg(m_conns.size()).arg(shown));
}

void NetMonitorPage::onAutoRefreshToggled(bool on)
{
    if (on) m_timer->start();
    else m_timer->stop();
}

quint32 NetMonitorPage::currentPid() const
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_conns.size()) return 0;
    auto* pidItem = m_table->item(row, NC_PID);
    if (!pidItem) return 0;
    bool ok = false;
    quint32 pid = pidItem->text().toUInt(&ok);
    return ok ? pid : 0;
}

void NetMonitorPage::onContextMenu(const QPoint& pos)
{
    QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    m_contextMenu->exec(m_table->viewport()->mapToGlobal(pos));
}

void NetMonitorPage::onDisconnectConnection()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* pidItem = m_table->item(row, NC_PID);
    if (!pidItem) return;
    bool ok = false;
    int ci = pidItem->data(Qt::UserRole).toInt(&ok);
    if (!ok || ci < 0 || ci >= m_conns.size()) return;
    const NetConnInfo& c = m_conns[ci];

    if (c.proto != 6) {
        QMessageBox::information(this, QStringLiteral("网络监控"),
            QStringLiteral("UDP 无连接语义，无法断开。可右键选择 [结束该进程]。"));
        return;
    }

    auto ret = QMessageBox::warning(this, QStringLiteral("网络监控"),
        QStringLiteral("确定断开 TCP 连接吗?\n\n本地: %1:%2\n远程: %3:%4\n进程: %5 (PID=%6)")
            .arg(c.localAddr).arg(c.localPort)
            .arg(c.remoteAddr).arg(c.remotePort)
            .arg(pidToName(c.pid)).arg(c.pid),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    DWORD netErr = 0;
    if (closeNetConnection(c, &netErr)) {
        m_statusLabel->setText(QStringLiteral("连接已断开 (%1:%2 -> %3:%4)")
            .arg(c.localAddr).arg(c.localPort).arg(c.remoteAddr).arg(c.remotePort));
        refreshConnections();
    } else {
        QString hint;
        if (c.state == 2) {
            hint = QStringLiteral("\nLISTEN 状态的监听端点无法通过 SetTcpEntry 断开。");
        } else if (netErr == 5 || netErr == 317) {
            hint = QStringLiteral("\n权限不足 (错误 %1)，请以管理员身份运行。").arg(netErr);
        } else if (netErr != 0) {
            hint = QStringLiteral("\n系统错误码: %1").arg(netErr);
        }
        QMessageBox::warning(this, QStringLiteral("网络监控"),
            QStringLiteral("断开连接失败 (连接可能已释放或状态不允许)%1").arg(hint));
    }
}

void NetMonitorPage::onKillProcess()
{
    quint32 pid = currentPid();
    if (pid == 0) return;

    auto ret = QMessageBox::warning(this, QStringLiteral("网络监控"),
        QStringLiteral("确定要结束进程 PID=%1 (%2) 吗?")
            .arg(pid).arg(pidToName(pid)),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    auto& ctx = AppContext::instance();
    bool ok = false;
    if (ctx.driverLoaded()) {
        ok = ctx.driver().zwKillProcess(pid);
    }
    if (!ok) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) {
            ok = TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
    if (ok) {
        m_statusLabel->setText(QStringLiteral("已发送结束指令 (PID=%1)").arg(pid));
        refreshConnections();
    } else {
        QMessageBox::warning(this, QStringLiteral("网络监控"),
            QStringLiteral("结束进程失败 (PID=%1)").arg(pid));
    }
}

void NetMonitorPage::onCopyRow()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    QStringList parts;
    for (int c = 0; c < NC_Count; ++c) {
        auto* it = m_table->item(row, c);
        parts << (it ? it->text() : QString());
    }
    QGuiApplication::clipboard()->setText(parts.join(QStringLiteral("\t")));
}
