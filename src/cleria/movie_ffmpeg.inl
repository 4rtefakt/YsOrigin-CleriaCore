// MovieDecoder over FFmpeg (Linux / macOS, CMake CLERIA_FFMPEG) — see movie.hpp.
// Textual part of movie.cpp (#included inside namespace cleria, after the FFmpeg headers); not standalone.
//
// The same contract as the Media Foundation decoder: the whole file is read into memory and its '00xx'
// video chunk ids renamed (avi_patch_chunk_ids), the AVI demuxer reads those bytes through a custom
// AVIOContext, only the video stream is decoded (libavcodec's MPEG-4 Part 2 decoder takes the XviD
// stream), and each shown frame goes YUV -> NV12 (libswscale, a plain repack at the same size) ->
// nv12_to_bgra, so the colour conversion is the one the Windows build uses (BT.601, video range).

struct AvMem {   // the patched AVI in memory, read by the AVIOContext (MovieDecoder::Impl holds one)
    std::vector<std::uint8_t> data;
    std::size_t pos = 0;
};
namespace {
int av_mem_read(void* o, std::uint8_t* buf, int n) {
    auto* m = static_cast<AvMem*>(o);
    const std::size_t left = m->data.size() - m->pos;
    if (left == 0) return AVERROR_EOF;
    const std::size_t k = std::min<std::size_t>(left, std::size_t(n));
    std::memcpy(buf, m->data.data() + m->pos, k);
    m->pos += k;
    return int(k);
}
std::int64_t av_mem_seek(void* o, std::int64_t off, int whence) {
    auto* m = static_cast<AvMem*>(o);
    const auto size = std::int64_t(m->data.size());
    if (whence & AVSEEK_SIZE) return size;
    std::int64_t p = off;
    switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET: break;
        case SEEK_CUR: p = std::int64_t(m->pos) + off; break;
        case SEEK_END: p = size + off; break;
        default: return -1;
    }
    if (p < 0 || p > size) return -1;
    m->pos = std::size_t(p);
    return p;
}
}  // namespace

struct MovieDecoder::Impl {
    AvMem mem;
    AVIOContext* io = nullptr;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    SwsContext* sws = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* next = nullptr;          // decoded, not shown yet (valid while have_next)
    AVFrame* show = nullptr;
    int stream = -1;
    double tb = 0;                    // seconds per timestamp unit
    double next_ts = 0, last_ts = -1;
    bool have_next = false, eos = false, flushing = false;
    std::vector<std::uint8_t> nv12;

    ~Impl() {
        sws_freeContext(sws);
        av_frame_free(&show);
        av_frame_free(&next);
        av_packet_free(&pkt);
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);   // frees fmt; our pb stays ours (AVFMT_FLAG_CUSTOM_IO)
        if (io) av_freep(&io->buffer);
        avio_context_free(&io);
    }

    // Decodes the next video frame into `next`. False at the end of the stream or on error.
    bool read_next() {
        for (;;) {
            const int r = avcodec_receive_frame(cc, next);
            if (r == 0) {
                const std::int64_t pts = next->best_effort_timestamp;
                next_ts = pts != AV_NOPTS_VALUE ? double(pts) * tb : last_ts + 1.0 / 60.0;
                last_ts = next_ts;
                return true;
            }
            if (r != AVERROR(EAGAIN) || flushing) { eos = true; return false; }
            for (;;) {   // feed one video packet (or the flush at the end)
                if (av_read_frame(fmt, pkt) < 0) {
                    avcodec_send_packet(cc, nullptr);
                    flushing = true;
                    break;
                }
                const bool ours = pkt->stream_index == stream;
                if (ours && avcodec_send_packet(cc, pkt) < 0) { av_packet_unref(pkt); eos = true; return false; }
                av_packet_unref(pkt);
                if (ours) break;
            }
        }
    }
};

MovieDecoder::MovieDecoder() = default;
MovieDecoder::~MovieDecoder() { close(); }

bool MovieDecoder::available() { return true; }
bool MovieDecoder::is_open() const { return impl_ && impl_->cc; }

void MovieDecoder::close() {
    impl_.reset();
    w_ = h_ = 0;
    duration_ = 0;
    ended_ = false;
    frame_.clear();
}

bool MovieDecoder::open(const fs::path& file) {
    close();
    std::error_code ec;
    if (file.empty() || !fs::is_regular_file(file, ec)) return false;
    auto im = std::make_unique<Impl>();
    {
        std::ifstream f(vfs::resolve(file), std::ios::binary);
        im->mem.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (im->mem.data.empty()) return false;
    avi_patch_chunk_ids(im->mem.data.data(), im->mem.data.size());

    constexpr int kIoBuf = 1 << 16;
    auto* iobuf = static_cast<unsigned char*>(av_malloc(kIoBuf));
    if (!iobuf) return false;
    im->io = avio_alloc_context(iobuf, kIoBuf, 0, &im->mem, av_mem_read, nullptr, av_mem_seek);
    if (!im->io) { av_free(iobuf); return false; }
    im->fmt = avformat_alloc_context();
    if (!im->fmt) return false;
    im->fmt->pb = im->io;
    im->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    // The demo inserts are AVIs named *.dat: name the demuxer rather than probe by extension.
    if (avformat_open_input(&im->fmt, "movie.avi", av_find_input_format("avi"), nullptr) < 0) return false;
    if (avformat_find_stream_info(im->fmt, nullptr) < 0) return false;
    const AVCodec* codec = nullptr;
    im->stream = av_find_best_stream(im->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (im->stream < 0 || !codec) return false;
    for (unsigned i = 0; i < im->fmt->nb_streams; ++i)
        if (int(i) != im->stream) im->fmt->streams[i]->discard = AVDISCARD_ALL;   // the PCM track is never played
    const AVStream* st = im->fmt->streams[im->stream];
    im->cc = avcodec_alloc_context3(codec);
    if (!im->cc || avcodec_parameters_to_context(im->cc, st->codecpar) < 0) return false;
    im->cc->thread_count = 0;   // FFmpeg's choice of threads
    if (avcodec_open2(im->cc, codec, nullptr) < 0) return false;
    im->tb = av_q2d(st->time_base);
    im->pkt = av_packet_alloc();
    im->next = av_frame_alloc();
    im->show = av_frame_alloc();
    if (!im->pkt || !im->next || !im->show || im->cc->width <= 0 || im->cc->height <= 0) return false;
    w_ = std::uint32_t(im->cc->width);
    h_ = std::uint32_t(im->cc->height);
    if (im->fmt->duration > 0) duration_ = double(im->fmt->duration) / AV_TIME_BASE;
    else if (st->duration > 0) duration_ = double(st->duration) * im->tb;
    else duration_ = avi_duration(file).value_or(0.0);
    frame_.assign(std::size_t(w_) * h_ * 4, 0);
    for (std::size_t i = 3; i < frame_.size(); i += 4) frame_[i] = 255;
    impl_ = std::move(im);
    return true;
}

bool MovieDecoder::advance(double t) {
    if (!impl_ || !impl_->cc || ended_) return false;
    Impl& im = *impl_;
    bool due = false;
    for (;;) {
        if (!im.have_next && !im.eos) im.have_next = im.read_next();
        if (!im.have_next) break;                // the end of the stream
        if (im.next_ts > t + 0.002) break;       // not due yet (2 ms of slack for t's rounding)
        av_frame_unref(im.show);                 // due: the newest due frame wins
        av_frame_move_ref(im.show, im.next);
        im.have_next = false;
        due = true;
    }
    if (!due) {
        if (im.eos && !im.have_next) ended_ = true;
        return false;
    }
    const AVFrame* f = im.show;
    if (f->width != int(w_) || f->height != int(h_)) return false;
    im.sws = sws_getCachedContext(im.sws, f->width, f->height, AVPixelFormat(f->format), f->width, f->height,
                                  AV_PIX_FMT_NV12, SWS_POINT, nullptr, nullptr, nullptr);
    if (!im.sws) return false;
    const std::size_t pitch = (std::size_t(w_) + 1) & ~std::size_t(1);
    const std::size_t rows = (std::size_t(h_) + 1) & ~std::size_t(1);
    im.nv12.resize(pitch * rows + pitch * rows / 2);
    std::uint8_t* dst[4] = {im.nv12.data(), im.nv12.data() + pitch * rows, nullptr, nullptr};
    const int dst_stride[4] = {int(pitch), int(pitch), 0, 0};
    sws_scale(im.sws, f->data, f->linesize, 0, f->height, dst, dst_stride);
    nv12_to_bgra(dst[0], dst[1], pitch, w_, h_, frame_.data());
    return true;
}
