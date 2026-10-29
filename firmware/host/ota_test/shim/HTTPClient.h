// Host stand-in for HTTPClient (plain http:// only) over real sockets, following the behaviour of the
// Arduino class that ota.cpp depends on: status codes, Content-Length, redirects, read timeout.
#pragma once
#include "Arduino.h"
#include "WiFiClient.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_READ_TIMEOUT (-11)

class HTTPClient {
    String url_;
    uint32_t connect_timeout_ = 5000, timeout_ = 5000;
    followRedirects_t redirects_ = HTTPC_DISABLE_FOLLOW_REDIRECTS;
    WiFiClient client_;
    int size_ = -1;
    std::string user_agent_, extra_headers_;

    static double scale() { const char *e = getenv("HOST_TIMEOUT_SCALE"); return e ? atof(e) : 1.0; }
    static uint64_t now_ms() { return millis(); }

    bool open(const std::string &host, int port)
    {
        client_.stop();
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &a.sin_addr);
        if (connect(fd, (sockaddr *)&a, sizeof(a)) != 0) { close(fd); return false; }
        client_.fd = fd;
        return true;
    }

    // read one header block; returns the status code or a negative error. Idle timeout like the real class.
    int read_headers(std::string &location)
    {
        std::string head;
        uint64_t last = now_ms();
        while (head.find("\r\n\r\n") == std::string::npos) {
            pollfd p = { client_.fd, POLLIN, 0 };
            int pr = poll(&p, 1, 20);
            if (pr > 0) {
                char c;
                ssize_t r = recv(client_.fd, &c, 1, 0);
                if (r <= 0) return HTTPC_ERROR_CONNECTION_REFUSED;
                head += c;
                last = now_ms();
            } else if (now_ms() - last > (uint32_t)(timeout_ * scale())) {
                return HTTPC_ERROR_READ_TIMEOUT;
            }
        }
        int code = 0;
        sscanf(head.c_str(), "HTTP/%*s %d", &code);
        size_ = -1;
        size_t pos = head.find("Content-Length:");
        if (pos != std::string::npos) size_ = atoi(head.c_str() + pos + 15);
        pos = head.find("Location:");
        if (pos != std::string::npos) {
            size_t e = head.find("\r\n", pos);
            location = head.substr(pos + 9, e - pos - 9);
            while (!location.empty() && location[0] == ' ') location.erase(0, 1);
        }
        return code;
    }

public:
    bool begin(const String &url) { url_ = url; return true; }
    void setConnectTimeout(uint32_t ms) { connect_timeout_ = ms; }
    void setTimeout(uint32_t ms) { timeout_ = ms; }
    void setFollowRedirects(followRedirects_t f) { redirects_ = f; }
    void addHeader(const char *name, const char *value) { extra_headers_ += std::string(name) + ": " + value + "\r\n"; }
    void setUserAgent(const char *ua) { user_agent_ = ua; }

    int GET()
    {
        std::string u = url_.c_str();
        for (int hops = 0; hops < 10; hops++) {
            if (u.rfind("http://", 0) != 0) return HTTPC_ERROR_CONNECTION_REFUSED;
            std::string rest = u.substr(7);
            size_t slash = rest.find('/');
            std::string hostport = rest.substr(0, slash), path = slash == std::string::npos ? "/" : rest.substr(slash);
            size_t colon = hostport.find(':');
            std::string host = hostport.substr(0, colon);
            int port = colon == std::string::npos ? 80 : atoi(hostport.c_str() + colon + 1);
            if (!open(host, port)) return HTTPC_ERROR_CONNECTION_REFUSED;
            std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + hostport + "\r\n" +
                              (user_agent_.empty() ? "" : "User-Agent: " + user_agent_ + "\r\n") + extra_headers_ +
                              "Connection: close\r\n\r\n";
            send(client_.fd, req.data(), req.size(), 0);
            std::string location;
            int code = read_headers(location);
            bool redirect = code == 301 || code == 302 || code == 307 || code == 308;
            if (redirect && redirects_ != HTTPC_DISABLE_FOLLOW_REDIRECTS && !location.empty()) {
                u = location[0] == '/' ? "http://" + hostport + location : location;
                continue;
            }
            return code;
        }
        return HTTPC_ERROR_CONNECTION_REFUSED;
    }
    int getSize() { return size_; }
    WiFiClient *getStreamPtr() { return &client_; }
    bool connected() { return client_.connected() || client_.available(); }
    String getString()
    {
        std::string body;
        uint64_t last = now_ms();
        while ((size_ < 0 || (int)body.size() < size_) && connected()) {
            if (client_.available()) {
                char b[512];
                size_t n = client_.readBytes((uint8_t *)b, std::min<size_t>(sizeof(b), client_.available()));
                body.append(b, n);
                last = now_ms();
            } else if (now_ms() - last > (uint32_t)(timeout_ * scale())) {
                break;
            } else {
                delay(1);
            }
        }
        return String(body.c_str());
    }
    void end() { client_.stop(); }
};
