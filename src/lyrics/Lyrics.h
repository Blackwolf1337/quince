#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class InnerTube;
class MpvPlayer;
class Playback;
class Settings;
class QNetworkAccessManager;
class Romanizer;

// Lyrics of the playing track: time-synced from LRCLIB or Megalobiz when they
// have them, otherwise the plain text YouTube Music or Genius shows.
class Lyrics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lyrics are owned by App")

    // [{time (seconds, -1 when not synced), text, romanized ("" none)}]
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY linesChanged)
    Q_PROPERTY(QString source READ source NOTIFY linesChanged)
    // the page of the lyrics at their source ("" none); Megalobiz asks for a link back
    Q_PROPERTY(QString sourceUrl READ sourceUrl NOTIFY linesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int currentLine READ currentLine NOTIFY currentLineChanged)

public:
    Lyrics(Playback *playback, MpvPlayer *player, InnerTube *innerTube, Settings *settings, QObject *parent = nullptr);

    QVariantList lines() const { return m_lines; }
    bool synced() const { return m_synced; }
    QString source() const { return m_source; }
    QString sourceUrl() const { return m_sourceUrl; }
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
    // asks the next source in m_order, or ends with no lyrics
    void tryNext(const QVariantMap &track, int generation);
    void fetchLrclib(const QVariantMap &track, int generation);
    void searchLrclib(const QVariantMap &track, int generation);
    void fetchYouTube(const QVariantMap &track, int generation);
    void fetchGenius(const QVariantMap &track, int generation);
    void fetchMegalobiz(const QVariantMap &track, int generation);
    void setResult(const QVariantList &lines, bool synced, const QString &source, const QString &sourceUrl = {});
    void setLoading(bool loading);
    // Chinese conversion and romanization of m_raw, on the romanizer's thread
    void applyScripts();
    void onScripts(int request, const QStringList &texts, const QStringList &romanized);

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    InnerTube *m_innerTube = nullptr;
    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    Romanizer *m_romanizer = nullptr;

    QString m_videoId;
    bool m_titled = false;
    int m_generation = 0;
    QVariantList m_lines;
    // the lines as the source gave them
    QVariantList m_raw;
    // the newest romanizer request, and the settings it was made with
    int m_scriptRequest = 0;
    bool m_romanized = false;
    QString m_chinese;
    QList<double> m_times;
    bool m_synced = false;
    QString m_source;
    QString m_sourceUrl;
    // sources still to ask for the playing track, and how far it got
    QStringList m_order;
    qsizetype m_step = 0;
    bool m_loading = false;
    int m_currentLine = -1;
};
