#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QTimer>

class MpvPlayer;
class Playback;
class Settings;
class QLocalSocket;

// "Listening to" status in Discord, through the local RPC socket of the
// Discord client (or of Vesktop's arRPC). Needs the id of a Discord
// application, which names the status.
class Discord : public QObject
{
    Q_OBJECT

public:
    Discord(Playback *playback, MpvPlayer *player, Settings *settings, QObject *parent = nullptr);

private:
    enum Opcode : quint32 {
        Handshake = 0,
        Frame = 1,
        Close = 2,
    };

    void applySettings();
    void connectNext();
    void disconnectNow();
    void onConnected();
    void onReadyRead();
    void send(quint32 opcode, const QByteArray &json);
    void scheduleUpdate();
    void sendActivity();
    void onPausedChanged();
    static QStringList socketCandidates();

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    Settings *m_settings = nullptr;

    QLocalSocket *m_socket = nullptr;
    QString m_appId;
    QStringList m_candidates;
    bool m_ready = false;
    QByteArray m_buffer;
    QTimer m_retry;
    QTimer m_update;
    // runs while paused; once it fires the status is cleared until playback goes on
    QTimer m_pauseTimeout;
    bool m_pausedTooLong = false;
    double m_lastPosition = 0;
};
