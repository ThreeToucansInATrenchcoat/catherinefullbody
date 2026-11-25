// Included by movie_decoder.cpp for the native Linux H.264 backend.
struct CatherineMovieDecoder {
    AVCodecContext* codec = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    SwsContext* converter = nullptr;
    std::vector<uint8_t> pixels;
    int error = 0;
    bool draining = false;

    ~CatherineMovieDecoder() {
        sws_freeContext(converter);
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
    }

    int result(int status) {
        if (status >= 0) return CATHERINE_MOVIE_OK;
        if (status == AVERROR(EAGAIN)) return CATHERINE_MOVIE_AGAIN;
        if (status == AVERROR_EOF) return CATHERINE_MOVIE_END;
        error = status;
        return CATHERINE_MOVIE_ERROR;
    }
};

CatherineMovieDecoder* catherine_movie_create(uint32_t width, uint32_t height,
        uint32_t rate_num, uint32_t rate_den, int32_t* error) {
    auto fail = [error](int status) -> CatherineMovieDecoder* {
        if (error) *error = status;
        return nullptr;
    };
    if ((width == 0) != (height == 0) || (rate_num == 0) != (rate_den == 0) ||
        width > 4096 || height > 2304 || ((width | height) & 1) ||
        rate_num > INT32_MAX || rate_den > INT32_MAX) return fail(AVERROR(EINVAL));

    const AVCodec* h264 = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!h264) return fail(AVERROR_DECODER_NOT_FOUND);
    std::unique_ptr<CatherineMovieDecoder> decoder(new (std::nothrow) CatherineMovieDecoder);
    if (!decoder || !decoder->packet || !decoder->decoded) return fail(AVERROR(ENOMEM));
    decoder->codec = avcodec_alloc_context3(h264);
    if (!decoder->codec) return fail(AVERROR(ENOMEM));
    decoder->codec->width = static_cast<int>(width);
    decoder->codec->height = static_cast<int>(height);
    decoder->codec->pkt_timebase = {1, 10000000};
    decoder->codec->max_pixels = 4096 * 2304;
    decoder->codec->thread_count = 1;
    if (rate_num) decoder->codec->framerate = {static_cast<int>(rate_num), static_cast<int>(rate_den)};
    const int status = avcodec_open2(decoder->codec, h264, nullptr);
    if (status < 0) return fail(status);
    if (error) *error = 0;
    return decoder.release();
}

void catherine_movie_destroy(CatherineMovieDecoder* decoder) {
    delete decoder;
}

int catherine_movie_submit(CatherineMovieDecoder* decoder, const void* data, uint32_t size,
                           int64_t pts_100ns) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (!data || !size || size > INT32_MAX || decoder->draining)
        return decoder->result(AVERROR(EINVAL));

    AVPacket* packet = decoder->packet;
    int status = av_new_packet(packet, static_cast<int>(size));
    if (status >= 0) {
        std::memcpy(packet->data, data, size);
        packet->pts = pts_100ns;
        status = avcodec_send_packet(decoder->codec, packet);
    }
    av_packet_unref(packet);
    return decoder->result(status);
}

int catherine_movie_receive(CatherineMovieDecoder* decoder, CatherineMovieFrame* frame) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (!frame) return decoder->result(AVERROR(EINVAL));
    AVFrame* decoded = decoder->decoded;
    const int status = avcodec_receive_frame(decoder->codec, decoded);
    if (status < 0) return decoder->result(status);

    const int width = decoded->width;
    const int height = decoded->height;
    if (width < 2 || width > 4096 || height < 2 || height > 2304 || ((width | height) & 1))
        return decoder->result(AVERROR_INVALIDDATA);
    const size_t luma = static_cast<size_t>(width) * height;
    try {
        decoder->pixels.resize(luma + luma / 2);
    } catch (const std::bad_alloc&) {
        return decoder->result(AVERROR(ENOMEM));
    }
    decoder->converter = sws_getCachedContext(decoder->converter, width, height,
        static_cast<AVPixelFormat>(decoded->format), width, height, AV_PIX_FMT_NV12,
        SWS_POINT, nullptr, nullptr, nullptr);
    if (!decoder->converter) return decoder->result(AVERROR(ENOMEM));

    uint8_t* out[] = {decoder->pixels.data(), decoder->pixels.data() + luma, nullptr, nullptr};
    const int strides[] = {width, width, 0, 0};
    if (sws_scale(decoder->converter, decoded->data, decoded->linesize, 0, height, out, strides) != height)
        return decoder->result(AVERROR_INVALIDDATA);

    *frame = {};
    frame->data = decoder->pixels.data();
    frame->size = decoder->pixels.size();
    frame->width = frame->display_width = static_cast<uint32_t>(width);
    frame->height = frame->display_height = static_cast<uint32_t>(height);
    const int64_t timestamp = decoded->best_effort_timestamp;
    frame->has_pts = timestamp != AV_NOPTS_VALUE;
    if (frame->has_pts) frame->pts_100ns = timestamp;
    return CATHERINE_MOVIE_OK;
}

int catherine_movie_drain(CatherineMovieDecoder* decoder) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    if (decoder->draining) return CATHERINE_MOVIE_OK;
    const int status = avcodec_send_packet(decoder->codec, nullptr);
    if (status == 0 || status == AVERROR_EOF) {
        decoder->draining = true;
        return CATHERINE_MOVIE_OK;
    }
    return decoder->result(status);
}

int catherine_movie_flush(CatherineMovieDecoder* decoder) {
    if (!decoder) return CATHERINE_MOVIE_ERROR;
    avcodec_flush_buffers(decoder->codec);
    av_frame_unref(decoder->decoded);
    decoder->draining = false;
    return CATHERINE_MOVIE_OK;
}

int32_t catherine_movie_last_error(const CatherineMovieDecoder* decoder) {
    return decoder ? decoder->error : AVERROR(EINVAL);
}
