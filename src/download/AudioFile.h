#pragma once

#include <QByteArray>
#include <QString>

// Writes a downloaded YouTube stream as an audio file with tags and cover,
// through libavformat and libavcodec (the FFmpeg that mpv brings along).
namespace AudioFile {

enum class Format {
    Opus, // the Opus stream copied into Ogg (.opus)
    M4a, // the AAC stream copied into MP4 (.m4a)
    Mp3, // encoded (.mp3)
};

struct Tags
{
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString year;
    int track = 0;
    QByteArray coverJpeg; // empty: no cover
    int coverWidth = 0;
    int coverHeight = 0;
};

QString extension(Format format);

// Blocks (a few seconds for MP3); run it on a worker thread. Returns an
// error text, empty on success.
QString write(const QString &input, const QString &output, Format format, const Tags &tags, int mp3Kbps = 320);

} // namespace AudioFile
