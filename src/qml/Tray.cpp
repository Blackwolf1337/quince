#include "qml/Tray.h"
#include "app/Settings.h"
#include "app/UiHost.h"
#include "integrations/Notifications.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace {

const QString kIcon = QStringLiteral(":/qt/qml/Quince/qml/icons/quince.svg");
constexpr int kMessageMs = 5000;

} // namespace

Tray::Tray(Playback *playback, MpvPlayer *player, Settings *settings, UiHost *host, QObject *parent)
    : QObject(parent)
    , m_playback(playback)
    , m_player(player)
    , m_settings(settings)
    , m_host(host)
{
    connect(m_settings, &Settings::changed, this, &Tray::applySettings);
    connect(m_playback, &Playback::trackChanged, this, &Tray::update);
    connect(m_player, &MpvPlayer::pausedChanged, this, &Tray::update);
    applySettings();
}

Tray::~Tray()
{
    delete m_menu;
}

void Tray::applySettings()
{
    const bool want = m_settings->tray() && QSystemTrayIcon::isSystemTrayAvailable();
    if (want && !m_tray)
        create();
    if (m_tray)
        m_tray->setVisible(want);
}

void Tray::create()
{
    m_menu = new QMenu();
    m_nowPlaying = m_menu->addAction(QString());
    m_nowPlaying->setEnabled(false);
    m_menu->addSeparator();
    m_playPause = m_menu->addAction(QString(), m_player, &MpvPlayer::togglePause);
    m_next = m_menu->addAction(QString(), m_playback, &Playback::next);
    m_previous = m_menu->addAction(QString(), m_playback, &Playback::previous);
    m_menu->addSeparator();
    m_show = m_menu->addAction(QString(), this, [this] { m_host->toggleWindow(); });
    m_quit = m_menu->addAction(QString(), qApp, &QCoreApplication::quit);

    m_tray = new QSystemTrayIcon(QIcon(kIcon), this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && m_settings->trayClickPlays())
            m_player->togglePause();
        else if (reason == QSystemTrayIcon::Trigger)
            m_host->toggleWindow();
        else if (reason == QSystemTrayIcon::MiddleClick)
            m_player->togglePause();
    });
    retranslate();
}

void Tray::retranslate()
{
    if (!m_menu)
        return;
    m_next->setText(tr("Next"));
    m_previous->setText(tr("Previous"));
    m_show->setText(tr("Show or hide Quince"));
    m_quit->setText(tr("Quit"));
    update();
}

void Tray::update()
{
    if (!m_tray)
        return;
    const QVariantMap track = m_playback->track();
    const QString title = track.value(QStringLiteral("title")).toString();
    const QString line = title.isEmpty() ? tr("Nothing playing")
                                         : title + QStringLiteral(" - ") + Notifications::artistsOf(track);
    m_nowPlaying->setText(line);
    m_tray->setToolTip(title.isEmpty() ? QStringLiteral("Quince") : line);
    m_playPause->setText(m_player->paused() || m_player->idle() ? tr("Play") : tr("Pause"));
}

bool Tray::showMessage(const QString &title, const QString &body, const QImage &cover)
{
    if (!m_tray || !m_tray->isVisible())
        return false;
    m_tray->showMessage(title, body, cover.isNull() ? QIcon(kIcon) : QIcon(QPixmap::fromImage(cover)), kMessageMs);
    return true;
}
