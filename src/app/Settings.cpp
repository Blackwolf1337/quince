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
const QString kGlowStrength = QStringLiteral("look/glowStrength");
const QString kColoredProgress = QStringLiteral("look/coloredProgress");
const QString kAmbientStrength = QStringLiteral("video/ambientStrength");
const QString kAmbientSize = QStringLiteral("video/ambientSize");
const QString kWindowOpacity = QStringLiteral("look/windowOpacity");
const QString kSkipDisliked = QStringLiteral("player/skipDisliked");
const QString kStartPaused = QStringLiteral("player/startPaused");
const QString kVideoMode = QStringLiteral("video/mode");
const QString kVideoHeight = QStringLiteral("video/height");
const QString kCaptionLanguage = QStringLiteral("video/captions");
const QString kVisualizer = QStringLiteral("look/visualizer");
const QString kDownloadFormat = QStringLiteral("downloads/format");
const QString kDownloadFolder = QStringLiteral("downloads/folder");
const QString kDownloadMp3Kbps = QStringLiteral("downloads/mp3Kbps");
const QString kDownloadSkipExisting = QStringLiteral("downloads/skipExisting");
const QString kDownloadPlaylistMax = QStringLiteral("downloads/playlistMax");
const QString kLyricsGenius = QStringLiteral("lyrics/genius");
const QString kLyricsMegalobiz = QStringLiteral("lyrics/megalobiz");
const QString kLyricsPreferred = QStringLiteral("lyrics/preferred");
const QString kLyricsInexact = QStringLiteral("lyrics/inexact");
const QString kLyricsGapText = QStringLiteral("lyrics/gapText");
const QString kLyricsRomanize = QStringLiteral("lyrics/romanize");
const QString kLyricsChinese = QStringLiteral("lyrics/chinese");
const QString kLyricsEffect = QStringLiteral("lyrics/effect");
const QString kLyricsTimeCodes = QStringLiteral("lyrics/timeCodes");
const QString kLanguage = QStringLiteral("look/language");
const QString kDiscordPauseMinutes = QStringLiteral("discord/pauseMinutes");
const QString kDiscordHideTimeLeft = QStringLiteral("discord/hideTimeLeft");
const QString kDiscordShows = QStringLiteral("discord/shows");
const QString kDiscordButton = QStringLiteral("discord/button");
const QString kSponsorBlock = QStringLiteral("player/sponsorBlock");
const QString kSponsorBlockCategories = QStringLiteral("player/sponsorBlockCategories");
const QString kCompactSidebar = QStringLiteral("look/compactSidebar");
const QString kStartPage = QStringLiteral("look/startPage");
const QString kResumeSession = QStringLiteral("player/resumeSession");
const QString kWindowWidth = QStringLiteral("window/width");
const QString kWindowHeight = QStringLiteral("window/height");
const QString kMiniWidth = QStringLiteral("window/miniWidth");
const QString kClock = QStringLiteral("look/clock");
const QString kClockSeconds = QStringLiteral("look/clockSeconds");
const QString kClockHours = QStringLiteral("look/clockHours");
const QString kTransparentWindow = QStringLiteral("look/transparentWindow");
const QString kNotifications = QStringLiteral("desktop/notifications");
const QString kNotifyOnResume = QStringLiteral("desktop/notifyOnResume");
const QString kNotificationUrgency = QStringLiteral("desktop/notificationUrgency");
const QString kTrayClickPlays = QStringLiteral("desktop/trayClickPlays");
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
int Settings::glowStrength() const { return m_store.value(kGlowStrength, 100).toInt(); }
bool Settings::coloredProgress() const { return m_store.value(kColoredProgress, true).toBool(); }
int Settings::ambientStrength() const { return m_store.value(kAmbientStrength, 60).toInt(); }
int Settings::ambientSize() const { return m_store.value(kAmbientSize, 100).toInt(); }
int Settings::windowOpacity() const { return m_store.value(kWindowOpacity, 80).toInt(); }
void Settings::setGlowStrength(int percent) { set(kGlowStrength, qBound(0, percent, 200)); }
void Settings::setColoredProgress(bool on) { set(kColoredProgress, on); }
void Settings::setAmbientStrength(int percent) { set(kAmbientStrength, qBound(0, percent, 100)); }
void Settings::setAmbientSize(int percent) { set(kAmbientSize, qBound(50, percent, 200)); }
void Settings::setWindowOpacity(int percent) { set(kWindowOpacity, qBound(0, percent, 100)); }
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
bool Settings::notifyOnResume() const { return m_store.value(kNotifyOnResume, false).toBool(); }
QString Settings::notificationUrgency() const
{
    return m_store.value(kNotificationUrgency, QStringLiteral("normal")).toString();
}
bool Settings::trayClickPlays() const { return m_store.value(kTrayClickPlays, false).toBool(); }
void Settings::setNotifyOnResume(bool on) { set(kNotifyOnResume, on); }
void Settings::setNotificationUrgency(const QString &urgency) { set(kNotificationUrgency, urgency); }
void Settings::setTrayClickPlays(bool on) { set(kTrayClickPlays, on); }
void Settings::setSkipDisliked(bool on) { set(kSkipDisliked, on); }
void Settings::setStartPaused(bool on) { set(kStartPaused, on); }
bool Settings::videoMode() const { return m_store.value(kVideoMode, false).toBool(); }
int Settings::videoHeight() const { return m_store.value(kVideoHeight, 1080).toInt(); }
QString Settings::captionLanguage() const { return m_store.value(kCaptionLanguage).toString(); }
void Settings::setVideoMode(bool on) { set(kVideoMode, on); }
void Settings::setVideoHeight(int height) { set(kVideoHeight, height); }
void Settings::setCaptionLanguage(const QString &language) { set(kCaptionLanguage, language); }
QString Settings::visualizer() const { return m_store.value(kVisualizer).toString(); }
void Settings::setVisualizer(const QString &style) { set(kVisualizer, style); }
QString Settings::downloadFormat() const { return m_store.value(kDownloadFormat, QStringLiteral("opus")).toString(); }
QString Settings::downloadFolder() const
{
    const QString folder = m_store.value(kDownloadFolder).toString();
    return !folder.isEmpty() ? folder
                             : QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Quince");
}
void Settings::setDownloadFormat(const QString &format) { set(kDownloadFormat, format); }
void Settings::setDownloadFolder(const QString &folder) { set(kDownloadFolder, folder); }
int Settings::downloadMp3Kbps() const { return m_store.value(kDownloadMp3Kbps, 320).toInt(); }
bool Settings::downloadSkipExisting() const { return m_store.value(kDownloadSkipExisting, true).toBool(); }
int Settings::downloadPlaylistMax() const { return m_store.value(kDownloadPlaylistMax, 0).toInt(); }
void Settings::setDownloadMp3Kbps(int kbps) { set(kDownloadMp3Kbps, qBound(96, kbps, 320)); }
void Settings::setDownloadSkipExisting(bool on) { set(kDownloadSkipExisting, on); }
void Settings::setDownloadPlaylistMax(int tracks) { set(kDownloadPlaylistMax, qMax(0, tracks)); }
bool Settings::lyricsGenius() const { return m_store.value(kLyricsGenius, true).toBool(); }
bool Settings::sponsorBlock() const { return m_store.value(kSponsorBlock, true).toBool(); }
void Settings::setLyricsGenius(bool on) { set(kLyricsGenius, on); }
bool Settings::lyricsMegalobiz() const { return m_store.value(kLyricsMegalobiz, true).toBool(); }
QString Settings::lyricsPreferred() const { return m_store.value(kLyricsPreferred).toString(); }
bool Settings::lyricsInexact() const { return m_store.value(kLyricsInexact, true).toBool(); }
QString Settings::lyricsGapText() const { return m_store.value(kLyricsGapText, QStringLiteral("\u266A")).toString(); }
void Settings::setLyricsMegalobiz(bool on) { set(kLyricsMegalobiz, on); }
void Settings::setLyricsPreferred(const QString &source) { set(kLyricsPreferred, source); }
void Settings::setLyricsInexact(bool on) { set(kLyricsInexact, on); }
void Settings::setLyricsGapText(const QString &text) { set(kLyricsGapText, text); }
bool Settings::lyricsRomanize() const { return m_store.value(kLyricsRomanize, true).toBool(); }
QString Settings::lyricsChinese() const { return m_store.value(kLyricsChinese).toString(); }
QString Settings::lyricsEffect() const { return m_store.value(kLyricsEffect, QStringLiteral("fancy")).toString(); }
bool Settings::lyricsTimeCodes() const { return m_store.value(kLyricsTimeCodes, false).toBool(); }
void Settings::setLyricsRomanize(bool on) { set(kLyricsRomanize, on); }
void Settings::setLyricsChinese(const QString &to) { set(kLyricsChinese, to); }
void Settings::setLyricsEffect(const QString &effect) { set(kLyricsEffect, effect); }
void Settings::setLyricsTimeCodes(bool on) { set(kLyricsTimeCodes, on); }
QString Settings::language() const { return m_store.value(kLanguage).toString(); }
int Settings::discordPauseMinutes() const { return m_store.value(kDiscordPauseMinutes, 10).toInt(); }
bool Settings::discordHideTimeLeft() const { return m_store.value(kDiscordHideTimeLeft, false).toBool(); }
QString Settings::discordShows() const { return m_store.value(kDiscordShows, QStringLiteral("title")).toString(); }
bool Settings::discordButton() const { return m_store.value(kDiscordButton, true).toBool(); }
void Settings::setLanguage(const QString &code) { set(kLanguage, code); }
void Settings::setDiscordPauseMinutes(int minutes) { set(kDiscordPauseMinutes, minutes); }
void Settings::setDiscordHideTimeLeft(bool on) { set(kDiscordHideTimeLeft, on); }
void Settings::setDiscordShows(const QString &shows) { set(kDiscordShows, shows); }
void Settings::setDiscordButton(bool on) { set(kDiscordButton, on); }
void Settings::setSponsorBlock(bool on) { set(kSponsorBlock, on); }
QStringList Settings::sponsorBlockCategories() const
{
    // intros and outros of music videos are often part of the music
    static const QStringList kDefault{QStringLiteral("music_offtopic"), QStringLiteral("sponsor"),
                                      QStringLiteral("selfpromo"), QStringLiteral("interaction")};
    return m_store.value(kSponsorBlockCategories, kDefault).toStringList();
}
void Settings::setSponsorBlockCategories(const QStringList &categories) { set(kSponsorBlockCategories, categories); }
bool Settings::compactSidebar() const { return m_store.value(kCompactSidebar, false).toBool(); }
bool Settings::clock() const { return m_store.value(kClock, false).toBool(); }
bool Settings::transparentWindow() const { return m_store.value(kTransparentWindow, false).toBool(); }
void Settings::setCompactSidebar(bool on) { set(kCompactSidebar, on); }
QString Settings::startPage() const { return m_store.value(kStartPage, QStringLiteral("home")).toString(); }
bool Settings::resumeSession() const { return m_store.value(kResumeSession, true).toBool(); }
int Settings::windowWidth() const { return m_store.value(kWindowWidth, 1280).toInt(); }
int Settings::windowHeight() const { return m_store.value(kWindowHeight, 800).toInt(); }
int Settings::miniWidth() const { return m_store.value(kMiniWidth, 340).toInt(); }
void Settings::setStartPage(const QString &page) { set(kStartPage, page); }
void Settings::setResumeSession(bool on) { set(kResumeSession, on); }
void Settings::setWindowWidth(int width) { set(kWindowWidth, width); }
void Settings::setWindowHeight(int height) { set(kWindowHeight, height); }
void Settings::setMiniWidth(int width) { set(kMiniWidth, width); }
void Settings::setClock(bool on) { set(kClock, on); }
bool Settings::clockSeconds() const { return m_store.value(kClockSeconds, false).toBool(); }
QString Settings::clockHours() const { return m_store.value(kClockHours).toString(); }
void Settings::setClockSeconds(bool on) { set(kClockSeconds, on); }
void Settings::setClockHours(const QString &hours) { set(kClockHours, hours); }
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
