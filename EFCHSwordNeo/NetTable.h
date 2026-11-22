#pragma once

// 网络连接枚举辅助（用户层 GetExtendedTcpTable / GetExtendedUdpTable）
// 供 进程属性窗口(单进程) 和 全局网络监控页 共用
// 注意: 本文件必须在 windows.h 之前包含 (winsock2.h 需先于 windows.h, 否则与 winsock.h 冲突)

#include <QString>
#include <QVector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

struct NetConnInfo {
    int     proto = 0;          // 6=TCP 17=UDP
    int     family = 0;         // AF_INET / AF_INET6
    QString localAddr;
    quint16 localPort = 0;
    QString remoteAddr;
    quint16 remotePort = 0;
    int     state = 0;          // MIB_TCP_STATE_*
    quint32 pid = 0;
    quint64 createTime = 0;     // 创建时间 FILETIME（仅 TCP OWNER_PID 有值）
    // 原始地址字节（网络字节序，断开连接时 SetTcpEntry/SetTcp6Entry 需要）
    quint32 localAddr4 = 0;
    quint32 remoteAddr4 = 0;
    quint8  localAddr6[16] = {0};
    quint8  remoteAddr6[16] = {0};
    quint32 localScopeId = 0;   // IPv6 scope id
    quint32 remoteScopeId = 0;
};

inline QString protoToString(int proto)
{
    return proto == 6 ? QStringLiteral("TCP") : QStringLiteral("UDP");
}

inline QString tcpStateToString(int state)
{
    switch (state) {
    case 1: return QStringLiteral("CLOSED");
    case 2: return QStringLiteral("LISTEN");
    case 3: return QStringLiteral("SYN_SENT");
    case 4: return QStringLiteral("SYN_RCVD");
    case 5: return QStringLiteral("ESTABLISHED");
    case 6: return QStringLiteral("FIN_WAIT1");
    case 7: return QStringLiteral("FIN_WAIT2");
    case 8: return QStringLiteral("CLOSE_WAIT");
    case 9: return QStringLiteral("CLOSING");
    case 10: return QStringLiteral("LAST_ACK");
    case 11: return QStringLiteral("TIME_WAIT");
    case 12: return QStringLiteral("DELETE_TCB");
    default: return QString::number(state);
    }
}

// 枚举网络连接。filterPid==0 返回全部，includeUdp 控制是否包含 UDP
inline QVector<NetConnInfo> enumNetConnections(quint32 filterPid, bool includeUdp)
{
    QVector<NetConnInfo> result;
    DWORD size = 0;

    // ---- TCP ----
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                             TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER) {
        QByteArray buf((int)size, Qt::Uninitialized);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCPROW_OWNER_PID& row = table->table[i];
                if (filterPid != 0 && row.dwOwningPid != filterPid) continue;
                NetConnInfo info;
                info.proto = 6;
                info.family = AF_INET;
                info.localAddr = QString::fromLatin1(inet_ntoa(*(in_addr*)&row.dwLocalAddr));
                info.localPort = ntohs((u_short)row.dwLocalPort);
                info.remoteAddr = QString::fromLatin1(inet_ntoa(*(in_addr*)&row.dwRemoteAddr));
                info.remotePort = ntohs((u_short)row.dwRemotePort);
                info.state = (int)row.dwState;
                info.pid = row.dwOwningPid;
                info.localAddr4 = row.dwLocalAddr;
                info.remoteAddr4 = row.dwRemoteAddr;
                result.append(info);
            }
        }
    }

    // ---- TCPv6 ----
    size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6,
                             TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER) {
        QByteArray buf((int)size, Qt::Uninitialized);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET6,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const MIB_TCP6ROW_OWNER_PID& row = table->table[i];
                if (filterPid != 0 && row.dwOwningPid != filterPid) continue;
                NetConnInfo info;
                info.proto = 6;
                info.family = AF_INET6;
                char addrBuf[64] = {0};
                inet_ntop(AF_INET6, &row.ucLocalAddr, addrBuf, sizeof(addrBuf));
                info.localAddr = QString::fromLatin1(addrBuf);
                info.localPort = ntohs((u_short)row.dwLocalPort);
                inet_ntop(AF_INET6, &row.ucRemoteAddr, addrBuf, sizeof(addrBuf));
                info.remoteAddr = QString::fromLatin1(addrBuf);
                info.remotePort = ntohs((u_short)row.dwRemotePort);
                info.state = (int)row.dwState;
                info.pid = row.dwOwningPid;
                memcpy(info.localAddr6, row.ucLocalAddr, 16);
                memcpy(info.remoteAddr6, row.ucRemoteAddr, 16);
                info.localScopeId = row.dwLocalScopeId;
                info.remoteScopeId = row.dwRemoteScopeId;
                result.append(info);
            }
        }
    }

    // ---- UDP ----
    if (includeUdp) {
        size = 0;
        if (GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET,
                                 UDP_TABLE_OWNER_PID, 0) == ERROR_INSUFFICIENT_BUFFER) {
            QByteArray buf((int)size, Qt::Uninitialized);
            if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET,
                                    UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_UDPROW_OWNER_PID& row = table->table[i];
                    if (filterPid != 0 && row.dwOwningPid != filterPid) continue;
                    NetConnInfo info;
                    info.proto = 17;
                    info.localAddr = QString::fromLatin1(inet_ntoa(*(in_addr*)&row.dwLocalAddr));
                    info.localPort = ntohs((u_short)row.dwLocalPort);
                    info.pid = row.dwOwningPid;
                    result.append(info);
                }
            }
        }
        size = 0;
        if (GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET6,
                                 UDP_TABLE_OWNER_PID, 0) == ERROR_INSUFFICIENT_BUFFER) {
            QByteArray buf((int)size, Qt::Uninitialized);
            if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET6,
                                    UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_UDP6ROW_OWNER_PID& row = table->table[i];
                    if (filterPid != 0 && row.dwOwningPid != filterPid) continue;
                    NetConnInfo info;
                    info.proto = 17;
                    char addrBuf[64] = {0};
                    inet_ntop(AF_INET6, &row.ucLocalAddr, addrBuf, sizeof(addrBuf));
                    info.localAddr = QString::fromLatin1(addrBuf);
                    info.localPort = ntohs((u_short)row.dwLocalPort);
                    info.pid = row.dwOwningPid;
                    result.append(info);
                }
            }
        }
    }

    return result;
}

// 断开 TCP 连接（将状态设为 DELETE_TCB，需要管理员权限）
// 仅支持 TCP；UDP 无连接语义无法断开
// errOut 可选：接收 SetTcpEntry/SetTcp6Entry 的实际失败错误码，便于界面提示
inline bool closeNetConnection(const NetConnInfo& c, DWORD* errOut = nullptr)
{
    if (errOut) *errOut = 0;
    if (c.proto != 6) { if (errOut) *errOut = ERROR_NOT_SUPPORTED; return false; }

    // SetTcpEntry 在 Vista+ 上要求管理员令牌；对属于其他账户/SYSTEM 进程的
    // 连接，还需要 SeDebugPrivilege 才允许操作其 TCB。这里临时启用。
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LUID luid;
        if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
            TOKEN_PRIVILEGES tp;
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        }
        CloseHandle(hToken);
    }

    if (c.family == AF_INET) {
        MIB_TCPROW row;
        row.dwState = MIB_TCP_STATE_DELETE_TCB;
        row.dwLocalAddr = c.localAddr4;
        row.dwLocalPort = htons(c.localPort);
        row.dwRemoteAddr = c.remoteAddr4;
        row.dwRemotePort = htons(c.remotePort);
        DWORD err = SetTcpEntry(&row);
        if (errOut) *errOut = err;
        return err == NO_ERROR;
    }
    if (c.family == AF_INET6) {
        // SetTcp6Entry 在部分 SDK 头文件中未声明，动态加载
        using SetTcp6EntryFn = DWORD(WINAPI*)(PMIB_TCP6ROW);
        static SetTcp6EntryFn pSetTcp6Entry = []() -> SetTcp6EntryFn {
            HMODULE h = GetModuleHandleW(L"iphlpapi.dll");
            if (!h) h = LoadLibraryW(L"iphlpapi.dll");
            return h ? reinterpret_cast<SetTcp6EntryFn>(GetProcAddress(h, "SetTcp6Entry")) : nullptr;
        }();
        if (!pSetTcp6Entry) { if (errOut) *errOut = ERROR_NOT_SUPPORTED; return false; }

        MIB_TCP6ROW row;
        ZeroMemory(&row, sizeof(row));
        row.State = MIB_TCP_STATE_DELETE_TCB;
        memcpy(&row.LocalAddr, c.localAddr6, 16);
        row.dwLocalScopeId = c.localScopeId;
        row.dwLocalPort = htons(c.localPort);
        memcpy(&row.RemoteAddr, c.remoteAddr6, 16);
        row.dwRemoteScopeId = c.remoteScopeId;
        row.dwRemotePort = htons(c.remotePort);
        DWORD err = pSetTcp6Entry(&row);
        if (errOut) *errOut = err;
        return err == NO_ERROR;
    }
    return false;
}
