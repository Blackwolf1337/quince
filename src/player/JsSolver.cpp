#include "player/JsSolver.h"
#include "ytm/InnerTube.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <quickjs.h>

#ifdef __GLIBC__
#include <malloc.h>
#endif

Q_LOGGING_CATEGORY(lcJs, "quince.js")

namespace {

// the engine is freed after this long without a solve
constexpr int kIdleMs = 60 * 1000;
// a solve or a player that runs longer is stopped
constexpr qint64 kTimeoutMs = 60 * 1000;
// the player id is looked up again after this long
constexpr qint64 kRecheckSecs = 24 * 3600;
// solved players kept; YouTube hands out more than one at a time (A/B tests)
constexpr int kKeepPlayers = 3;

// challenges solved together with a new player, to see that both functions work
const QString kTestN = QStringLiteral("ZdZIqFPQK-Ty8wId");
const QString kTestSig = QStringLiteral(
    "AAAAAAAAAAAAAAAAAAAABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_abcdefghijklmnopqrstuvw");

QByteArray solverScript(const QString &name)
{
    // a newer solver in the data folder wins, so a YouTube change can be
    // fixed by dropping in the two files of a newer yt-dlp-ejs
    const QString local = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/ejs/") + name;
    QFile file(QFile::exists(local) ? local : QStringLiteral(":/ejs/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

// Owns the QuickJS runtime; lives on the solver thread only.
class JsWorker : public QObject
{
public:
    JsWorker()
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(kIdleMs);
        QObject::connect(&m_idle, &QTimer::timeout, this, [this] { unload(); });
    }
    ~JsWorker() override { unload(); }

    // Solves a fresh player; returns the preprocessed player, or empty and error.
    QByteArray preprocess(const QByteArray &playerJs, QString *error)
    {
        Engine engine;
        if (!engine.ok() || !engine.evalSolver(error))
            return {};
        engine.setGlobal("__player", playerJs);
        const QJsonObject request{
            {QStringLiteral("type"), QStringLiteral("player")},
            {QStringLiteral("requests"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("n")}, {QStringLiteral("challenges"), QJsonArray{kTestN}}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("sig")}, {QStringLiteral("challenges"), QJsonArray{kTestSig}}},
            }},
            {QStringLiteral("output_preprocessed"), true},
        };
        engine.setGlobal("__request", QJsonDocument(request).toJson(QJsonDocument::Compact));
        const QByteArray out = engine.eval(
            "var __r = JSON.parse(__request); __r.player = __player; __player = undefined;"
            "JSON.stringify(jsc(__r))", error);
        if (out.isEmpty())
            return {};
        const QJsonObject result = QJsonDocument::fromJson(out).object();
        if (result.value(u"type").toString() != u"result") {
            *error = result.value(u"error").toString();
            return {};
        }
        for (const QJsonValue &response : result.value(u"responses").toArray()) {
            if (response[u"type"].toString() != u"result") {
                *error = response[u"error"].toString();
                return {};
            }
        }
        return result.value(u"preprocessed_player").toString().toUtf8();
    }

    // Loads the preprocessed player in path unless it is the one loaded.
    QString ensure(const QString &path)
    {
        if (m_engine && m_loadedPath == path) {
            m_idle.start();
            return {};
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return file.errorString();
        QElapsedTimer timer;
        timer.start();
        const QString error = load(file.readAll());
        if (error.isEmpty())
            m_loadedPath = path;
        qCDebug(lcJs) << "loaded" << QFileInfo(path).completeBaseName() << "in" << timer.elapsed() << "ms" << error;
        return error;
    }

    QString load(const QByteArray &preprocessed)
    {
        unload();
        m_engine = std::make_unique<Engine>();
        if (!m_engine->ok()) {
            m_engine.reset();
            return QStringLiteral("QuickJS could not start");
        }
        QString error;
        m_engine->setGlobal("__pp", preprocessed);
        // The solver itself runs a preprocessed player as Function("_result",
        // player)(result), which fills in result.n and result.sig. Doing the
        // same once here keeps both functions for every later solve.
        const QByteArray direct = m_engine->eval(
            "var __q = {n: null, sig: null}; Function('_result', __pp)(__q);"
            "typeof __q.n === 'function' && typeof __q.sig === 'function' ? 'yes' : 'no'", &error);
        m_direct = direct == "yes";
        if (m_direct) {
            m_engine->eval("__pp = undefined; 1", &error);
        } else if (!m_engine->evalSolver(&error)) {
            // a newer solver may have changed that; it still solves through jsc()
            m_engine.reset();
            return error;
        }
        m_idle.start();
        return {};
    }

    QString solve(const QStringList &n, const QStringList &sig, QHash<QString, QString> *nOut,
                  QHash<QString, QString> *sigOut)
    {
        if (!m_engine)
            return QStringLiteral("no player loaded");
        m_idle.start();
        QString error;
        const QJsonObject input{
            {QStringLiteral("n"), QJsonArray::fromStringList(n)},
            {QStringLiteral("sig"), QJsonArray::fromStringList(sig)},
        };
        m_engine->setGlobal("__in", QJsonDocument(input).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (m_direct) {
            out = m_engine->eval(
                "(function () { var i = JSON.parse(__in), o = {n: {}, sig: {}};"
                " i.n.forEach(function (c) { o.n[c] = __q.n(c); });"
                " i.sig.forEach(function (c) { o.sig[c] = __q.sig(c); });"
                " return JSON.stringify(o); })()", &error);
        } else {
            out = m_engine->eval(
                "(function () { var i = JSON.parse(__in), q = [], o = {n: {}, sig: {}};"
                " if (i.n.length) q.push({type: 'n', challenges: i.n});"
                " if (i.sig.length) q.push({type: 'sig', challenges: i.sig});"
                " var r = jsc({type: 'preprocessed', preprocessed_player: __pp, requests: q});"
                " if (r.type !== 'result') throw r.error;"
                " r.responses.forEach(function (x, k) { if (x.type !== 'result') throw x.error; o[q[k].type] = x.data; });"
                " return JSON.stringify(o); })()", &error);
        }
        if (out.isEmpty())
            return error;
        const QJsonObject result = QJsonDocument::fromJson(out).object();
        const QJsonObject nMap = result.value(u"n").toObject();
        const QJsonObject sigMap = result.value(u"sig").toObject();
        for (auto it = nMap.begin(); it != nMap.end(); ++it)
            nOut->insert(it.key(), it.value().toString());
        for (auto it = sigMap.begin(); it != sigMap.end(); ++it)
            sigOut->insert(it.key(), it.value().toString());
        return {};
    }

    void unload()
    {
        if (!m_engine)
            return;
        m_engine.reset();
        m_loadedPath.clear();
        m_idle.stop();
        qCDebug(lcJs) << "player unloaded";
#ifdef __GLIBC__
        // the runtime's many small blocks would otherwise stay with the process
        malloc_trim(0);
#endif
    }

private:
    class Engine
    {
    public:
        Engine()
        {
            m_runtime = JS_NewRuntime();
            if (!m_runtime)
                return;
            // meriyah recurses deeply over the 3 MB player; the thread has 16 MiB
            JS_SetMaxStackSize(m_runtime, 12 * 1024 * 1024);
            JS_SetInterruptHandler(m_runtime, &Engine::interrupt, this);
            m_context = JS_NewContext(m_runtime);
        }
        ~Engine()
        {
            if (m_context)
                JS_FreeContext(m_context);
            if (m_runtime)
                JS_FreeRuntime(m_runtime);
        }
        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;

        bool ok() const { return m_context != nullptr; }

        bool evalSolver(QString *error)
        {
            const QByteArray lib = solverScript(QStringLiteral("lib.min.js"));
            const QByteArray core = solverScript(QStringLiteral("core.min.js"));
            if (lib.isEmpty() || core.isEmpty()) {
                *error = QStringLiteral("solver scripts missing");
                return false;
            }
            return !eval(lib + "\nObject.assign(globalThis, lib);\n" + core + "\n1", error).isEmpty();
        }

        void setGlobal(const char *name, const QByteArray &value)
        {
            JSValue global = JS_GetGlobalObject(m_context);
            JS_SetPropertyStr(m_context, global, name, JS_NewStringLen(m_context, value.constData(), value.size()));
            JS_FreeValue(m_context, global);
        }

        // The script's last value as a string; empty and error on an exception.
        QByteArray eval(const QByteArray &script, QString *error)
        {
            m_started.start();
            // JS_Eval wants the terminating zero, which QByteArray has
            JSValue value = JS_Eval(m_context, script.constData(), script.size(), "<quince>", JS_EVAL_TYPE_GLOBAL);
            QByteArray out;
            if (JS_IsException(value)) {
                JSValue exception = JS_GetException(m_context);
                *error = QString::fromUtf8(toUtf8(exception));
                JS_FreeValue(m_context, exception);
                if (error->isEmpty())
                    *error = QStringLiteral("JavaScript error");
            } else {
                out = toUtf8(value);
            }
            JS_FreeValue(m_context, value);
            return out;
        }

    private:
        QByteArray toUtf8(JSValueConst value)
        {
            size_t length = 0;
            const char *text = JS_ToCStringLen(m_context, &length, value);
            if (!text)
                return {};
            QByteArray out(text, qsizetype(length));
            JS_FreeCString(m_context, text);
            return out;
        }

        static int interrupt(JSRuntime *, void *opaque)
        {
            const auto *engine = static_cast<const Engine *>(opaque);
            return engine->m_started.isValid() && engine->m_started.elapsed() > kTimeoutMs;
        }

        JSRuntime *m_runtime = nullptr;
        JSContext *m_context = nullptr;
        QElapsedTimer m_started;
    };

    std::unique_ptr<Engine> m_engine;
    QString m_loadedPath;
    bool m_direct = false;
    QTimer m_idle;
};

JsSolver::JsSolver(InnerTube *innerTube, QObject *parent)
    : QObject(parent)
    , m_innerTube(innerTube)
    , m_worker(new JsWorker)
{
    m_thread.setObjectName(QStringLiteral("quince-js"));
    m_thread.setStackSize(16 * 1024 * 1024);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start(QThread::LowPriority);
    loadCached();
}

JsSolver::~JsSolver()
{
    m_thread.quit();
    m_thread.wait();
}

QString JsSolver::cacheDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/player");
}

// the newest solved player from an earlier run
void JsSolver::loadCached()
{
    const QFileInfoList files = QDir(cacheDir()).entryInfoList({QStringLiteral("*.sts")}, QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        if (useCached(info.completeBaseName())) {
            qCDebug(lcJs) << "cached player" << m_playerId << "sts" << m_sts;
            return;
        }
    }
}

bool JsSolver::useCached(const QString &playerId)
{
    QFile sts(cacheDir() + u'/' + playerId + u".sts");
    if (!sts.open(QIODevice::ReadOnly) || !QFile::exists(path(playerId)))
        return false;
    const int value = sts.readAll().trimmed().toInt();
    if (value <= 0)
        return false;
    m_playerId = playerId;
    m_sts = value;
    return true;
}

QString JsSolver::path(const QString &playerId) const
{
    return cacheDir() + u'/' + playerId + u".js";
}

void JsSolver::prepare(bool force)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_preparing)
        return;
    if (!force) {
        // a player solved within a day still works
        if (m_preparedAt && now - m_preparedAt < kRecheckSecs)
            return;
        if (!m_preparedAt && ready()
            && QFileInfo(path(m_playerId)).lastModified().secsTo(QDateTime::currentDateTime()) < kRecheckSecs) {
            m_preparedAt = now;
            return;
        }
    }
    m_preparing = true;
    m_preparedAt = now;

    // the iframe API names the current player, as yt-dlp finds it too
    m_innerTube->get(QUrl(QStringLiteral("https://www.youtube.com/iframe_api")), this,
                     [this](const QByteArray &data, const QString &error) {
        static const QRegularExpression version(QStringLiteral("player\\\\?/([0-9a-fA-F]{8})\\\\?/"));
        const QString playerId = version.match(QString::fromUtf8(data)).captured(1);
        if (playerId.isEmpty()) {
            qCWarning(lcJs) << "no player id" << error;
            m_preparedAt = 0;
            endPrepare();
            return;
        }
        if (playerId == m_playerId || useCached(playerId)) {
            endPrepare();
            return;
        }
        const QUrl url(QStringLiteral("https://www.youtube.com/s/player/%1/player_ias.vflset/en_US/base.js").arg(playerId));
        m_innerTube->get(url, this, [this, playerId](const QByteArray &js, const QString &error) {
            if (js.isEmpty()) {
                qCWarning(lcJs) << "player" << playerId << "not loaded:" << error;
                m_preparedAt = 0;
                endPrepare();
                return;
            }
            preprocess(playerId, js);
        });
    });
}

void JsSolver::preprocess(const QString &playerId, const QByteArray &playerJs)
{
    static const QRegularExpression stsPattern(QStringLiteral("(?:signatureTimestamp|sts)\\s*:\\s*([0-9]{5})"));
    const int sts = stsPattern.match(QString::fromUtf8(playerJs)).captured(1).toInt();
    if (sts <= 0) {
        qCWarning(lcJs) << "player" << playerId << "has no signatureTimestamp";
        endPrepare();
        return;
    }

    QPointer<JsSolver> self(this);
    const QString dir = cacheDir();
    QMetaObject::invokeMethod(m_worker, [self, worker = m_worker, playerId, playerJs, sts, dir] {
        QElapsedTimer timer;
        timer.start();
        QString error;
        const QByteArray preprocessed = worker->preprocess(playerJs, &error);
        qCDebug(lcJs) << "player" << playerId << "solved in" << timer.elapsed() << "ms" << error;
        if (!preprocessed.isEmpty()) {
            QDir().mkpath(dir);
            QSaveFile js(dir + u'/' + playerId + u".js");
            QSaveFile stsFile(dir + u'/' + playerId + u".sts");
            if (js.open(QIODevice::WriteOnly) && js.write(preprocessed) == preprocessed.size() && js.commit()
                && stsFile.open(QIODevice::WriteOnly) && stsFile.write(QByteArray::number(sts)) > 0 && stsFile.commit()) {
                // the oldest go beyond kKeepPlayers
                const QFileInfoList kept = QDir(dir).entryInfoList({QStringLiteral("*.sts")}, QDir::Files, QDir::Time);
                for (qsizetype i = kKeepPlayers; i < kept.size(); ++i) {
                    QFile::remove(kept.at(i).filePath());
                    QFile::remove(dir + u'/' + kept.at(i).completeBaseName() + u".js");
                }
            } else {
                error = QStringLiteral("cache folder not writable");
            }
        }
        QMetaObject::invokeMethod(self, [self, playerId, sts, error] {
            if (!self)
                return;
            if (error.isEmpty()) {
                self->m_playerId = playerId;
                self->m_sts = sts;
            } else {
                qCWarning(lcJs) << "player" << playerId << "could not be solved:" << error;
            }
            self->endPrepare();
        });
    });
}

void JsSolver::endPrepare()
{
    m_preparing = false;
    emit prepared();
}

void JsSolver::warm()
{
    if (!ready())
        return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, file = path(m_playerId)] {
        worker->ensure(file);
    });
}

void JsSolver::solve(const QString &playerId, const QStringList &n, const QStringList &sig, QObject *context,
                     Callback callback)
{
    QPointer<QObject> guard(context);
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, file = path(playerId), n, sig, guard, callback] {
        QHash<QString, QString> nOut;
        QHash<QString, QString> sigOut;
        QString error = worker->ensure(file);
        if (error.isEmpty())
            error = worker->solve(n, sig, &nOut, &sigOut);
        QMetaObject::invokeMethod(this, [guard, callback, nOut, sigOut, error] {
            if (guard)
                callback(nOut, sigOut, error);
        });
    });
}
