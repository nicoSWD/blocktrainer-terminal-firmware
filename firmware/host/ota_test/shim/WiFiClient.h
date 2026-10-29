// Host stand-in for WiFiClient: a plain TCP socket with the semantics ota.cpp relies on.
#pragma once
#include "Arduino.h"
#include <deque>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

class WiFiClient {
public:
    int fd = -1;
    std::deque<uint8_t> pre;            // body bytes that were read together with the headers

    ~WiFiClient() { stop(); }
    void stop()
    {
        if (fd >= 0) close(fd);
        fd = -1;
        pre.clear();
    }
    // bytes that can be read without waiting
    int available()
    {
        int n = (int)pre.size();
        if (fd >= 0) {
            int k = 0;
            if (ioctl(fd, FIONREAD, &k) == 0) n += k;
        }
        return n;
    }
    // like the real class: open until the peer has closed AND everything was read
    bool connected()
    {
        if (!pre.empty()) return true;
        if (fd < 0) return false;
        uint8_t b;
        ssize_t r = recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
        if (r > 0) return true;
        if (r == 0) return false;
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
    size_t readBytes(uint8_t *buf, size_t len)
    {
        size_t n = 0;
        while (n < len && !pre.empty()) { buf[n++] = pre.front(); pre.pop_front(); }
        if (n < len && fd >= 0) {
            ssize_t r = recv(fd, buf + n, len - n, MSG_DONTWAIT);
            if (r > 0) n += r;
        }
        return n;
    }
};
