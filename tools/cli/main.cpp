// SPDX-License-Identifier: Apache-2.0
//
// wearout-cli — desktop tool for poking at the Wearable protocol over TCP.
//
// The Wear OS emulator exposes the watch side of the Wearable transport on
// TCP port 5601 inside the emulator. Forward it with
//     adb -s emulator-5554 forward tcp:5601 tcp:5601
// and the same bytes that would flow over Bluetooth RFCOMM flow over TCP,
// so the protocol can be developed without Bluetooth or a Sailfish device.
//
// Modes:
//   wearout-cli connect [host:port]          act as the phone (default 127.0.0.1:5601)
//   wearout-cli proxy <listen-port> <host:port>
//                                            sit between a real phone and a watch,
//                                            forward bytes untouched, log decoded traffic
//   wearout-cli fake-watch [port]            minimal watch impersonator for self-tests
//
// Interactive commands in `connect` mode (type on stdin):
//   msg <path> <text>       send a MessageClient message to the watch
//   put <path> <text>       put a DataItem
//   app <package> <digest>  set the package/signature used for msg/put
//   items                   list DataItems received from the watch
//   quit
#include "wearout/framing.h"
#include "wearout/protobuf.h"
#include "wearout/session.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using namespace wearout;
using pb::Bytes;

namespace {

bool g_verbose = false;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void splitHostPort(const std::string &s, std::string &host, std::string &port) {
    auto c = s.rfind(':');
    if (c == std::string::npos) {
        host = "127.0.0.1";
        port = s;
    } else {
        host = s.substr(0, c);
        port = s.substr(c + 1);
    }
}

int dialTcp(const std::string &hostPort) {
    std::string host, port;
    splitHostPort(hostPort, host, port);
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        std::fprintf(stderr, "cannot resolve %s\n", hostPort.c_str());
        return -1;
    }
    int fd = -1;
    for (addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
    return fd;
}

int listenTcp(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, on purpose
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || listen(fd, 1) != 0) {
        std::perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

bool writeAll(int fd, const Bytes &b) {
    size_t off = 0;
    while (off < b.size()) {
        ssize_t n = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
        if (n <= 0)
            return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

std::string printable(const Bytes &b, size_t max = 120) {
    std::string s;
    for (size_t i = 0; i < b.size() && i < max; ++i)
        s += (b[i] >= 0x20 && b[i] < 0x7f) ? static_cast<char>(b[i]) : '.';
    if (b.size() > max)
        s += "...";
    return s;
}

void logMessage(const char *dir, const msg::RootMessage &m, const Bytes &raw) {
    std::printf("%s %s (%zu bytes)\n", dir, msg::kindName(m.kind()), raw.size());
    // Always dump things we don't model yet; dump everything with -v.
    bool interesting = g_verbose || m.kind() == msg::RootMessage::Kind::Unknown ||
                       m.kind() == msg::RootMessage::Kind::Connect ||
                       m.kind() == msg::RootMessage::Kind::SyncStart || !m.unknown.empty();
    if (interesting)
        std::printf("%s", pb::dump(raw, 1).c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------

int runConnect(const std::string &target) {
    int fd = dialTcp(target);
    if (fd < 0) {
        std::fprintf(stderr, "could not connect to %s (did you `adb forward tcp:5601 tcp:5601`?)\n",
                     target.c_str());
        return 1;
    }
    std::printf("connected to %s\n", target.c_str());

    std::string appPkg = "org.wearout.bridge";
    std::string appDigest;

    SessionConfig cfg;
    if (const char *v = std::getenv("WEAROUT_PROTO_VERSION"))
        cfg.protocolVersion = std::atoi(v);
    if (const char *v = std::getenv("WEAROUT_CONNECT_U4"))
        cfg.connectUnknown4 = std::atoi(v);
    cfg.localName = "WearOut CLI";

    bool alive = true;
    Session session(cfg, {
        [&](const Bytes &b) { if (!writeAll(fd, b)) alive = false; },
        [&](const SessionEvent &e) {
            switch (e.type) {
            case SessionEvent::Type::StateChanged:
                std::printf("-- state: %s\n", stateName(e.state));
                break;
            case SessionEvent::Type::PeerConnected:
                std::printf("-- peer connected\n");
                break;
            case SessionEvent::Type::MessageReceived:
                std::printf("-- message %s from %s for %s: %s\n", e.path.c_str(), e.sourceNodeId.c_str(),
                            e.packageName.c_str(), printable(e.data).c_str());
                break;
            case SessionEvent::Type::DataItemChanged:
                std::printf("-- data item %s (%s): %s\n", e.path.c_str(), e.packageName.c_str(),
                            printable(e.data).c_str());
                break;
            case SessionEvent::Type::Error:
                std::printf("-- error: %s\n", e.error.c_str());
                break;
            default:
                break;
            }
            std::fflush(stdout);
        },
        logMessage,
    });
    std::printf("local node id: %s\n", session.config().localNodeId.c_str());
    session.start(nowMs());

    pollfd fds[2] = {{fd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
    uint8_t buf[16384];
    std::string line;
    while (alive && session.state() != SessionState::Closed) {
        int r = poll(fds, 2, 1000);
        if (r < 0)
            break;
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n <= 0) {
                std::printf("-- remote closed\n");
                break;
            }
            session.onBytes(buf, static_cast<size_t>(n), nowMs());
        }
        if (fds[1].revents & POLLIN) {
            if (!std::getline(std::cin, line))
                break;
            std::istringstream in(line);
            std::string cmd, a, rest;
            in >> cmd >> a;
            std::getline(in, rest);
            if (!rest.empty() && rest[0] == ' ')
                rest.erase(0, 1);
            if (cmd == "quit" || cmd == "exit") {
                break;
            } else if (cmd == "msg" && !a.empty()) {
                int id = session.sendMessage(a, Bytes(rest.begin(), rest.end()), appPkg, appDigest);
                std::printf("-- sent request %d\n", id);
            } else if (cmd == "put" && !a.empty()) {
                session.putDataItem(a, Bytes(rest.begin(), rest.end()), appPkg, appDigest, nowMs());
            } else if (cmd == "app" && !a.empty()) {
                appPkg = a;
                appDigest = rest;
                std::printf("-- target app %s digest '%s'\n", appPkg.c_str(), appDigest.c_str());
            } else if (cmd == "items") {
                for (auto &kv : session.dataItems())
                    std::printf("   %s = %s\n", kv.first.c_str(),
                                printable(kv.second.data.value_or(Bytes{})).c_str());
            } else if (!cmd.empty()) {
                std::printf("commands: msg <path> <text> | put <path> <text> | app <pkg> <digest> | items | quit\n");
            }
            std::fflush(stdout);
        }
        session.tick(nowMs());
    }
    close(fd);
    return 0;
}

// ---------------------------------------------------------------------------

int runProxy(int listenPort, const std::string &target) {
    int lfd = listenTcp(listenPort);
    if (lfd < 0)
        return 1;
    std::printf("proxy: point the phone at 127.0.0.1:%d, forwarding to %s\n", listenPort, target.c_str());
    int client = accept(lfd, nullptr, nullptr);
    close(lfd);
    if (client < 0)
        return 1;
    int upstream = dialTcp(target);
    if (upstream < 0) {
        std::fprintf(stderr, "cannot reach %s\n", target.c_str());
        close(client);
        return 1;
    }
    std::printf("proxy: phone connected, upstream connected\n");

    StreamDecoder fromPhone, fromWatch;
    pollfd fds[2] = {{client, POLLIN, 0}, {upstream, POLLIN, 0}};
    uint8_t buf[16384];
    for (;;) {
        if (poll(fds, 2, -1) < 0)
            break;
        for (int i = 0; i < 2; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            ssize_t n = recv(fds[i].fd, buf, sizeof buf, 0);
            if (n <= 0) {
                std::printf("proxy: %s closed\n", i == 0 ? "phone" : "watch");
                close(client);
                close(upstream);
                return 0;
            }
            // Forward first, decode second: logging must never alter traffic.
            writeAll(i == 0 ? upstream : client, Bytes(buf, buf + n));
            StreamDecoder &dec = i == 0 ? fromPhone : fromWatch;
            try {
                dec.feed(buf, static_cast<size_t>(n));
                while (auto d = dec.next())
                    logMessage(i == 0 ? "phone>>watch" : "watch>>phone", d->message, d->raw);
            } catch (const std::exception &e) {
                std::printf("proxy: decode error (%s), continuing raw\n", e.what());
                dec = StreamDecoder();
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------

int runFakeWatch(int port) {
    int lfd = listenTcp(port);
    if (lfd < 0)
        return 1;
    std::printf("fake-watch listening on 127.0.0.1:%d\n", port);
    int fd = accept(lfd, nullptr, nullptr);
    close(lfd);
    if (fd < 0)
        return 1;

    StreamDecoder dec;
    uint8_t buf[16384];
    bool greeted = false;
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0)
            break;
        dec.feed(buf, static_cast<size_t>(n));
        while (auto d = dec.next()) {
            logMessage("phone>>", d->message, d->raw);
            if (d->message.connect && !greeted) {
                greeted = true;
                msg::Connect c;
                c.id = "fakew0tc";
                c.name = "WearOut fake watch";
                c.peerVersion = 2;
                c.peerMinimumVersion = 0;
                c.networkId = "fakew0tc";
                writeAll(fd, encodeMessage(msg::RootMessage::makeConnect(c), 1));
                msg::Request r;
                r.path = "/wearout/hello";
                r.packageName = "org.wearout.bridge";
                r.sourceNodeId = "fakew0tc";
                r.rawData = Bytes{'h', 'i'};
                writeAll(fd, encodeMessage(msg::RootMessage::makeRpc(r), 2));
            } else if (d->message.rpcRequest) {
                // Echo messages back so the round trip is visible.
                msg::Request echo = *d->message.rpcRequest;
                echo.path = "/wearout/echo" + echo.path.value_or("");
                echo.sourceNodeId = "fakew0tc";
                echo.targetNodeId = d->message.rpcRequest->sourceNodeId;
                writeAll(fd, encodeMessage(msg::RootMessage::makeRpc(echo), 3));
            }
        }
    }
    close(fd);
    return 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  wearout-cli [-v] connect [host:port]\n"
                 "  wearout-cli [-v] proxy <listen-port> <host:port>\n"
                 "  wearout-cli [-v] fake-watch [port]\n");
}

} // namespace

int main(int argc, char **argv) {
    int i = 1;
    if (i < argc && std::strcmp(argv[i], "-v") == 0) {
        g_verbose = true;
        ++i;
    }
    if (i >= argc) {
        usage();
        return 2;
    }
    std::string mode = argv[i++];
    if (mode == "connect")
        return runConnect(i < argc ? argv[i] : "127.0.0.1:5601");
    if (mode == "proxy" && i + 1 < argc)
        return runProxy(std::atoi(argv[i]), argv[i + 1]);
    if (mode == "fake-watch")
        return runFakeWatch(i < argc ? std::atoi(argv[i]) : 5601);
    usage();
    return 2;
}
