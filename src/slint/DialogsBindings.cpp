#include "Areas.h"
#include "FilePicker.h"
#include "ImageCache.h"
#include "ModelMirror.h"
#include "PropertyTable.h"
#include "app/AppController.h"
#include "app/Settings.h"
#include "download/Downloader.h"
#include "player/AudioEffects.h"
#include "player/MpvPlayer.h"
#include "ytm/Account.h"

#include <QUrl>

using namespace bridge;

namespace {

// download rows' covers, in logical pixels
constexpr int kRowArt = 44;

// the fields of ui::SettingsValues (ui/dialogs-data.slint) and their Settings properties
const props::Field<ui::SettingsValues> kSettingsFields[] = {
    { "exponentialVolume", &ui::SettingsValues::exponential_volume },
    { "normalizeLoudness", &ui::SettingsValues::normalize_loudness },
    { "volumeStep", &ui::SettingsValues::volume_step },
    { "audioDevice", &ui::SettingsValues::audio_device },
    { "resumeSession", &ui::SettingsValues::resume_session },
    { "skipDisliked", &ui::SettingsValues::skip_disliked },
    { "startPaused", &ui::SettingsValues::start_paused },
    { "sponsorBlock", &ui::SettingsValues::sponsor_block },
    { "sponsorBlockCategories", &ui::SettingsValues::sponsor_block_categories },
    { "lyricsLrclib", &ui::SettingsValues::lyrics_lrclib },
    { "lyricsMegalobiz", &ui::SettingsValues::lyrics_megalobiz },
    { "lyricsGenius", &ui::SettingsValues::lyrics_genius },
    { "lyricsPreferred", &ui::SettingsValues::lyrics_preferred },
    { "lyricsInexact", &ui::SettingsValues::lyrics_inexact },
    { "lyricsEffect", &ui::SettingsValues::lyrics_effect },
    { "lyricsTimeCodes", &ui::SettingsValues::lyrics_time_codes },
    { "lyricsGapText", &ui::SettingsValues::lyrics_gap_text },
    { "lyricsRomanize", &ui::SettingsValues::lyrics_romanize },
    { "lyricsChinese", &ui::SettingsValues::lyrics_chinese },
    { "language", &ui::SettingsValues::language },
    { "startPage", &ui::SettingsValues::start_page },
    { "compactSidebar", &ui::SettingsValues::compact_sidebar },
    { "albumColors", &ui::SettingsValues::album_colors },
    { "glowStrength", &ui::SettingsValues::glow_strength },
    { "coloredProgress", &ui::SettingsValues::colored_progress },
    { "clock", &ui::SettingsValues::clock },
    { "clockSeconds", &ui::SettingsValues::clock_seconds },
    { "clockHours", &ui::SettingsValues::clock_hours },
    { "transparentWindow", &ui::SettingsValues::transparent_window },
    { "windowOpacity", &ui::SettingsValues::window_opacity },
    { "ambientStrength", &ui::SettingsValues::ambient_strength },
    { "ambientSize", &ui::SettingsValues::ambient_size },
    { "tray", &ui::SettingsValues::tray },
    { "trayClickPlays", &ui::SettingsValues::tray_click_plays },
    { "closeToTray", &ui::SettingsValues::close_to_tray },
    { "notifications", &ui::SettingsValues::notifications },
    { "notifyOnResume", &ui::SettingsValues::notify_on_resume },
    { "notificationUrgency", &ui::SettingsValues::notification_urgency },
    { "globalShortcuts", &ui::SettingsValues::global_shortcuts },
    { "discord", &ui::SettingsValues::discord },
    { "discordShows", &ui::SettingsValues::discord_shows },
    { "discordHideTimeLeft", &ui::SettingsValues::discord_hide_time_left },
    { "discordButton", &ui::SettingsValues::discord_button },
    { "discordPauseMinutes", &ui::SettingsValues::discord_pause_minutes },
    { "discordAppId", &ui::SettingsValues::discord_app_id },
    { "downloadFolder", &ui::SettingsValues::download_folder },
    { "downloadFormat", &ui::SettingsValues::download_format },
    { "downloadMp3Kbps", &ui::SettingsValues::download_mp3_kbps },
    { "downloadSkipExisting", &ui::SettingsValues::download_skip_existing },
    { "downloadPlaylistMax", &ui::SettingsValues::download_playlist_max },
};

// the same for ui::EffectsValues and AudioEffects
const props::Field<ui::EffectsValues> kEffectsFields[] = {
    { "equalizer", &ui::EffectsValues::equalizer },
    { "compressor", &ui::EffectsValues::compressor },
    { "skipSilences", &ui::EffectsValues::skip_silences },
    { "skipSilencesAtStart", &ui::EffectsValues::skip_silences_at_start },
    { "crossfade", &ui::EffectsValues::crossfade },
    { "crossfadeIn", &ui::EffectsValues::crossfade_in },
    { "speed", &ui::EffectsValues::speed },
};

struct ChoiceData
{
    std::string id, name;
};

struct EqualizerData
{
    std::vector<std::pair<float, std::string>> bands; // gain, label
    std::vector<ChoiceData> presets;
    std::string preset, presetName;
};

struct DownloadData
{
    std::string title, subtitle, artUrl, state, error;
    float progress = 0;
};

// what the playlist picker adds; core thread only
struct PickerItem : QObject
{
    using QObject::QObject;
    QVariantMap item;
};

std::vector<ChoiceData> choicesOf(const QVariantList &list, const char *idKey, const char *nameKey)
{
    std::vector<ChoiceData> out;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        out.push_back({ utf8(m.value(QLatin1String(idKey)).toString()), utf8(m.value(QLatin1String(nameKey)).toString()) });
    }
    return out;
}

std::shared_ptr<slint::VectorModel<ui::Choice>> choiceModel(const std::vector<ChoiceData> &list)
{
    auto model = std::make_shared<slint::VectorModel<ui::Choice>>();
    for (const ChoiceData &c : list)
        model->push_back(ui::Choice{ str(c.id), str(c.name) });
    return model;
}

// Keeps a struct property in step with obj's properties through a field
// table; edited (from the dialog) hands the struct's fields back to obj.
template<typename Struct, typename Obj, typename Get, typename Set, typename OnEdited>
void mirror(const Window &window, Obj *obj, std::span<const props::Field<Struct>> fields, Get get, Set set,
            OnEdited onEdited)
{
    bind(window, obj, &Obj::changed, [fields](Obj *o) { return props::read(o, fields); },
         [fields, get, set](ui::MainWindow &w, const std::vector<props::Value> &values) {
        Struct s = get(w);
        props::fill(s, fields, values);
        set(w, s);
    });
    toUi(window, [window, obj, fields, get, onEdited](ui::MainWindow &w) {
        onEdited(w, [window, obj, fields, get] {
            if (auto ui = window.lock())
                toCore(obj, [obj, fields, values = props::collect(get(**ui), fields)] { props::write(obj, fields, values); });
        });
    });
}

void bindSettings(AppController &app, const Window &window)
{
    mirror<ui::SettingsValues>(
        window, app.settings(), std::span(kSettingsFields),
        [](ui::MainWindow &w) { return w.global<ui::App>().get_settings(); },
        [](ui::MainWindow &w, const ui::SettingsValues &s) { w.global<ui::App>().set_settings(s); },
        [](ui::MainWindow &w, auto edited) { w.global<ui::App>().on_settings_edited(edited); });

    MpvPlayer *player = app.player();
    bind(window, player, &MpvPlayer::audioDevicesChanged,
         [](MpvPlayer *p) { return choicesOf(p->audioDevices(), "name", "description"); },
         [](ui::MainWindow &w, const std::vector<ChoiceData> &list) { w.global<ui::App>().set_audio_devices(choiceModel(list)); });
    bind(window, player, &MpvPlayer::audioDeviceChanged, [](MpvPlayer *p) { return utf8(p->audioDevice()); },
         [](ui::MainWindow &w, const std::string &v) { w.global<ui::App>().set_audio_device(str(v)); });

    Settings *settings = app.settings();
    toUi(window, [settings, window](ui::MainWindow &w) {
        w.global<ui::App>().on_choose_download_folder([settings, window] {
            auto ui = window.lock();
            if (!ui)
                return;
            const std::string from(std::string_view((*ui)->global<ui::App>().get_settings().download_folder));
            filepicker::pickFolder(from, [settings](const std::string &path) {
                toCore(settings, [settings, folder = QString::fromStdString(path)] { settings->setDownloadFolder(folder); });
            });
        });
    });
}

void bindEffects(AudioEffects *effects, const Window &window)
{
    mirror<ui::EffectsValues>(
        window, effects, std::span(kEffectsFields),
        [](ui::MainWindow &w) { return w.global<ui::App>().get_effects(); },
        [](ui::MainWindow &w, const ui::EffectsValues &e) { w.global<ui::App>().set_effects(e); },
        [](ui::MainWindow &w, auto edited) { w.global<ui::App>().on_effects_edited(edited); });

    bind(window, effects, &AudioEffects::changed,
         [](AudioEffects *e) {
        EqualizerData d;
        const QVariantList gains = e->bands();
        const QVariantList frequencies = e->bandFrequencies();
        for (qsizetype i = 0; i < frequencies.size(); i++) {
            const int f = frequencies.at(i).toInt();
            const QString label = f >= 1000 ? QString::number(f / 1000.0) + u'k' : QString::number(f);
            d.bands.emplace_back(float(gains.value(i).toDouble()), utf8(label));
        }
        for (const QString &id : e->presets())
            d.presets.push_back({ utf8(id), utf8(e->presetName(id)) });
        d.preset = utf8(e->preset());
        d.presetName = utf8(e->presetName(e->preset()));
        return d;
    },
         [](ui::MainWindow &w, const EqualizerData &d) {
        auto &a = w.global<ui::App>();
        ui::Equalizer eq = a.get_equalizer();
        // rows change in place: a slider being dragged must stay
        if (!eq.bands || eq.bands->row_count() != d.bands.size()) {
            auto bands = std::make_shared<slint::VectorModel<ui::EqualizerBand>>();
            for (const auto &[gain, label] : d.bands)
                bands->push_back(ui::EqualizerBand{ gain, str(label) });
            eq.bands = bands;
        } else {
            for (size_t i = 0; i < d.bands.size(); i++) {
                const ui::EqualizerBand band{ d.bands[i].first, str(d.bands[i].second) };
                if (eq.bands->row_data(i) != band)
                    eq.bands->set_row_data(i, band);
            }
        }
        eq.presets = choiceModel(d.presets);
        eq.preset = str(d.preset);
        eq.preset_name = str(d.presetName);
        a.set_equalizer(eq);
    });

    toUi(window, [effects](ui::MainWindow &w) {
        auto &a = w.global<ui::App>();
        a.on_set_band([effects](int band, float gain) { toCore(effects, [effects, band, gain] { effects->setBand(band, gain); }); });
        a.on_apply_preset([effects](const slint::SharedString &id) {
            toCore(effects, [effects, id = fromSlint(id)] { effects->applyPreset(id); });
        });
    });
}

void bindAccount(AppController &app, const Window &window)
{
    Account *account = app.account();
    bind(window, account, &Account::busyChanged, [](Account *a) { return a->busy(); },
         [](ui::MainWindow &w, bool v) { w.global<ui::Account>().set_busy(v); });
    toUi(window, [browsers = choicesOf(account->browsers(), "id", "name")](ui::MainWindow &w) {
        w.global<ui::Account>().set_browsers(choiceModel(browsers));
    });
    bind(window, account, &Account::playlistsChanged,
         [](Account *a) {
        std::vector<ChoiceData> own; // browse id, title
        for (const QVariant &v : a->playlists()) {
            const QVariantMap p = v.toMap();
            if (p.value(QStringLiteral("owned")).toBool())
                own.push_back({ utf8(p.value(QStringLiteral("browseId")).toString()), utf8(p.value(QStringLiteral("title")).toString()) });
        }
        return own;
    },
         [](ui::MainWindow &w, const std::vector<ChoiceData> &own) {
        auto model = std::make_shared<slint::VectorModel<ui::PlaylistEntry>>();
        for (const ChoiceData &p : own)
            model->push_back(ui::PlaylistEntry{ str(p.name), str(p.id) });
        w.global<ui::Account>().set_own_playlists(model);
    });

    // the dialogs the core asks for; the picker's item stays here
    auto *picked = new PickerItem(&app);
    MpvPlayer *player = app.player();
    QObject::connect(&app, &AppController::dialogRequested, &app,
                     [window, account, player, picked](const QString &name, const QVariantMap &data) {
        if (name == u"addToPlaylist") {
            picked->item = data.value(QStringLiteral("item")).toMap();
            account->reloadPlaylists();
        } else if (name == u"settings")
            player->watchAudioDevices();
        toUi(window, [n = utf8(name)](ui::MainWindow &w) { w.global<ui::App>().set_dialog(str(n)); });
    });

    toUi(window, [account, picked](ui::MainWindow &w) {
        auto &a = w.global<ui::Account>();
        a.on_import_from_browser([account](const slint::SharedString &id) {
            toCore(account, [account, id = fromSlint(id)] { account->importFromBrowser(id); });
        });
        a.on_import_cookies([account](const slint::SharedString &title, const slint::SharedString &filter) {
            filepicker::openFile(std::string(std::string_view(title)), std::string(std::string_view(filter)), "txt",
                                 [account](const std::string &path) {
                toCore(account, [account, file = QUrl::fromLocalFile(QString::fromStdString(path))] {
                    account->importCookies(file);
                });
            });
        });
        a.on_add_to_playlist([account, picked](const slint::SharedString &browseId, const slint::SharedString &title) {
            toCore(account, [account, picked, id = fromSlint(browseId), t = fromSlint(title)] {
                account->addToPlaylist(id, picked->item.value(QStringLiteral("videoId")).toString(), t);
            });
        });
        a.on_create_playlist([account, picked](const slint::SharedString &title) {
            toCore(account, [account, picked, t = fromSlint(title).trimmed()] {
                account->createPlaylist(t, picked->item.value(QStringLiteral("videoId")).toString());
            });
        });
    });
}

void bindDownloads(Downloader *downloads, const Window &window, ImageCache *images)
{
    bind(window, downloads, &Downloader::folderChanged, [](Downloader *d) { return utf8(d->folder()); },
         [](ui::MainWindow &w, const std::string &v) { w.global<ui::App>().set_download_folder(str(v)); });

    ModelMirror<DownloadData, ui::DownloadRow>::start(
        window, downloads,
        [](const QModelIndex &index) {
            QStringList subtitle;
            for (const int role : { Downloader::ArtistRole, Downloader::AlbumRole }) {
                const QString part = index.data(role).toString();
                if (!part.isEmpty())
                    subtitle.append(part);
            }
            return DownloadData{ utf8(index.data(Downloader::TitleRole).toString()),
                                 utf8(subtitle.join(QStringLiteral(" • "))),
                                 utf8(index.data(Downloader::ThumbnailRole).toString()),
                                 utf8(index.data(Downloader::StateRole).toString()),
                                 utf8(index.data(Downloader::ErrorRole).toString()),
                                 float(index.data(Downloader::ProgressRole).toDouble()) };
        },
        [](const DownloadData &d) {
            ui::DownloadRow row;
            row.title = str(d.title);
            row.subtitle = str(d.subtitle);
            row.art_url = str(d.artUrl);
            row.state = str(d.state);
            row.progress = d.progress;
            row.error = str(d.error);
            return row;
        },
        [](ui::MainWindow &w, std::shared_ptr<slint::VectorModel<ui::DownloadRow>> model) {
            w.global<ui::App>().set_downloads(model);
        },
        [images](const std::shared_ptr<slint::VectorModel<ui::DownloadRow>> &model, size_t, const DownloadData &d) {
            loadRowArt(images, model, d.artUrl, kRowArt * 2);
        });

    toUi(window, [downloads](ui::MainWindow &w) {
        auto &a = w.global<ui::App>();
        a.on_open_download_folder([downloads](int row) { toCore(downloads, [downloads, row] { downloads->openFolder(row); }); });
        a.on_retry_download([downloads](int row) { toCore(downloads, [downloads, row] { downloads->retry(row); }); });
        a.on_clear_finished_downloads([downloads] { toCore(downloads, [downloads] { downloads->clearFinished(); }); });
    });
}

} // namespace

void bindDialogs(AppController &app, const bridge::Window &window, ImageCache *images, SlintHost *)
{
    bindSettings(app, window);
    bindEffects(app.effects(), window);
    bindAccount(app, window);
    bindDownloads(app.downloads(), window, images);
}
