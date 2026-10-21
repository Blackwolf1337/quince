#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class InnerTube;
class MpvPlayer;
class Playback;
class Settings;
class QNetworkAccessManager;

// Lyrics of the playing track: time-synced from LRCLIB when it has them,
// otherwise the plain text YouTube Music shows.
class Lyrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lyrics are owned by App")

    // [{time (seconds, -1 when not synced), text}]
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY linesChanged)
    Q_PROPERTY(QString source READ source NOTIFY linesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int currentLine READ currentLine NOTIFY currentLineChanged)

public:
    Lyrics(Playback *playback, MpvPlayer *player, InnerTube *innerTube, Settings *settings, QObject *parent = nullptr);

    QVariantList lines() const { return m_lines; }
    bool synced() const { return m_synced; }
    QString source() const { return m_source; }
    bool loading() const { return m_loading; }
    int currentLine() const { return m_currentLine; }

    // LRC text ("[01:23.45] line") to lines sorted by time
    static QVariantList parseLrc(const QString &lrc);
    // "Song (Official Audio)" -> "Song"
    static QString cleanTitle(const QString &title);

signals:
    void linesChanged();
    void loadingChanged();
    void currentLineChanged();

private:
    void onTrackChanged();
    void updateCurrentLine();
    void fetchLrclib(const QVariantMap &track, int generation);
    void searchLrclib(const QVariantMap &track, int generation);
    void fetchYouTube(const QString &videoId, int generation);
    void setResult(const QVariantList &lines, bool synced, const QString &source);
    void setLoading(bool loading);

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    InnerTube *m_innerTube = nullptr;
    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;

    QString m_videoId;
    int m_generation = 0;
    QVariantList m_lines;
    QList<double> m_times;
    bool m_synced = false;
    QString m_source;
    bool m_loading = false;
    int m_currentLine = -1;
};
