#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class MpvPlayer;
class Playback;
class UiHost;

// The system's media controls where there is no MPRIS: Windows' (the media
// overlay, media keys and lock screen: SystemMediaTransportControls) and
// macOS' Now Playing (Control Center, the menu bar, media keys). This is the
// core's side; MediaControlsWin.cpp and MediaControlsMac.mm talk to the
// system. Core thread, but the system's buttons may come on any thread.
class MediaControls : public QObject
{
public:
    enum class Button { Play, Pause, Toggle, Stop, Next, Previous };
    // what the system shows
    struct State
    {
        bool hasTrack = false;
        bool playing = false;
        bool canNext = false;
        bool canPrevious = false;
        QString title;
        QString artist;
        QString album;
        QString artUrl;
        double duration = 0;
        double position = 0;
    };

    MediaControls(Playback *playback, MpvPlayer *player, UiHost *host, QObject *parent = nullptr);
    ~MediaControls() override;

    // from the system, on any thread
    void pressed(Button button);
    void seekTo(double seconds);

    // the platform's state (MediaControlsWin.cpp, MediaControlsMac.mm)
    struct System;

private:
    // the platform's part. open() may give nullptr while it cannot start yet
    // (Windows: before the window exists) and is asked again with the next
    // change
    static System *open(MediaControls *owner, void *window);
    static void show(System *system, const State &state);
    static void close(System *system);
#ifdef __APPLE__
    // show() on the main thread
    static void showNow(System *system, const State &state);
#endif

    void update();
    // the position jumped (a seek), more than playing on explains
    void positionMoved();

    Playback *m_playback = nullptr;
    MpvPlayer *m_player = nullptr;
    UiHost *m_host = nullptr;
    System *m_system = nullptr;
    double m_shownPosition = 0;
    QElapsedTimer m_sinceShown;
};
