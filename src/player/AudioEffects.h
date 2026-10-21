#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class MpvPlayer;
class Playback;
class Settings;

// What changes the sound on its way out: equalizer, compressor, speed,
// skipping silences and the crossfade between tracks. Kept in the settings and
// applied to mpv as one audio filter chain (af).
class AudioEffects : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The effects are owned by App")

    Q_PROPERTY(bool equalizer READ equalizer WRITE setEqualizer NOTIFY changed)
    // dB per band, at bandFrequencies
    Q_PROPERTY(QVariantList bands READ bands NOTIFY changed)
    Q_PROPERTY(QVariantList bandFrequencies READ bandFrequencies CONSTANT)
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    Q_PROPERTY(bool compressor READ compressor WRITE setCompressor NOTIFY changed)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(bool skipSilences READ skipSilences WRITE setSkipSilences NOTIFY changed)
    // seconds; 0 plays tracks gapless instead
    Q_PROPERTY(double crossfade READ crossfade WRITE setCrossfade NOTIFY changed)

public:
    AudioEffects(MpvPlayer *player, Playback *playback, Settings *settings, QObject *parent = nullptr);

    bool equalizer() const;
    QVariantList bands() const;
    QVariantList bandFrequencies() const;
    QString preset() const;
    QStringList presets() const;
    bool compressor() const;
    double speed() const;
    bool skipSilences() const;
    double crossfade() const;

    void setEqualizer(bool on);
    void setCompressor(bool on);
    void setSpeed(double speed);
    void setSkipSilences(bool on);
    void setCrossfade(double seconds);

    Q_INVOKABLE void setBand(int band, double gainDb);
    // a named set of band gains; "Custom" keeps the bands as they are
    Q_INVOKABLE void applyPreset(const QString &name);
    // translated name of a preset id
    Q_INVOKABLE QString presetName(const QString &id) const;

signals:
    void changed();

private:
    void apply();
    void setBands(const QList<double> &gains, const QString &preset);
    void onSilence(const QVariantMap &values);

    MpvPlayer *m_player = nullptr;
    Playback *m_playback = nullptr;
    Settings *m_settings = nullptr;
    bool m_skipping = false;
};
