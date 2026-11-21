// ============================================================================
//  LiteEngine networking - tiny UDP "presence" layer for 2-player multiplayer.
//  One player hosts (port 27015), the other joins by IP. Each side sends its
//  player state ~20 times a second and draws the other player in its own world.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#ifdef _WIN32
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET sock_t;
typedef int sockaddr_len_t;
#define LITE_BADSOCK INVALID_SOCKET
#define LITE_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
typedef socklen_t sockaddr_len_t;
#define LITE_BADSOCK (-1)
#define LITE_CLOSE close
#endif

namespace lite {
namespace net {

const uint16_t PORT = 27015;
const uint32_t MAGIC = 0x4C434954u;  // "LCIT"

#pragma pack(push, 1)
struct Packet {
    uint32_t magic;
    uint8_t ch;      // character (0 = John, 1 = Jason)
    uint8_t inCar;   // 1 when driving
    uint8_t carType;
    uint8_t pad;
    float x, z, heading, speed, phase;
    float cr, cg, cb;  // car colour
};
#pragma pack(pop)

struct Remote {
    bool seen = false;
    int ch = 0;
    bool inCar = false;
    int carType = 0;
    float x = 0, z = 0, heading = 0, speed = 0, phase = 0, cr = 1, cg = 1, cb = 1;
    float age = 0;  // seconds since last packet
};

struct Net {
    sock_t s = LITE_BADSOCK;
    bool active = false, host = false;
    sockaddr_in peer;
    bool havePeer = false;
    Remote remote;
    float sendTimer = 0;
    std::string status;
};
inline Net& N() { static Net n; return n; }

inline bool startup() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    return true;
#endif
}
inline void setNonBlocking(sock_t s) {
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

inline void stop() {
    Net& n = N();
    if (n.s != LITE_BADSOCK) LITE_CLOSE(n.s);
    n.s = LITE_BADSOCK;
    n.active = false; n.havePeer = false; n.remote = Remote();
}

inline bool open(bool host, const std::string& ip, uint16_t port = PORT) {
    Net& n = N();
    stop();
    if (!startup()) { n.status = "Network start-up failed"; return false; }
    n.s = (sock_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (n.s == LITE_BADSOCK) { n.status = "Could not create socket"; return false; }
    setNonBlocking(n.s);
    n.host = host;
    memset(&n.peer, 0, sizeof(n.peer));
    if (host) {
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port);
        if (bind(n.s, (sockaddr*)&a, sizeof(a)) != 0) { n.status = "Could not host (port in use?)"; stop(); return false; }
        n.status = "Hosting on port " + std::to_string((int)port) + " - waiting for Player 2...";
    } else {
        n.peer.sin_family = AF_INET;
        n.peer.sin_port = htons(port);
        n.peer.sin_addr.s_addr = inet_addr(ip.c_str());
        if (n.peer.sin_addr.s_addr == INADDR_NONE) { n.status = "That IP address is not valid"; stop(); return false; }
        n.havePeer = true;
        n.status = "Joining " + ip + " ...";
    }
    n.active = true;
    return true;
}

// Call every frame with the local state; sends at ~20 Hz and receives everything pending.
inline void tick(float dt, const Packet& local) {
    Net& n = N();
    if (!n.active) return;
    n.remote.age += dt;
    n.sendTimer -= dt;
    if (n.sendTimer <= 0 && n.havePeer) {
        n.sendTimer = 0.05f;
        Packet p = local;
        p.magic = MAGIC;
        sendto(n.s, (const char*)&p, (int)sizeof(p), 0, (sockaddr*)&n.peer, sizeof(n.peer));
    }
    for (int i = 0; i < 32; i++) {
        Packet p;
        sockaddr_in from;
        sockaddr_len_t fl = sizeof(from);
        int r = (int)recvfrom(n.s, (char*)&p, (int)sizeof(p), 0, (sockaddr*)&from, &fl);
        if (r != (int)sizeof(p)) break;
        if (p.magic != MAGIC) continue;
        if (n.host && !n.havePeer) { n.peer = from; n.havePeer = true; }
        Remote& rm = n.remote;
        rm.seen = true; rm.age = 0;
        rm.ch = p.ch; rm.inCar = p.inCar != 0; rm.carType = p.carType;
        rm.x = p.x; rm.z = p.z; rm.heading = p.heading; rm.speed = p.speed; rm.phase = p.phase;
        rm.cr = p.cr; rm.cg = p.cg; rm.cb = p.cb;
        n.status = n.host ? "Player 2 connected" : "Connected to host";
    }
    if (n.remote.seen && n.remote.age > 4.f) { n.remote.seen = false; n.status = "Connection lost - waiting..."; }
}

}  // namespace net
}  // namespace lite
