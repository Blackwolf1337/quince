#include "app/Settings.h"

#include <QStandardPaths>

#include <cmath>

namespace {

const QString kVolume = QStringLiteral("player/volume");
const QString kExponential = QStringLiteral("player/exponentialVolume");
const QString kVolumeStep = QStringLiteral("player/volumeStep");
const QString kAudioDevice = QStringLiteral("player/audioDevice");
const QString kNormalize = QStringLiteral("player/normalizeLoudness");
const QString kAlbumColors = QStringLiteral("look/albumColors");
const QString kSkipDisliked = QStringLiteral("player/skipDisliked");
const QString kStartPaused = QStringLiteral("player/startPaused");
const QString kCompactSidebar = QStringLiteral("look/compactSidebar");
const QString kClock = QStringLiteral("look/clock");
const QString kTransparentWindow = QStringLiteral("look/transparentWindow");
const QString kNotifications = QStringLiteral("desktop/notifications");
const QString kTray = QStringLiteral("desktop/tray");
const QString kCloseToTray = QStringLiteral("desktop/closeToTray");
const QString kDiscord = QStringLiteral("discord/enabled");
const QString kDiscordAppId = QStringLiteral("discord/appId");
const QString kLyricsLrclib = QStringLiteral("lyrics/lrclib");
const QString kGlobalShortcuts = QStringLiteral("desktop/globalShortcuts");
// Pear Desktop's Discord application, so the status reads "Listening to" the same
const QString kDefaultDiscordAppId = QStringLiteral("1177081335727267940");

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_store(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/quince.conf"),
              QSettings::IniFormat)
{
}

void Settings::set(const QString &key, const QVariant &value)
{
    if (m_store.value(key) == value)
        return;
    m_store.setValue(key, value);
    emit changed();
}

double Settings::volume() const { return m_store.value(kVolume, 80.0).toDouble(); }
bool Settings::exponentialVolume() const { return m_store.value(kExponential, true).toBool(); }
double Settings::volumeStep() const { return m_store.value(kVolumeStep, 2.0).toDouble(); }
QString Settings::audioDevice() const { return m_store.value(kAudioDevice, QStringLiteral("auto")).toString(); }
bool Settings::normalizeLoudness() const { return m_store.value(kNormalize, true).toBool(); }
bool Settings::albumColors() const { return m_store.value(kAlbumColors, true).toBool(); }
bool Settings::notifications() const { return m_store.value(kNotifications, true).toBool(); }
bool Settings::tray() const { return m_store.value(kTray, true).toBool(); }
bool Settings::closeToTray() const { return m_store.value(kCloseToTray, false).toBool(); }
bool Settings::discord() const { return m_store.value(kDiscord, true).toBool(); }
QString Settings::discordAppId() const
{
    const QString id = m_store.value(kDiscordAppId).toString();
    return id.isEmpty() ? kDefaultDiscordAppId : id;
}
bool Settings::lyricsLrclib() const { return m_store.value(kLyricsLrclib, true).toBool(); }
bool Settings::globalShortcuts() const { return m_store.value(kGlobalShortcuts, true).toBool(); }

void Settings::setVolume(double volume) { set(kVolume, qBound(0.0, volume, 100.0)); }
void Settings::setExponentialVolume(bool on) { set(kExponential, on); }
void Settings::setVolumeStep(double step) { set(kVolumeStep, qBound(0.1, step, 25.0)); }
void Settings::setAudioDevice(const QString &device) { set(kAudioDevice, device); }
void Settings::setNormalizeLoudness(bool on) { set(kNormalize, on); }
void Settings::setAlbumColors(bool on) { set(kAlbumColors, on); }
void Settings::setNotifications(bool on) { set(kNotifications, on); }
void Settings::setTray(bool on) { set(kTray, on); }
void Settings::setCloseToTray(bool on) { set(kCloseToTray, on); }
void Settings::setDiscord(bool on) { set(kDiscord, on); }
void Settings::setDiscordAppId(const QString &id) { set(kDiscordAppId, id.trimmed()); }
void Settings::setLyricsLrclib(bool on) { set(kLyricsLrclib, on); }
void Settings::setGlobalShortcuts(bool on) { set(kGlobalShortcuts, on); }
bool Settings::skipDisliked() const { return m_store.value(kSkipDisliked, false).toBool(); }
bool Settings::startPaused() const { return m_store.value(kStartPaused, false).toBool(); }
void Settings::setSkipDisliked(bool on) { set(kSkipDisliked, on); }
void Settings::setStartPaused(bool on) { set(kStartPaused, on); }
bool Settings::compactSidebar() const { return m_store.value(kCompactSidebar, false).toBool(); }
bool Settings::clock() const { return m_store.value(kClock, false).toBool(); }
bool Settings::transparentWindow() const { return m_store.value(kTransparentWindow, false).toBool(); }
void Settings::setCompactSidebar(bool on) { set(kCompactSidebar, on); }
void Settings::setClock(bool on) { set(kClock, on); }
void Settings::setTransparentWindow(bool on) { set(kTransparentWindow, on); }

// Loudness is heard roughly logarithmically; a cubic curve makes the lower
// half of the slider usable instead of jumping from silent to loud.
double Settings::toPlayerVolume(double slider) const
{
    const double s = qBound(0.0, slider, 100.0);
    return exponentialVolume() ? 100.0 * std::pow(s / 100.0, 3.0) : s;
}

double Settings::toSlider(double playerVolume) const
{
    const double v = qBound(0.0, playerVolume, 100.0);
    return exponentialVolume() ? 100.0 * std::cbrt(v / 100.0) : v;
}
