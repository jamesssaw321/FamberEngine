// Minimal non-blocking UDP over Winsock2. Build defines WIN32_LEAN_AND_MEAN
// so windows.h (already included elsewhere) does not drag in winsock 1.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

namespace net {

struct Addr {
    sockaddr_in sa{};
    bool operator==(const Addr& o) const {
        return sa.sin_addr.s_addr == o.sa.sin_addr.s_addr && sa.sin_port == o.sa.sin_port;
    }
};

inline bool initWSA() {
    static bool done = false;
    if (done) return true;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { printf("[net] WSAStartup failed\n"); return false; }
    done = true;
    return true;
}

struct Socket {
    SOCKET s = INVALID_SOCKET;

    bool open(unsigned short port) { // 0 = ephemeral
        if (!initWSA()) return false;
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) return false;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(port);
        if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            printf("[net] bind port %d failed\n", port);
            closesocket(s); s = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void close() { if (s != INVALID_SOCKET) closesocket(s); s = INVALID_SOCKET; }

    int send(const Addr& to, const void* d, int n) {
        return sendto(s, (const char*)d, n, 0, (const sockaddr*)&to.sa, sizeof(to.sa));
    }

    int recv(Addr& from, void* d, int cap) { // -1 = nothing pending
        int fl = sizeof(from.sa);
        int n = recvfrom(s, (char*)d, cap, 0, (sockaddr*)&from.sa, &fl);
        return n == SOCKET_ERROR ? -1 : n;
    }
};

inline bool resolve(const char* host, unsigned short port, Addr& out) {
    out.sa.sin_family = AF_INET;
    out.sa.sin_port = htons(port);
    out.sa.sin_addr.s_addr = inet_addr(host);
    if (out.sa.sin_addr.s_addr == INADDR_NONE) {
        hostent* he = gethostbyname(host);
        if (!he) { printf("[net] cannot resolve %s\n", host); return false; }
        memcpy(&out.sa.sin_addr, he->h_addr, 4);
    }
    return true;
}

} // namespace net
