// macOS' media controls: Now Playing (Control Center, the menu bar) and the
// remote commands of the media keys. Built with ARC. Everything but open()
// happens on the main thread: the core thread hands it over.
#include "integrations/MediaControls.h"

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

struct MediaControls::System
{
    // the handlers' tokens, per command, for removing them again
    NSMutableArray *commands = [NSMutableArray array];
    NSMutableArray *tokens = [NSMutableArray array];
    NSMutableDictionary *info = [NSMutableDictionary dictionary];
    QString shownArt;
};

namespace {

// main thread: the open System, so late blocks of a closed one do nothing
MediaControls::System *g_current = nullptr;

void add(MediaControls::System *system, MPRemoteCommand *command, MPRemoteCommandHandlerStatus (^handler)(MPRemoteCommandEvent *))
{
    command.enabled = YES;
    [system->commands addObject:command];
    [system->tokens addObject:[command addTargetWithHandler:handler]];
}

NSString *ns(const QString &text)
{
    return text.toNSString();
}

} // namespace

MediaControls::System *MediaControls::open(MediaControls *owner, void *)
{
    auto *system = new System;
    MPRemoteCommandCenter *center = [MPRemoteCommandCenter sharedCommandCenter];
    auto press = ^(MediaControls::Button button) {
        return ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
            owner->pressed(button);
            return MPRemoteCommandHandlerStatusSuccess;
        };
    };
    add(system, center.playCommand, press(Button::Play));
    add(system, center.pauseCommand, press(Button::Pause));
    add(system, center.togglePlayPauseCommand, press(Button::Toggle));
    add(system, center.stopCommand, press(Button::Stop));
    add(system, center.nextTrackCommand, press(Button::Next));
    add(system, center.previousTrackCommand, press(Button::Previous));
    add(system, center.changePlaybackPositionCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
        owner->seekTo(static_cast<MPChangePlaybackPositionCommandEvent *>(event).positionTime);
        return MPRemoteCommandHandlerStatusSuccess;
    });
    dispatch_async(dispatch_get_main_queue(), ^{
        g_current = system;
    });
    return system;
}

void MediaControls::show(System *system, const State &state)
{
    const State copy = state;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (system == g_current)
            showNow(system, copy);
    });
}

void MediaControls::showNow(System *system, const State &state)
{
    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    MPRemoteCommandCenter *commands = [MPRemoteCommandCenter sharedCommandCenter];
    commands.nextTrackCommand.enabled = state.canNext;
    commands.previousTrackCommand.enabled = state.canPrevious;
    if (!state.hasTrack) {
        [system->info removeAllObjects];
        system->shownArt.clear();
        center.nowPlayingInfo = nil;
        center.playbackState = MPNowPlayingPlaybackStateStopped;
        return;
    }
    NSMutableDictionary *info = system->info;
    info[MPMediaItemPropertyTitle] = ns(state.title);
    info[MPMediaItemPropertyArtist] = ns(state.artist);
    info[MPMediaItemPropertyAlbumTitle] = ns(state.album);
    info[MPMediaItemPropertyPlaybackDuration] = @(state.duration);
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(state.position);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(state.playing ? 1.0 : 0.0);
    if (state.artUrl != system->shownArt) {
        system->shownArt = state.artUrl;
        [info removeObjectForKey:MPMediaItemPropertyArtwork];
        // the cover fetched in the background; set when it is still this track's
        NSURL *url = state.artUrl.isEmpty() ? nil : [NSURL URLWithString:ns(state.artUrl)];
        const QString wanted = state.artUrl;
        if (url) {
            [[[NSURLSession sharedSession] dataTaskWithURL:url
                                         completionHandler:^(NSData *data, NSURLResponse *, NSError *) {
                NSImage *image = data ? [[NSImage alloc] initWithData:data] : nil;
                if (!image)
                    return;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (system != g_current || system->shownArt != wanted)
                        return;
                    system->info[MPMediaItemPropertyArtwork] =
                        [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                        requestHandler:^NSImage *(CGSize) { return image; }];
                    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = system->info;
                });
            }] resume];
        }
    }
    center.nowPlayingInfo = info;
    center.playbackState = state.playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

void MediaControls::close(System *system)
{
    for (NSUInteger i = 0; i < system->commands.count; i++)
        [system->commands[i] removeTarget:system->tokens[i]];
    // not waited for: at the end the main thread may be waiting for this one
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_current == system)
            g_current = nullptr;
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
        delete system;
    });
}
