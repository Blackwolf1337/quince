#pragma once

// The core's types under their QML names. The core does not link QtQml, so
// they are registered here as foreign types (BrowsePage: QmlBrowsePage.h).

#include "app/AppController.h"
#include "app/ColorTheme.h"
#include "app/Settings.h"
#include "download/Downloader.h"
#include "integrations/SponsorBlock.h"
#include "lyrics/Lyrics.h"
#include "player/AudioEffects.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/Queue.h"
#include "ytm/Account.h"
#include "ytm/SearchSuggestions.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

struct AppForeign
{
    Q_GADGET
    QML_FOREIGN(AppController)
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

public:
    static AppController *create(QQmlEngine *, QJSEngine *)
    {
        AppController *app = AppController::instance();
        Q_ASSERT(app);
        QJSEngine::setObjectOwnership(app, QJSEngine::CppOwnership);
        return app;
    }
};

struct SettingsForeign
{
    Q_GADGET
    QML_FOREIGN(Settings)
    QML_NAMED_ELEMENT(Settings)
    QML_UNCREATABLE("Settings are owned by App")
};

struct ColorThemeForeign
{
    Q_GADGET
    QML_FOREIGN(ColorTheme)
    QML_NAMED_ELEMENT(ColorTheme)
    QML_UNCREATABLE("The theme is owned by App")
};

struct PlaybackForeign
{
    Q_GADGET
    QML_FOREIGN(Playback)
    QML_NAMED_ELEMENT(Playback)
    QML_UNCREATABLE("Playback is owned by App")
};

struct MpvPlayerForeign
{
    Q_GADGET
    QML_FOREIGN(MpvPlayer)
    QML_NAMED_ELEMENT(MpvPlayer)
    QML_UNCREATABLE("The player is owned by App")
};

struct QueueForeign
{
    Q_GADGET
    QML_FOREIGN(Queue)
    QML_NAMED_ELEMENT(Queue)
    QML_UNCREATABLE("The queue is owned by App")
};

struct AudioEffectsForeign
{
    Q_GADGET
    QML_FOREIGN(AudioEffects)
    QML_NAMED_ELEMENT(AudioEffects)
    QML_UNCREATABLE("The effects are owned by App")
};

struct AccountForeign
{
    Q_GADGET
    QML_FOREIGN(Account)
    QML_NAMED_ELEMENT(Account)
    QML_UNCREATABLE("The account is owned by App")
};

struct LyricsForeign
{
    Q_GADGET
    QML_FOREIGN(Lyrics)
    QML_NAMED_ELEMENT(Lyrics)
    QML_UNCREATABLE("Lyrics are owned by App")
};

struct SponsorBlockForeign
{
    Q_GADGET
    QML_FOREIGN(SponsorBlock)
    QML_NAMED_ELEMENT(SponsorBlock)
    QML_UNCREATABLE("SponsorBlock is owned by App")
};

struct DownloaderForeign
{
    Q_GADGET
    QML_FOREIGN(Downloader)
    QML_NAMED_ELEMENT(Downloader)
    QML_UNCREATABLE("The downloader is owned by App")
};

struct SearchSuggestionsForeign
{
    Q_GADGET
    QML_FOREIGN(SearchSuggestions)
    QML_NAMED_ELEMENT(SearchSuggestions)
};
