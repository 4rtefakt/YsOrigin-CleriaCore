// websocket.cpp, Windows: a TCP connection (winsock) with TLS by SChannel, for the WebSocket that frames its own
// messages (ws_proto: permessage-deflate, which WinHTTP refuses). The certificate is checked by SChannel against
// the system store and the host name (SCH_CRED_AUTO_CRED_VALIDATION, the target name). TLS 1.2: SChannel's
// TLS 1.3 needs the newer SCH_CREDENTIALS and its post-handshake messages [H]; WinHTTP stays the fallback.
// Included inside namespace cleria::net.

class Stream {
public:
    ~Stream() { shut(); }

    // Connects to host:port (and makes the TLS session when `secure`); false with `why` on a failure.
    bool connect(const std::string& host, int port, bool secure, int timeout_ms, std::string* why) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { *why = "winsock unavailable"; return false; }
        wsa_ = true;
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
            *why = "server name not found";
            return false;
        }
        std::string last = "cannot connect (nothing listening, or blocked)";
        for (addrinfo* a = res; a && sock_ == INVALID_SOCKET; a = a->ai_next) {
            SOCKET s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == INVALID_SOCKET) continue;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (aborted_) { ::closesocket(s); break; }
                sock_ = s;
            }
            u_long nb = 1;
            ioctlsocket(s, FIONBIO, &nb);
            const int r = ::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
            bool ok = r == 0;
            if (!ok && WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set w, e;
                FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
                timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
                ok = ::select(0, nullptr, &w, &e, &tv) == 1 && FD_ISSET(s, &w);
                if (!ok && !FD_ISSET(s, &e)) last = "timed out";
            }
            if (!ok) {
                std::lock_guard<std::mutex> l(mu_);
                if (sock_ != INVALID_SOCKET) ::closesocket(sock_);
                sock_ = INVALID_SOCKET;
            }
        }
        freeaddrinfo(res);
        if (sock_ == INVALID_SOCKET) { *why = aborted_ ? "cancelled" : last; return false; }
        int one = 1;
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
        if (secure && !tls_handshake(host, timeout_ms, why)) return false;
        return true;
    }

    // Sends everything (TLS records when secure); false when the connection is gone.
    bool send(std::string_view data) {
        if (!tls_) return send_raw(data);
        while (!data.empty()) {
            const std::size_t n = (std::min)(data.size(), static_cast<std::size_t>(sizes_.cbMaximumMessage));
            std::string rec(sizes_.cbHeader + n + sizes_.cbTrailer, '\0');
            std::memcpy(rec.data() + sizes_.cbHeader, data.data(), n);
            SecBuffer b[4] = {{sizes_.cbHeader, SECBUFFER_STREAM_HEADER, rec.data()},
                              {static_cast<unsigned long>(n), SECBUFFER_DATA, rec.data() + sizes_.cbHeader},
                              {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, rec.data() + sizes_.cbHeader + n},
                              {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc d{SECBUFFER_VERSION, 4, b};
            if (EncryptMessage(&ctx_, 0, &d, 0) != SEC_E_OK) return false;
            if (!send_raw(std::string_view(rec.data(), b[0].cbBuffer + b[1].cbBuffer + b[2].cbBuffer))) return false;
            data.remove_prefix(n);
        }
        return true;
    }

    // Waits up to `ms` for data: appends what arrived (decrypted) to `out`. False when the connection ended
    // (`why` then says how), true otherwise (possibly with nothing new).
    bool recv(std::string* out, int ms, std::string* why) {
        if (tls_ && !plain_.empty()) { out->append(plain_); plain_.clear(); return true; }
        if (tls_ && decrypt(out, why) == 1) return true;   // a whole record already waiting
        if (!why->empty()) return false;
        fd_set r;
        FD_ZERO(&r);
        FD_SET(sock_, &r);
        timeval tv{ms / 1000, (ms % 1000) * 1000};
        const int sel = ::select(0, &r, nullptr, nullptr, &tv);
        if (sel < 0) { *why = aborted_ ? "closed" : "connection lost"; return false; }
        if (sel == 0) return true;
        char buf[16384];
        const int n = ::recv(sock_, buf, sizeof buf, 0);
        if (n == 0) { *why = "the server closed the connection"; return false; }
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) return true;
            *why = aborted_ ? "closed" : "connection lost";
            return false;
        }
        if (!tls_) { out->append(buf, static_cast<std::size_t>(n)); return true; }
        enc_.append(buf, static_cast<std::size_t>(n));
        while (decrypt(out, why) == 1) {}
        return why->empty();
    }

    // From another thread: makes a blocked connect / select return.
    void abort() {
        std::lock_guard<std::mutex> l(mu_);
        aborted_ = true;
        if (sock_ != INVALID_SOCKET) { ::shutdown(sock_, SD_BOTH); ::closesocket(sock_); sock_ = INVALID_SOCKET; }
    }

private:
    bool send_raw(std::string_view data) {
        while (!data.empty()) {
            const int n = ::send(sock_, data.data(), static_cast<int>((std::min)(data.size(), std::size_t(1) << 20)), 0);
            if (n > 0) { data.remove_prefix(static_cast<std::size_t>(n)); continue; }
            if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set w;
                FD_ZERO(&w);
                FD_SET(sock_, &w);
                timeval tv{15, 0};
                if (::select(0, nullptr, &w, nullptr, &tv) != 1) return false;
                continue;
            }
            return false;
        }
        return true;
    }

    // One TLS record out of enc_: 1 decrypted (appended), 0 needs more bytes, -1 error / closed (why set).
    int decrypt(std::string* out, std::string* why) {
        if (enc_.empty()) return 0;
        SecBuffer b[4] = {{static_cast<unsigned long>(enc_.size()), SECBUFFER_DATA, enc_.data()},
                          {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc d{SECBUFFER_VERSION, 4, b};
        const SECURITY_STATUS st = DecryptMessage(&ctx_, &d, 0, nullptr);
        if (st == SEC_E_INCOMPLETE_MESSAGE) return 0;
        if (st == SEC_I_CONTEXT_EXPIRED) { *why = "the server closed the connection"; return -1; }
        if (st != SEC_E_OK && st != SEC_I_RENEGOTIATE) { *why = "TLS failure (" + std::to_string(st) + ")"; return -1; }
        std::string extra;
        for (SecBuffer& x : b) {
            if (x.BufferType == SECBUFFER_DATA) out->append(static_cast<char*>(x.pvBuffer), x.cbBuffer);
            if (x.BufferType == SECBUFFER_EXTRA) extra.assign(static_cast<char*>(x.pvBuffer), x.cbBuffer);
        }
        enc_ = std::move(extra);
        if (st == SEC_I_RENEGOTIATE) { *why = "TLS renegotiation is not supported"; return -1; }
        return 1;
    }

    bool tls_handshake(const std::string& host, int timeout_ms, std::string* why) {
        SCHANNEL_CRED sc{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        sc.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
        if (AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &sc,
                                      nullptr, nullptr, &cred_, nullptr) != SEC_E_OK) {
            *why = "TLS unavailable";
            return false;
        }
        has_cred_ = true;
        const std::wstring target(host.begin(), host.end());
        const DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                            ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
        std::string in;
        bool first = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            SecBuffer ib[2] = {{static_cast<unsigned long>(in.size()), SECBUFFER_TOKEN, in.data()}, {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc id{SECBUFFER_VERSION, 2, ib};
            SecBuffer ob[1] = {{0, SECBUFFER_TOKEN, nullptr}};
            SecBufferDesc od{SECBUFFER_VERSION, 1, ob};
            DWORD got = 0;
            const SECURITY_STATUS st = InitializeSecurityContextW(&cred_, first ? nullptr : &ctx_, const_cast<wchar_t*>(target.c_str()),
                                                                  flags, 0, 0, first ? nullptr : &id, 0, first ? &ctx_ : nullptr,
                                                                  &od, &got, nullptr);
            if (first) has_ctx_ = true;
            first = false;
            if (ob[0].pvBuffer) {
                const bool sent = ob[0].cbBuffer == 0 ||
                                  send_raw(std::string_view(static_cast<char*>(ob[0].pvBuffer), ob[0].cbBuffer));
                FreeContextBuffer(ob[0].pvBuffer);
                if (!sent) { *why = "connection lost during the TLS handshake"; return false; }
            }
            if (st == SEC_E_INCOMPLETE_MESSAGE) {
                // keep `in`, read more
            } else {
                if (ib[1].BufferType == SECBUFFER_EXTRA) in = in.substr(in.size() - ib[1].cbBuffer);
                else in.clear();
            }
            if (st == SEC_E_OK) {
                if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK) {
                    *why = "TLS failure (stream sizes)";
                    return false;
                }
                tls_ = true;
                enc_ = std::move(in);        // application data that came with the last handshake record
                return true;
            }
            if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_INCOMPLETE_MESSAGE) {
                *why = st == SEC_E_UNTRUSTED_ROOT || st == SEC_E_WRONG_PRINCIPAL || st == SEC_E_CERT_EXPIRED
                           ? "TLS failure (certificate)" : "TLS failure (" + std::to_string(st) + ")";
                return false;
            }
            if (st == SEC_I_CONTINUE_NEEDED && !in.empty()) continue;
            // read more of the server's handshake
            const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  deadline - std::chrono::steady_clock::now()).count());
            if (left <= 0) { *why = "timed out (TLS handshake)"; return false; }
            fd_set r;
            FD_ZERO(&r);
            FD_SET(sock_, &r);
            timeval tv{left / 1000, (left % 1000) * 1000};
            if (::select(0, &r, nullptr, nullptr, &tv) != 1) { *why = aborted_ ? "cancelled" : "timed out (TLS handshake)"; return false; }
            char buf[16384];
            const int n = ::recv(sock_, buf, sizeof buf, 0);
            if (n <= 0) { *why = "connection lost during the TLS handshake"; return false; }
            in.append(buf, static_cast<std::size_t>(n));
        }
    }

    void shut() {
        if (tls_ && sock_ != INVALID_SOCKET) {       // a polite close_notify
            DWORD type = SCHANNEL_SHUTDOWN;
            SecBuffer b{sizeof type, SECBUFFER_TOKEN, &type};
            SecBufferDesc d{SECBUFFER_VERSION, 1, &b};
            if (ApplyControlToken(&ctx_, &d) == SEC_E_OK) {
                SecBuffer ob{0, SECBUFFER_TOKEN, nullptr};
                SecBufferDesc od{SECBUFFER_VERSION, 1, &ob};
                DWORD got = 0;
                InitializeSecurityContextW(&cred_, &ctx_, nullptr, ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0, 0,
                                           nullptr, 0, nullptr, &od, &got, nullptr);
                if (ob.pvBuffer) { send_raw(std::string_view(static_cast<char*>(ob.pvBuffer), ob.cbBuffer)); FreeContextBuffer(ob.pvBuffer); }
            }
        }
        if (has_ctx_) { DeleteSecurityContext(&ctx_); has_ctx_ = false; }
        if (has_cred_) { FreeCredentialsHandle(&cred_); has_cred_ = false; }
        {
            std::lock_guard<std::mutex> l(mu_);
            if (sock_ != INVALID_SOCKET) { ::closesocket(sock_); sock_ = INVALID_SOCKET; }
        }
        if (wsa_) { WSACleanup(); wsa_ = false; }
    }

    std::mutex mu_;
    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> aborted_{false};
    bool wsa_ = false;
    CredHandle cred_{};
    CtxtHandle ctx_{};
    bool has_cred_ = false, has_ctx_ = false, tls_ = false;
    SecPkgContext_StreamSizes sizes_{};
    std::string enc_;     // received TLS bytes not decrypted yet
    std::string plain_;
};
