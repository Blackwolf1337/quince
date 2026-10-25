// Windows' media controls: SystemMediaTransportControls for the main window,
// through the WinRT ABI (MinGW has no C++/WinRT). Windows 10 and newer.
#include "integrations/MediaControls.h"

#include <QLoggingCategory>

// the GUIDs of the WinRT headers below defined here, not only declared
#include <initguid.h>
// MinGW's headers specialize IReference for boolean and for BYTE, both
// unsigned char to C++: the boolean one stays out (unused here)
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.foundation.h>
#include <windows.media.h>
#include <windows.storage.streams.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <atomic>

using namespace ABI::Windows::Media;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace wf = ABI::Windows::Foundation;
namespace ws = ABI::Windows::Storage::Streams;

Q_LOGGING_CATEGORY(lcMediaControls, "quince.mediacontrols")

namespace {

using ButtonHandler = wf::ITypedEventHandler<SystemMediaTransportControls *, SystemMediaTransportControlsButtonPressedEventArgs *>;
using SeekHandler = wf::ITypedEventHandler<SystemMediaTransportControls *, PlaybackPositionChangeRequestedEventArgs *>;

// A handler of the system's events, which come on its own threads (agile).
// Calls back into MediaControls, which takes them to the core thread.
template<typename Interface, const IID &Iid, typename Args, void (*Handle)(MediaControls *, Args *)>
class Handler : public Interface, public IAgileObject
{
public:
    explicit Handler(MediaControls *owner) : m_owner(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (riid == IID_IUnknown || riid == Iid)
            *object = static_cast<Interface *>(this);
        else if (riid == IID_IAgileObject)
            *object = static_cast<IAgileObject *>(this);
        else {
            *object = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ISystemMediaTransportControls *, Args *args) override
    {
        Handle(m_owner, args);
        return S_OK;
    }

private:
    virtual ~Handler() = default;
    std::atomic<ULONG> m_refs = 1;
    MediaControls *m_owner;
};

void onButton(MediaControls *owner, ISystemMediaTransportControlsButtonPressedEventArgs *args)
{
    SystemMediaTransportControlsButton button;
    if (FAILED(args->get_Button(&button)))
        return;
    switch (button) {
    case SystemMediaTransportControlsButton_Play:
        owner->pressed(MediaControls::Button::Play);
        break;
    case SystemMediaTransportControlsButton_Pause:
        owner->pressed(MediaControls::Button::Pause);
        break;
    case SystemMediaTransportControlsButton_Stop:
        owner->pressed(MediaControls::Button::Stop);
        break;
    case SystemMediaTransportControlsButton_Next:
        owner->pressed(MediaControls::Button::Next);
        break;
    case SystemMediaTransportControlsButton_Previous:
        owner->pressed(MediaControls::Button::Previous);
        break;
    default:
        break;
    }
}

void onSeek(MediaControls *owner, IPlaybackPositionChangeRequestedEventArgs *args)
{
    wf::TimeSpan position;
    if (SUCCEEDED(args->get_RequestedPlaybackPosition(&position)))
        owner->seekTo(position.Duration / 1e7);
}

// WinRT's TimeSpan: 100 ns steps
wf::TimeSpan timeSpan(double seconds)
{
    return { INT64(seconds * 1e7) };
}

HString hstring(const QString &text)
{
    HString s;
    s.Set(reinterpret_cast<const wchar_t *>(text.utf16()), UINT32(text.size()));
    return s;
}

template<typename T>
HRESULT factory(const wchar_t *runtimeClass, ComPtr<T> &out)
{
    return RoGetActivationFactory(HStringReference(runtimeClass).Get(), __uuidof(T), &out);
}

} // namespace

struct MediaControls::System
{
    ComPtr<ISystemMediaTransportControls> controls;
    ComPtr<ISystemMediaTransportControls2> controls2;
    EventRegistrationToken buttonToken {};
    EventRegistrationToken seekToken {};
    QString shownArt;
};

MediaControls::System *MediaControls::open(MediaControls *owner, void *window)
{
    if (!window)
        return nullptr;
    // the core thread's apartment; Qt may have made it single-threaded already
    const HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        qCWarning(lcMediaControls) << "RoInitialize failed" << Qt::hex << ulong(init);

    ComPtr<ISystemMediaTransportControlsInterop> interop;
    auto *system = new System;
    HRESULT hr = factory(RuntimeClass_Windows_Media_SystemMediaTransportControls, interop);
    if (SUCCEEDED(hr))
        hr = interop->GetForWindow(static_cast<HWND>(window), __uuidof(ISystemMediaTransportControls), &system->controls);
    if (FAILED(hr)) {
        qCWarning(lcMediaControls) << "no media controls for the window" << Qt::hex << ulong(hr);
        // kept without controls, showing nothing: not tried again with every change
        system->controls.Reset();
        return system;
    }
    system->controls->put_IsEnabled(true);
    system->controls->put_IsPlayEnabled(true);
    system->controls->put_IsPauseEnabled(true);
    system->controls->put_IsStopEnabled(true);
    auto *buttons = new Handler<ButtonHandler, IID___FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs,
                                ISystemMediaTransportControlsButtonPressedEventArgs, onButton>(owner);
    system->controls->add_ButtonPressed(buttons, &system->buttonToken);
    buttons->Release();
    // the timeline (Windows 10 1607): the position bar and seeking in it
    if (SUCCEEDED(system->controls.As(&system->controls2))) {
        auto *seek = new Handler<SeekHandler, IID___FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs,
                                 IPlaybackPositionChangeRequestedEventArgs, onSeek>(owner);
        system->controls2->add_PlaybackPositionChangeRequested(seek, &system->seekToken);
        seek->Release();
    }
    return system;
}

void MediaControls::show(System *system, const State &state)
{
    if (!system->controls)
        return;
    ISystemMediaTransportControls *controls = system->controls.Get();
    controls->put_PlaybackStatus(!state.hasTrack ? MediaPlaybackStatus_Closed
                                 : state.playing ? MediaPlaybackStatus_Playing
                                                 : MediaPlaybackStatus_Paused);
    controls->put_IsNextEnabled(state.canNext);
    controls->put_IsPreviousEnabled(state.canPrevious);

    ComPtr<ISystemMediaTransportControlsDisplayUpdater> display;
    if (FAILED(controls->get_DisplayUpdater(&display)))
        return;
    if (!state.hasTrack) {
        display->ClearAll();
        system->shownArt.clear();
        return;
    }
    display->put_Type(MediaPlaybackType_Music);
    ComPtr<IMusicDisplayProperties> music;
    if (SUCCEEDED(display->get_MusicProperties(&music))) {
        music->put_Title(hstring(state.title).Get());
        music->put_Artist(hstring(state.artist).Get());
        ComPtr<IMusicDisplayProperties2> music2;
        if (SUCCEEDED(music.As(&music2)))
            music2->put_AlbumTitle(hstring(state.album).Get());
    }
    // the cover straight from its URL; Windows fetches it
    if (state.artUrl != system->shownArt) {
        system->shownArt = state.artUrl;
        ComPtr<wf::IUriRuntimeClassFactory> uris;
        ComPtr<wf::IUriRuntimeClass> uri;
        ComPtr<ws::IRandomAccessStreamReferenceStatics> streams;
        ComPtr<ws::IRandomAccessStreamReference> art;
        if (!state.artUrl.isEmpty() && SUCCEEDED(factory(RuntimeClass_Windows_Foundation_Uri, uris))
            && SUCCEEDED(uris->CreateUri(hstring(state.artUrl).Get(), &uri))
            && SUCCEEDED(factory(RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference, streams))
            && SUCCEEDED(streams->CreateFromUri(uri.Get(), &art)))
            display->put_Thumbnail(art.Get());
        else
            display->put_Thumbnail(nullptr);
    }
    display->Update();

    if (system->controls2) {
        ComPtr<IInspectable> instance;
        ComPtr<ISystemMediaTransportControlsTimelineProperties> timeline;
        if (SUCCEEDED(RoActivateInstance(HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties).Get(),
                                         &instance))
            && SUCCEEDED(instance.As(&timeline))) {
            timeline->put_StartTime(timeSpan(0));
            timeline->put_EndTime(timeSpan(state.duration));
            timeline->put_MinSeekTime(timeSpan(0));
            timeline->put_MaxSeekTime(timeSpan(state.duration));
            timeline->put_Position(timeSpan(state.position));
            system->controls2->UpdateTimelineProperties(timeline.Get());
        }
    }
}

void MediaControls::close(System *system)
{
    if (system->controls) {
        system->controls->remove_ButtonPressed(system->buttonToken);
        if (system->controls2)
            system->controls2->remove_PlaybackPositionChangeRequested(system->seekToken);
        system->controls->put_IsEnabled(false);
    }
    delete system;
}
