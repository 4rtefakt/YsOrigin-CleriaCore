// The Windows byte stream under our own WebSocket framing (websocket_frames.inl): winsock, and SChannel for
// wss:// (TLS 1.2, the server certificate checked against the OS store by SChannel itself). Textual part of
// websocket.cpp (#included inside namespace cleria::net); not standalone.

namespace {

// The socket stays non-blocking: a read waits on its event and on `wake` (set when a frame is queued), so a send
// never waits for a time slice.
class WinStream final : public Stream {
public:
    explicit WinStream(HANDLE wake) : wake_(wake) {}
    ~WinStream() override {
        if (have_ctx_) DeleteSecurityContext(&ctx_);
        if (have_cred_) FreeCredentialsHandle(&cred_);
        if (sock_ != INVALID_SOCKET) closesocket(sock_);
        if (event_ != WSA_INVALID_EVENT) WSACloseEvent(event_);
    }

    // TCP connect (and the TLS handshake for wss). "" when connected, else why not.
    template <class ConnT>
    std::string open(ConnT& c, const WsUrl& u, const WsOptions& o) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res)
            return "server name not found";
        std::string why = "cannot connect (nothing listening, or blocked)";
        for (addrinfo* ai = res; ai && sock_ == INVALID_SOCKET; ai = ai->ai_next) {
            SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s == INVALID_SOCKET) continue;
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);
            bool ok = ::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0;
            if (!ok && WSAGetLastError() == WSAEWOULDBLOCK) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(o.connect_timeout_ms);
                while (!c.stop && !c.aborted) {
                    fd_set ws, es;
                    FD_ZERO(&ws);
                    FD_ZERO(&es);
                    FD_SET(s, &ws);
                    FD_SET(s, &es);
                    timeval tv{0, 50000};
                    if (select(0, nullptr, &ws, &es, &tv) > 0) {
                        int e = 0, len = sizeof e;
                        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&e), &len);
                        ok = e == 0 && FD_ISSET(s, &ws);
                        break;
                    }
                    if (std::chrono::steady_clock::now() > deadline) { why = "timed out"; break; }
                }
            }
            if (!ok) { closesocket(s); continue; }
            BOOL nodelay = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
            sock_ = s;
        }
        freeaddrinfo(res);
        if (c.stop) return "cancelled";
        if (sock_ == INVALID_SOCKET) return why;
        send_timeout_ms_ = o.send_timeout_ms;
        event_ = WSACreateEvent();
        if (event_ == WSA_INVALID_EVENT || WSAEventSelect(sock_, event_, FD_READ | FD_CLOSE) != 0) return "socket event failed";
        if (!u.secure) return {};
        return tls_handshake(c, widen(u.host), o.handshake_timeout_ms);
    }

    bool write(const char* p, std::size_t n) override {
        if (!tls_) return raw_write(p, n);
        std::vector<char> rec;
        while (n) {
            const std::size_t k = std::min<std::size_t>(n, sizes_.cbMaximumMessage);
            rec.assign(sizes_.cbHeader + k + sizes_.cbTrailer, 0);
            std::memcpy(rec.data() + sizes_.cbHeader, p, k);
            SecBuffer b[4] = {{sizes_.cbHeader, SECBUFFER_STREAM_HEADER, rec.data()},
                              {static_cast<unsigned long>(k), SECBUFFER_DATA, rec.data() + sizes_.cbHeader},
                              {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, rec.data() + sizes_.cbHeader + k},
                              {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc d{SECBUFFER_VERSION, 4, b};
            const SECURITY_STATUS ss = EncryptMessage(&ctx_, 0, &d, 0);
            if (ss != SEC_E_OK) { err = "TLS failure (encrypt " + hex(ss) + ")"; return false; }
            if (!raw_write(rec.data(), b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer)) return false;
            p += k;
            n -= k;
        }
        return true;
    }

    long read(char* p, std::size_t n, int wait_ms) override {
        if (!tls_) return raw_read(p, n, wait_ms);
        while (plain_.empty()) {
            if (!raw_.empty()) {
                SecBuffer b[4] = {{static_cast<unsigned long>(raw_.size()), SECBUFFER_DATA, raw_.data()},
                                  {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}};
                SecBufferDesc d{SECBUFFER_VERSION, 4, b};
                const SECURITY_STATUS ss = DecryptMessage(&ctx_, &d, 0, nullptr);
                if (ss == SEC_E_OK) {
                    std::string extra;
                    for (const SecBuffer& x : b) {
                        if (x.BufferType == SECBUFFER_DATA) plain_.append(static_cast<char*>(x.pvBuffer), x.cbBuffer);
                        if (x.BufferType == SECBUFFER_EXTRA) extra.assign(raw_.data() + raw_.size() - x.cbBuffer, x.cbBuffer);
                    }
                    raw_.swap(extra);
                    continue;
                }
                if (ss == SEC_I_CONTEXT_EXPIRED) { err = "the server dropped the connection"; return -1; }
                if (ss != SEC_E_INCOMPLETE_MESSAGE) { err = "TLS failure (decrypt " + hex(ss) + ")"; return -1; }
            }
            char buf[16 * 1024];
            const long k = raw_read(buf, sizeof buf, wait_ms);
            if (k <= 0) return k;
            raw_.append(buf, static_cast<std::size_t>(k));
            wait_ms = 0;
        }
        const std::size_t k = std::min(n, plain_.size());
        std::memcpy(p, plain_.data(), k);
        plain_.erase(0, k);
        return static_cast<long>(k);
    }

    SOCKET handle() const { return sock_; }

private:
    static std::string hex(SECURITY_STATUS ss) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08lX", static_cast<unsigned long>(ss));
        return b;
    }

    bool raw_write(const char* p, std::size_t n) {
        auto last = std::chrono::steady_clock::now();
        while (n) {
            const int k = ::send(sock_, p, static_cast<int>(std::min<std::size_t>(n, 1 << 20)), 0);
            if (k > 0) {
                p += k;
                n -= static_cast<std::size_t>(k);
                last = std::chrono::steady_clock::now();
                continue;
            }
            if (WSAGetLastError() != WSAEWOULDBLOCK) { err = "connection reset"; return false; }
            if (std::chrono::steady_clock::now() - last > std::chrono::milliseconds(send_timeout_ms_)) {
                err = "timed out";
                return false;
            }
            fd_set ws;
            FD_ZERO(&ws);
            FD_SET(sock_, &ws);
            timeval tv{0, 50000};
            select(0, nullptr, &ws, nullptr, &tv);
        }
        return true;
    }
    long raw_read(char* p, std::size_t n, int wait_ms) {
        for (int pass = 0; pass < 2; ++pass) {
            const int k = ::recv(sock_, p, static_cast<int>(n), 0);
            if (k > 0) return k;
            if (k == 0) { err = "the server dropped the connection"; return -1; }
            if (WSAGetLastError() != WSAEWOULDBLOCK) { err = "connection reset"; return -1; }
            if (pass == 1 || wait_ms <= 0) break;
            HANDLE hs[2] = {event_, wake_};
            WSAWaitForMultipleEvents(wake_ ? 2 : 1, hs, FALSE, static_cast<DWORD>(wait_ms), FALSE);
            WSAResetEvent(event_);   // the recv below re-arms FD_READ when nothing is there yet
        }
        return 0;
    }

    template <class ConnT>
    std::string tls_handshake(ConnT& c, const std::wstring& host, int timeout_ms) {
        SCHANNEL_CRED sc{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
        if (AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &sc,
                                      nullptr, nullptr, &cred_, nullptr) != SEC_E_OK)
            return "TLS failure (no credentials)";
        have_cred_ = true;
        const DWORD want = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                           ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        std::wstring target = host;
        while (true) {
            SecBuffer in[2] = {{static_cast<unsigned long>(raw_.size()), SECBUFFER_TOKEN, raw_.data()}, {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc ind{SECBUFFER_VERSION, 2, in};
            SecBuffer out[1] = {{0, SECBUFFER_TOKEN, nullptr}};
            SecBufferDesc outd{SECBUFFER_VERSION, 1, out};
            DWORD got = 0;
            const SECURITY_STATUS ss =
                InitializeSecurityContextW(&cred_, have_ctx_ ? &ctx_ : nullptr, target.data(), want, 0, 0,
                                           have_ctx_ ? &ind : nullptr, 0, &ctx_, &outd, &got, nullptr);
            if (ss == SEC_E_OK || ss == SEC_I_CONTINUE_NEEDED) have_ctx_ = true;
            if (out[0].cbBuffer && out[0].pvBuffer) {
                const bool ok = raw_write(static_cast<char*>(out[0].pvBuffer), out[0].cbBuffer);
                FreeContextBuffer(out[0].pvBuffer);
                if (!ok) return err;
            }
            if (ss != SEC_E_INCOMPLETE_MESSAGE) {
                if (in[1].BufferType == SECBUFFER_EXTRA) raw_.erase(0, raw_.size() - in[1].cbBuffer);
                else raw_.clear();
            }
            if (ss == SEC_E_OK) break;
            if (ss != SEC_I_CONTINUE_NEEDED && ss != SEC_E_INCOMPLETE_MESSAGE && ss != SEC_I_INCOMPLETE_CREDENTIALS)
                return "TLS failure (certificate or protocol, " + hex(ss) + ")";
            if (ss == SEC_I_CONTINUE_NEEDED && !raw_.empty()) continue;
            if (ss == SEC_I_INCOMPLETE_CREDENTIALS) continue;   // a client certificate asked: go on without one
            while (true) {   // more from the server
                if (c.stop || c.aborted) return "cancelled";
                if (std::chrono::steady_clock::now() > deadline) return "timed out";
                char buf[16 * 1024];
                const long k = raw_read(buf, sizeof buf, 50);
                if (k < 0) return "TLS failure (" + err + ")";
                if (k > 0) { raw_.append(buf, static_cast<std::size_t>(k)); break; }
            }
        }
        if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK) return "TLS failure (stream sizes)";
        tls_ = true;
        return {};
    }

    SOCKET sock_ = INVALID_SOCKET;
    HANDLE wake_ = nullptr;
    WSAEVENT event_ = WSA_INVALID_EVENT;
    int send_timeout_ms_ = 15000;
    bool tls_ = false, have_cred_ = false, have_ctx_ = false;
    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    std::string raw_, plain_;   // TLS records not yet decrypted; decrypted bytes not yet read
};

}  // namespace
