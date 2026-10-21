#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QtQml/qqmlregistration.h>

class MpvPlayer;
class Playback;
class Settings;
class QNetworkAccessManager;

// sponsorblock: skips the parts of a music video that are no music (intros,
// talking, sponsor spots), from the SponsorBlock database. The lookup sends
// only the first four characters of the video id's SHA-256, so the server
// does not learn which video plays.
class SponsorBlock : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SponsorBlock is owned by App")

public:
    SponsorBlock(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent = nullptr);

signals:
    // a part was skipped; text says which kind
    void skipped(const QString &text);

private:
    struct Segment
    {
        double start = 0;
        double end = 0;
        QString category;
        bool done = false;
    };

    void onStream(const QString &videoId);
    void onPosition();
    QString describe(const QString &category) const;

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QString m_videoId;
    QList<Segment> m_segments;
};
