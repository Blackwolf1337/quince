#include "DesktopNotify.h"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <QDir>
#include <QStandardPaths>

void desktopNotify(const QString &title, const QString &body, const QImage &cover)
{
    // the notification center throws without an app bundle (a bare binary)
    if (![[NSBundle mainBundle] bundleIdentifier])
        return;
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    static bool asked = false;
    if (!asked) {
        asked = true;
        [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                              completionHandler:^(BOOL, NSError *) {}];
    }
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = title.toNSString();
    content.body = body.toNSString();
    if (!cover.isNull()) {
        // attachments are files; the notification center moves this one away
        const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/quince-cover.png");
        if (cover.save(path)) {
            NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
            UNNotificationAttachment *art = [UNNotificationAttachment attachmentWithIdentifier:@"cover" URL:url options:nil error:nil];
            if (art)
                content.attachments = @[ art ];
        }
    }
    // one identifier: a new track replaces the bubble of the last
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:@"quince-track" content:content trigger:nil];
    [center addNotificationRequest:request withCompletionHandler:nil];
}
