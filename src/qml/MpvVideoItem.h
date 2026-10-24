#pragma once

#include <QPointer>
#include <QQuickFramebufferObject>
#include <QtQml/qqmlregistration.h>

class MpvPlayer;

// Draws what mpv shows (the video, or the visualizer) through mpv's OpenGL
// render API, from the player's active deck. Only one item may draw at a time;
// mpv has one render context per instance.
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("player/MpvPlayer.h")
    QML_ELEMENT

    Q_PROPERTY(MpvPlayer *player READ player WRITE setPlayer NOTIFY playerChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    MpvPlayer *player() const { return m_player; }
    void setPlayer(MpvPlayer *player);

    Renderer *createRenderer() const override;

signals:
    void playerChanged();

private:
    QPointer<MpvPlayer> m_player;
};
