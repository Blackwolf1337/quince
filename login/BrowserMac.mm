// The sign-in window on macOS: WKWebView with a non-persistent data store (in
// memory only). Built with ARC. The user agent ends like Safari's, the browser
// of the same engine, as WebKitGTK's does (let past Google's sign-in, see
// BrowserGtk.cpp).
#include "Browser.h"

#include <QDateTime>
#include <QTimeZone>

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

namespace browser {
class MacBrowser;
}

@interface QuinceLoginDelegate : NSObject <WKNavigationDelegate, NSWindowDelegate>
@property(nonatomic, assign) browser::MacBrowser *owner;
@end

namespace browser {

class MacBrowser final : public Window
{
public:
    explicit MacBrowser(const Options &options)
    {
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        config.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        config.applicationNameForUserAgent = @"Version/17.0 Safari/605.1.15";
        const NSRect frame = NSMakeRect(0, 0, 520, 720);
        m_view = [[WKWebView alloc] initWithFrame:frame configuration:config];
        m_delegate = [[QuinceLoginDelegate alloc] init];
        m_delegate.owner = this;
        m_view.navigationDelegate = m_delegate;
        m_window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                         | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        m_window.title = options.title.toNSString();
        m_window.contentView = m_view;
        m_window.delegate = m_delegate;
        m_window.releasedWhenClosed = NO;
        [m_window center];
    }

    int exec(const QUrl &url)
    {
        [m_view loadRequest:[NSURLRequest requestWithURL:url.toNSURL()]];
        [m_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        return m_code;
    }

    void cookies(std::function<void(const QList<QNetworkCookie> &)> done) override
    {
        [m_view.configuration.websiteDataStore.httpCookieStore getAllCookies:^(NSArray<NSHTTPCookie *> *all) {
            QList<QNetworkCookie> out;
            for (NSHTTPCookie *c in all) {
                QNetworkCookie cookie(QString::fromNSString(c.name).toUtf8(), QString::fromNSString(c.value).toUtf8());
                cookie.setDomain(QString::fromNSString(c.domain));
                cookie.setPath(QString::fromNSString(c.path));
                if (c.expiresDate)
                    cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(qint64(c.expiresDate.timeIntervalSince1970), QTimeZone::UTC));
                cookie.setSecure(c.secure);
                cookie.setHttpOnly(c.HTTPOnly);
                out.append(cookie);
            }
            done(out);
        }];
    }

    void runJavaScript(const QString &script, std::function<void(const QString &)> done) override
    {
        [m_view evaluateJavaScript:script.toNSString()
                 completionHandler:^(id result, NSError *) {
            done(result ? QString::fromNSString([result description]) : QString());
        }];
    }

    void after(int ms, std::function<void()> callback) override
    {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(ms) * int64_t(NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            callback();
        });
    }

    void quit(int code) override
    {
        m_code = code;
        [NSApp stop:nil];
        // stop: takes effect once the run loop handles an event
        [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                                           timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0]
                 atStart:YES];
    }

private:
    WKWebView *m_view;
    NSWindow *m_window;
    QuinceLoginDelegate *m_delegate;
    int m_code = 1;
};

int run(const Options &options, const std::function<void(Window &)> &setup)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        // a window in front and in the Dock, though not an app bundle of its own
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MacBrowser browser(options);
        setup(browser);
        return browser.exec(options.url);
    }
}

} // namespace browser

@implementation QuinceLoginDelegate

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    if (self.owner->pageLoaded)
        self.owner->pageLoaded(QUrl::fromNSURL(webView.URL));
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (self.owner->closed)
        self.owner->closed();
    return NO;
}

@end
