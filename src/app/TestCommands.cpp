#include "app/TestCommands.h"
#include "app/AppController.h"
#include "player/MpvPlayer.h"
#include "player/Playback.h"
#include "player/Queue.h"

#include <QDebug>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

bool runTestCommand(AppController *app, const QString &line, QObject *context)
{
    const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
    const QString command = parts.value(0);

    if (command == u"state") {
        const Playback *playback = app->playback();
        const MpvPlayer *player = app->player();
        qInfo().noquote() << "state:" << playback->track().value(QStringLiteral("title")).toString()
                          << "| queue" << playback->queue()->current() << "/" << playback->queue()->count()
                          << "| pos" << player->position() << "/" << player->duration()
                          << "| paused" << player->paused() << "idle" << player->idle()
                          << "| loading" << playback->loading() << "| error" << playback->error()
                          << "|" << playback->streamInfo();
    } else if (command == u"open") {
        app->openBrowse(parts.value(1));
    } else if (command == u"call") {
        // call <App property> <method> [string argument], e.g. call account signOut;
        // "." for App itself
        QObject *target = parts.value(1) == u"." ? static_cast<QObject *>(app)
                                                  : app->property(parts.value(1).toLatin1()).value<QObject *>();
        const QByteArray method = parts.value(2).toLatin1();
        const QString argument = parts.mid(3).join(u' ');
        bool number = false;
        const int value = argument.toInt(&number);
        // key=value,key=value passes a map (e.g. an item)
        QVariantMap map;
        if (argument.contains(u'=')) {
            for (const QString &pair : argument.split(u','))
                map.insert(pair.section(u'=', 0, 0), pair.section(u'=', 1));
        }
        const bool ok = !target ? false
            : parts.size() <= 3 ? QMetaObject::invokeMethod(target, method.constData())
            : !map.isEmpty() ? QMetaObject::invokeMethod(target, method.constData(), Q_ARG(QVariantMap, map))
            : (number && QMetaObject::invokeMethod(target, method.constData(), Q_ARG(int, value)))
                || (number && QMetaObject::invokeMethod(target, method.constData(), Q_ARG(double, value)))
                || QMetaObject::invokeMethod(target, method.constData(), Q_ARG(QString, argument));
        qInfo() << "test: call" << parts.mid(1) << (ok ? "ok" : "failed");
    } else if (command == u"set") {
        // set <App property> <property> <value>, e.g. set settings lyricsEffect scale
        QObject *target = app->property(parts.value(1).toLatin1()).value<QObject *>();
        const bool ok = target && target->setProperty(parts.value(2).toLatin1(), parts.mid(3).join(u' '));
        qInfo() << "test: set" << parts.mid(1) << (ok ? "ok" : "failed");
    } else if (command == u"sh") {
        // a shell command in the app's environment (e.g. playerctl on its bus)
        auto *process = new QProcess(context);
        process->setProcessChannelMode(QProcess::MergedChannels);
        const QString shellLine = line.mid(3);
        QObject::connect(process, &QProcess::finished, context, [process, shellLine] {
            qInfo().noquote() << "sh:" << shellLine << "->" << QString::fromUtf8(process->readAll()).trimmed();
            process->deleteLater();
        });
        process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), shellLine});
    } else {
        return false;
    }
    return true;
}
