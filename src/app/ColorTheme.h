#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

class Playback;
class Settings;
class QNetworkAccessManager;

// Colors taken from the cover of the playing track (album-color-theme): the
// accent, and dark tinted backgrounds. They fade over instead of jumping.
class ColorTheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The theme is owned by App")

    Q_PROPERTY(QColor accent READ accent NOTIFY colorsChanged)
    Q_PROPERTY(QColor background READ background NOTIFY colorsChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY colorsChanged)
    Q_PROPERTY(QColor surfaceHigh READ surfaceHigh NOTIFY colorsChanged)
    // the soft light behind header and player bar
    Q_PROPERTY(QColor glow READ glow NOTIFY colorsChanged)

public:
    ColorTheme(Playback *playback, Settings *settings, QObject *parent = nullptr);

    QColor accent() const { return m_current.accent; }
    QColor background() const { return m_current.background; }
    QColor surface() const { return m_current.surface; }
    QColor surfaceHigh() const { return m_current.surfaceHigh; }
    QColor glow() const { return m_current.glow; }

    // most prominent colorful tone of a picture; invalid when it has none
    static QColor dominantColor(const QImage &image);

signals:
    void colorsChanged();
    // the small cover that was analysed, for notifications
    void coverReady(const QString &videoId, const QImage &cover);

private:
    struct Palette
    {
        QColor accent;
        QColor background;
        QColor surface;
        QColor surfaceHigh;
        QColor glow;
    };

    static Palette defaults();
    static Palette fromColor(const QColor &base);
    static Palette mix(const Palette &a, const Palette &b, double t);

    void onTrackChanged();
    void apply();
    void fadeTo(const Palette &target);

    Playback *m_playback = nullptr;
    Settings *m_settings = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QString m_videoId;
    QColor m_base;

    Palette m_from;
    Palette m_to;
    Palette m_current;
    QVariantAnimation m_fade;
};
