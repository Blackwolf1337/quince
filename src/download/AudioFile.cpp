#include "download/AudioFile.h"

#include <QtEndian>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <memory>

namespace AudioFile {

namespace {

QString errorText(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

// owners for the FFmpeg objects, freed however write() returns
struct InputDeleter
{
    void operator()(AVFormatContext *c) const { avformat_close_input(&c); }
};
struct OutputDeleter
{
    void operator()(AVFormatContext *c) const
    {
        if (c && !(c->oformat->flags & AVFMT_NOFILE))
            avio_closep(&c->pb);
        avformat_free_context(c);
    }
};
struct CodecDeleter
{
    void operator()(AVCodecContext *c) const { avcodec_free_context(&c); }
};
struct SwrDeleter
{
    void operator()(SwrContext *c) const { swr_free(&c); }
};
struct FifoDeleter
{
    void operator()(AVAudioFifo *f) const { av_audio_fifo_free(f); }
};
struct FrameDeleter
{
    void operator()(AVFrame *f) const { av_frame_free(&f); }
};
struct PacketDeleter
{
    void operator()(AVPacket *p) const { av_packet_free(&p); }
};

using Input = std::unique_ptr<AVFormatContext, InputDeleter>;
using Output = std::unique_ptr<AVFormatContext, OutputDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;

// FLAC picture block (front cover), base64 in the Vorbis comment
// METADATA_BLOCK_PICTURE, which is how Ogg files carry covers
QByteArray pictureBlock(const Tags &tags)
{
    const QByteArray mime("image/jpeg");
    QByteArray block;
    const auto be32 = [&block](quint32 value) {
        char bytes[4];
        qToBigEndian(value, bytes);
        block.append(bytes, 4);
    };
    be32(3); // front cover
    be32(quint32(mime.size()));
    block.append(mime);
    be32(0); // no description
    be32(quint32(tags.coverWidth));
    be32(quint32(tags.coverHeight));
    be32(24);
    be32(0);
    be32(quint32(tags.coverJpeg.size()));
    block.append(tags.coverJpeg);
    return block.toBase64();
}

void setTags(AVDictionary **dict, const Tags &tags)
{
    const auto set = [dict](const char *key, const QString &value) {
        if (!value.isEmpty())
            av_dict_set(dict, key, value.toUtf8().constData(), 0);
    };
    set("title", tags.title);
    set("artist", tags.artist);
    set("album", tags.album);
    set("album_artist", tags.albumArtist);
    set("date", tags.year);
    if (tags.track > 0)
        set("track", QString::number(tags.track));
}

// the cover as an attached picture stream (MP3: ID3 APIC, MP4: covr)
AVStream *addCoverStream(AVFormatContext *out, const Tags &tags)
{
    AVStream *cover = avformat_new_stream(out, nullptr);
    if (!cover)
        return nullptr;
    cover->disposition = AV_DISPOSITION_ATTACHED_PIC;
    cover->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    cover->codecpar->codec_id = AV_CODEC_ID_MJPEG;
    // the muxers refuse a picture of unknown size
    cover->codecpar->width = tags.coverWidth;
    cover->codecpar->height = tags.coverHeight;
    cover->time_base = AVRational{1, 90000};
    return cover;
}

QString writeCover(AVFormatContext *out, AVStream *cover, const Tags &tags)
{
    Packet packet(av_packet_alloc());
    if (av_new_packet(packet.get(), int(tags.coverJpeg.size())) < 0)
        return QStringLiteral("out of memory");
    memcpy(packet->data, tags.coverJpeg.constData(), size_t(tags.coverJpeg.size()));
    packet->stream_index = cover->index;
    packet->flags |= AV_PKT_FLAG_KEY;
    const int r = av_interleaved_write_frame(out, packet.get());
    return r < 0 ? errorText(r) : QString();
}

QString copyPackets(AVFormatContext *in, int inIndex, AVFormatContext *out, AVStream *outStream)
{
    Packet packet(av_packet_alloc());
    while (av_read_frame(in, packet.get()) >= 0) {
        if (packet->stream_index == inIndex) {
            av_packet_rescale_ts(packet.get(), in->streams[inIndex]->time_base, outStream->time_base);
            packet->stream_index = outStream->index;
            packet->pos = -1;
            const int r = av_interleaved_write_frame(out, packet.get());
            if (r < 0)
                return errorText(r);
        }
        av_packet_unref(packet.get());
    }
    return {};
}

// decode, resample, re-frame to the encoder's frame size, encode
QString encodePackets(AVFormatContext *in, int inIndex, AVCodecContext *decoder, AVFormatContext *out,
                      AVStream *outStream, AVCodecContext *encoder)
{
    SwrContext *swrRaw = nullptr;
    if (swr_alloc_set_opts2(&swrRaw, &encoder->ch_layout, encoder->sample_fmt, encoder->sample_rate,
                            &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr) < 0
        || swr_init(swrRaw) < 0) {
        swr_free(&swrRaw);
        return QStringLiteral("resampler failed");
    }
    std::unique_ptr<SwrContext, SwrDeleter> swr(swrRaw);
    std::unique_ptr<AVAudioFifo, FifoDeleter> fifo(
        av_audio_fifo_alloc(encoder->sample_fmt, encoder->ch_layout.nb_channels, encoder->frame_size * 4));
    Packet packet(av_packet_alloc());
    Frame decoded(av_frame_alloc());
    Frame resampled(av_frame_alloc());
    Frame chunk(av_frame_alloc());
    Packet encoded(av_packet_alloc());
    qint64 pts = 0;

    const auto drainEncoder = [&]() -> QString {
        while (true) {
            const int r = avcodec_receive_packet(encoder, encoded.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                return {};
            if (r < 0)
                return errorText(r);
            av_packet_rescale_ts(encoded.get(), encoder->time_base, outStream->time_base);
            encoded->stream_index = outStream->index;
            const int w = av_interleaved_write_frame(out, encoded.get());
            if (w < 0)
                return errorText(w);
        }
    };
    // hands the encoder full frames from the fifo; all of it at the end
    const auto feed = [&](bool flush) -> QString {
        while (av_audio_fifo_size(fifo.get()) >= encoder->frame_size
               || (flush && av_audio_fifo_size(fifo.get()) > 0)) {
            const int samples = qMin(av_audio_fifo_size(fifo.get()), encoder->frame_size);
            av_frame_unref(chunk.get());
            chunk->nb_samples = samples;
            chunk->format = encoder->sample_fmt;
            chunk->sample_rate = encoder->sample_rate;
            av_channel_layout_copy(&chunk->ch_layout, &encoder->ch_layout);
            if (av_frame_get_buffer(chunk.get(), 0) < 0)
                return QStringLiteral("out of memory");
            av_audio_fifo_read(fifo.get(), reinterpret_cast<void **>(chunk->data), samples);
            chunk->pts = pts;
            pts += samples;
            if (avcodec_send_frame(encoder, chunk.get()) < 0)
                return QStringLiteral("encoder refused a frame");
            if (const QString e = drainEncoder(); !e.isEmpty())
                return e;
        }
        return {};
    };
    const auto convert = [&](AVFrame *source) -> QString {
        const int maxOut = int(swr_get_out_samples(swr.get(), source ? source->nb_samples : 0));
        if (maxOut <= 0)
            return {};
        av_frame_unref(resampled.get());
        resampled->nb_samples = maxOut;
        resampled->format = encoder->sample_fmt;
        resampled->sample_rate = encoder->sample_rate;
        av_channel_layout_copy(&resampled->ch_layout, &encoder->ch_layout);
        if (av_frame_get_buffer(resampled.get(), 0) < 0)
            return QStringLiteral("out of memory");
        const int got = swr_convert(swr.get(), resampled->data, maxOut,
                                    source ? const_cast<const uint8_t **>(source->extended_data) : nullptr,
                                    source ? source->nb_samples : 0);
        if (got < 0)
            return errorText(got);
        av_audio_fifo_write(fifo.get(), reinterpret_cast<void **>(resampled->data), got);
        return feed(false);
    };
    const auto drainDecoder = [&]() -> QString {
        while (avcodec_receive_frame(decoder, decoded.get()) >= 0) {
            if (const QString e = convert(decoded.get()); !e.isEmpty())
                return e;
            av_frame_unref(decoded.get());
        }
        return {};
    };

    while (av_read_frame(in, packet.get()) >= 0) {
        if (packet->stream_index == inIndex && avcodec_send_packet(decoder, packet.get()) >= 0) {
            if (const QString e = drainDecoder(); !e.isEmpty())
                return e;
        }
        av_packet_unref(packet.get());
    }
    // what is still inside decoder, resampler, fifo and encoder
    avcodec_send_packet(decoder, nullptr);
    if (const QString e = drainDecoder(); !e.isEmpty())
        return e;
    if (const QString e = convert(nullptr); !e.isEmpty())
        return e;
    if (const QString e = feed(true); !e.isEmpty())
        return e;
    avcodec_send_frame(encoder, nullptr);
    return drainEncoder();
}

} // namespace

QString extension(Format format)
{
    switch (format) {
    case Format::Opus:
        return QStringLiteral("opus");
    case Format::M4a:
        return QStringLiteral("m4a");
    case Format::Mp3:
        return QStringLiteral("mp3");
    }
    return {};
}

QString write(const QString &input, const QString &output, Format format, const Tags &tags, int mp3Kbps)
{
    AVFormatContext *inRaw = nullptr;
    if (int r = avformat_open_input(&inRaw, input.toUtf8().constData(), nullptr, nullptr); r < 0)
        return errorText(r);
    Input in(inRaw);
    if (int r = avformat_find_stream_info(in.get(), nullptr); r < 0)
        return errorText(r);
    const int inIndex = av_find_best_stream(in.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (inIndex < 0)
        return QStringLiteral("no audio in the download");
    AVStream *inStream = in->streams[inIndex];

    const char *muxer = format == Format::Opus ? "opus" : format == Format::M4a ? "ipod" : "mp3";
    if (format == Format::Opus && inStream->codecpar->codec_id != AV_CODEC_ID_OPUS)
        return QStringLiteral("the stream is not Opus");
    if (format == Format::M4a && inStream->codecpar->codec_id != AV_CODEC_ID_AAC)
        return QStringLiteral("the stream is not AAC");

    AVFormatContext *outRaw = nullptr;
    if (int r = avformat_alloc_output_context2(&outRaw, nullptr, muxer, output.toUtf8().constData()); r < 0)
        return errorText(r);
    Output out(outRaw);
    AVStream *outStream = avformat_new_stream(out.get(), nullptr);
    if (!outStream)
        return QStringLiteral("out of memory");

    Codec decoder;
    Codec encoder;
    if (format == Format::Mp3) {
        const AVCodec *decoderCodec = avcodec_find_decoder(inStream->codecpar->codec_id);
        const AVCodec *encoderCodec = avcodec_find_encoder_by_name("libmp3lame");
        if (!decoderCodec || !encoderCodec)
            return QStringLiteral("this FFmpeg has no MP3 encoder (libmp3lame)");
        decoder.reset(avcodec_alloc_context3(decoderCodec));
        avcodec_parameters_to_context(decoder.get(), inStream->codecpar);
        if (int r = avcodec_open2(decoder.get(), decoderCodec, nullptr); r < 0)
            return errorText(r);

        encoder.reset(avcodec_alloc_context3(encoderCodec));
        encoder->bit_rate = int64_t(mp3Kbps) * 1000;
        encoder->sample_rate = decoder->sample_rate == 44100 ? 44100 : 48000;
        av_channel_layout_default(&encoder->ch_layout, qMin(2, decoder->ch_layout.nb_channels));
        const void *formats = nullptr;
        int count = 0;
        avcodec_get_supported_config(encoder.get(), encoderCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &formats, &count);
        encoder->sample_fmt = count > 0 ? static_cast<const AVSampleFormat *>(formats)[0] : AV_SAMPLE_FMT_S16P;
        encoder->time_base = AVRational{1, encoder->sample_rate};
        if (out->oformat->flags & AVFMT_GLOBALHEADER)
            encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (int r = avcodec_open2(encoder.get(), encoderCodec, nullptr); r < 0)
            return errorText(r);
        avcodec_parameters_from_context(outStream->codecpar, encoder.get());
        outStream->time_base = encoder->time_base;
    } else {
        avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        outStream->codecpar->codec_tag = 0;
        outStream->time_base = inStream->time_base;
    }

    setTags(&out->metadata, tags);
    setTags(&outStream->metadata, tags);
    AVStream *cover = nullptr;
    if (!tags.coverJpeg.isEmpty()) {
        if (format == Format::Opus)
            av_dict_set(&outStream->metadata, "METADATA_BLOCK_PICTURE", pictureBlock(tags).constData(), 0);
        else
            cover = addCoverStream(out.get(), tags);
    }

    if (!(out->oformat->flags & AVFMT_NOFILE)) {
        if (int r = avio_open(&out->pb, output.toUtf8().constData(), AVIO_FLAG_WRITE); r < 0)
            return errorText(r);
    }
    AVDictionary *options = nullptr;
    if (format == Format::Mp3)
        av_dict_set(&options, "id3v2_version", "3", 0);
    const int header = avformat_write_header(out.get(), &options);
    av_dict_free(&options);
    if (header < 0)
        return errorText(header);
    if (cover) {
        if (const QString e = writeCover(out.get(), cover, tags); !e.isEmpty())
            return e;
    }

    const QString error = format == Format::Mp3
        ? encodePackets(in.get(), inIndex, decoder.get(), out.get(), outStream, encoder.get())
        : copyPackets(in.get(), inIndex, out.get(), outStream);
    if (!error.isEmpty())
        return error;
    if (int r = av_write_trailer(out.get()); r < 0)
        return errorText(r);
    return {};
}

} // namespace AudioFile
