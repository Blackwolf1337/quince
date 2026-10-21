#include "player/AudioEffects.h"
#include "app/Settings.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEffects, "quince.effects")

namespace {

const QString kEqualizer = QStringLiteral("effects/equalizer");
const QString kBands = QStringLiteral("effects/bands");
const QString kPreset = QStringLiteral("effects/preset");
const QString kCompressor = QStringLiteral("effects/compressor");
const QString kSpeed = QStringLiteral("effects/speed");
const QString kSkipSilences = QStringLiteral("effects/skipSilences");
const QString kCrossfade = QStringLiteral("effects/crossfade");

// ten octave bands, as in most players
const QList<int> kFrequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

struct Preset
{
    const char *id;
    QList<double> gains;
};

const QList<Preset> kPresets{
    {"flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"bass", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"treble", {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
    {"vocal", {-2, -2, -1, 1, 3, 4, 3, 1, 0, -1}},
    {"loudness", {5, 4, 2, 0, -1, 0, 0, 2, 4, 5}},
    {"rock", {4, 3, 2, 0, -1, -1, 1, 2, 3, 4}},
    {"electronic", {5, 4, 1, 0, -2, 1, 0, 1, 4, 5}},
    {"acoustic", {3, 3, 2, 1, 1, 1, 2, 3, 2, 1}},
    {"night", {-3, -2, -1, 0, 1, 2, 2, 1, -1, -3}},
};

// silence played this much faster while skip-silences runs over it
constexpr double kSilenceSpeed = 10.0;

} // namespace

AudioEffects::AudioEffects(MpvPlayer *player, Playback *playback, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_playback(playback)
    , m_settings(settings)
{
    connect(m_player, &MpvPlayer::silenceMetadata, this, &AudioEffects::onSilence);
    // a new track never starts at the speed of a silence
    connect(m_player, &MpvPlayer::trackStarted, this, [this] {
        m_skipping = false;
        m_player->setSpeedBoost(1.0);
    });
    apply();
}

bool AudioEffects::equalizer() const { return m_settings->value(kEqualizer, false).toBool(); }
QString AudioEffects::preset() const { return m_settings->value(kPreset, QStringLiteral("flat")).toString(); }
bool AudioEffects::compressor() const { return m_settings->value(kCompressor, false).toBool(); }
double AudioEffects::speed() const { return m_settings->value(kSpeed, 1.0).toDouble(); }
bool AudioEffects::skipSilences() const { return m_settings->value(kSkipSilences, false).toBool(); }
double AudioEffects::crossfade() const { return m_settings->value(kCrossfade, 0.0).toDouble(); }

QVariantList AudioEffects::bands() const
{
    QVariantList out = m_settings->value(kBands).toList();
    while (out.size() < kFrequencies.size())
        out.append(0.0);
    return out.mid(0, kFrequencies.size());
}

QVariantList AudioEffects::bandFrequencies() const
{
    QVariantList out;
    for (int f : kFrequencies)
        out.append(f);
    return out;
}

QStringList AudioEffects::presets() const
{
    QStringList out;
    for (const Preset &preset : kPresets)
        out.append(QString::fromLatin1(preset.id));
    out.append(QStringLiteral("custom"));
    return out;
}

QString AudioEffects::presetName(const QString &id) const
{
    if (id == u"flat") return tr("Flat");
    if (id == u"bass") return tr("Bass");
    if (id == u"treble") return tr("Treble");
    if (id == u"vocal") return tr("Vocal");
    if (id == u"loudness") return tr("Loudness");
    if (id == u"rock") return tr("Rock");
    if (id == u"electronic") return tr("Electronic");
    if (id == u"acoustic") return tr("Acoustic");
    if (id == u"night") return tr("Night (quiet ends)");
    return tr("Custom");
}

void AudioEffects::setEqualizer(bool on) { m_settings->set(kEqualizer, on); apply(); }
void AudioEffects::setCompressor(bool on) { m_settings->set(kCompressor, on); apply(); }
void AudioEffects::setSkipSilences(bool on) { m_settings->set(kSkipSilences, on); apply(); }

void AudioEffects::setSpeed(double speed)
{
    m_settings->set(kSpeed, qBound(0.5, std::round(speed * 20) / 20, 2.0));
    apply();
}

void AudioEffects::setCrossfade(double seconds)
{
    m_settings->set(kCrossfade, qBound(0.0, std::round(seconds), 12.0));
    apply();
}

void AudioEffects::setBand(int band, double gainDb)
{
    QList<double> gains;
    for (const QVariant &v : bands())
        gains.append(v.toDouble());
    if (band < 0 || band >= gains.size())
        return;
    gains[band] = qBound(-12.0, std::round(gainDb * 2) / 2, 12.0);
    setBands(gains, QStringLiteral("custom"));
}

void AudioEffects::applyPreset(const QString &name)
{
    for (const Preset &preset : kPresets) {
        if (name == QLatin1StringView(preset.id)) {
            setBands(preset.gains, name);
            return;
        }
    }
    m_settings->set(kPreset, QStringLiteral("custom"));
    emit changed();
}

void AudioEffects::setBands(const QList<double> &gains, const QString &preset)
{
    QVariantList list;
    for (double g : gains)
        list.append(g);
    m_settings->set(kBands, list);
    m_settings->set(kPreset, preset);
    apply();
}

void AudioEffects::apply()
{
    QStringList chain;
    if (equalizer()) {
        QStringList filters;
        const QVariantList gains = bands();
        for (qsizetype i = 0; i < kFrequencies.size(); ++i) {
            const double gain = gains.at(i).toDouble();
            if (gain != 0)
                filters.append(QStringLiteral("equalizer=f=%1:t=o:w=1:g=%2").arg(kFrequencies.at(i)).arg(gain));
        }
        // boosts would clip; the limiter keeps the peaks below full scale
        if (!filters.isEmpty()) {
            filters.append(QStringLiteral("alimiter=limit=0.95:level=false"));
            chain.append(QStringLiteral("@eq:lavfi=[%1]").arg(filters.join(u',')));
        }
    }
    if (compressor()) {
        // -21 dB threshold, 3:1, a little make-up gain
        chain.append(QStringLiteral(
            "@compressor:lavfi=[acompressor=threshold=0.089:ratio=3:attack=20:release=250:makeup=1.6]"));
    }
    if (skipSilences())
        chain.append(QStringLiteral("@silence:lavfi=[silencedetect=noise=-50dB:d=1.5]"));

    const QString filters = chain.join(u',');
    qCDebug(lcEffects) << "af" << filters << "speed" << speed() << "crossfade" << crossfade();
    m_player->setFilters(filters);
    m_player->setSpeed(speed());
    m_playback->setCrossfade(crossfade());
    if (!skipSilences() && m_skipping) {
        m_skipping = false;
        m_player->setSpeedBoost(1.0);
    }
    emit changed();
}

// silencedetect marks the frame where a silence of d seconds began, and the
// one where it ended; in between the track plays much faster
void AudioEffects::onSilence(const QVariantMap &values)
{
    if (!skipSilences())
        return;
    if (values.contains(QStringLiteral("lavfi.silence_start")) && !m_skipping) {
        m_skipping = true;
        qCDebug(lcEffects) << "silence from" << values.value(QStringLiteral("lavfi.silence_start")).toString();
        m_player->setSpeedBoost(kSilenceSpeed);
    } else if (values.contains(QStringLiteral("lavfi.silence_end")) && m_skipping) {
        m_skipping = false;
        qCDebug(lcEffects) << "silence until" << values.value(QStringLiteral("lavfi.silence_end")).toString();
        m_player->setSpeedBoost(1.0);
    }
}
