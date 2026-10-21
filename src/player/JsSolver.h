#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

#include <functional>

class InnerTube;
class JsWorker;

// YouTube's web player hides the stream URLs behind two puzzles: the "s"
// signature and the "n" parameter (without a solved n, googlevideo throttles).
// Both are functions in the player JS that changes about weekly. yt-dlp's own
// solver (third_party/ejs) finds them; it runs here in an embedded QuickJS on
// a worker thread.
//
// Solving a new player takes a few seconds once; the result (the
// "preprocessed player") is kept in the cache folder. Loading it into the
// engine takes about 0.25 s and is done ahead of a solve (warm), and the
// engine is freed again when it has not been used for a minute (~60 MiB).
class JsSolver : public QObject
{
    Q_OBJECT

public:
    // n and sig map each challenge to its answer; error is empty on success
    using Callback = std::function<void(const QHash<QString, QString> &n, const QHash<QString, QString> &sig,
                                        const QString &error)>;

    explicit JsSolver(InnerTube *innerTube, QObject *parent = nullptr);
    ~JsSolver() override;

    // a solved player is there (it may be one version behind)
    bool ready() const { return !m_playerId.isEmpty(); }
    // the solved player in use; player requests and solves name it
    QString playerId() const { return m_playerId; }
    // the player's signatureTimestamp, sent with player requests so the
    // answers fit the player the challenges are solved with
    int signatureTimestamp() const { return m_sts; }
    // a new player is being looked up or solved
    bool preparing() const { return m_preparing; }

    // Looks up YouTube's current player and solves it in the background when
    // it is new. Without force, at most once a day. Safe to call often.
    void prepare(bool force = false);
    // Loads the solved player into the engine, so a solve right after is fast.
    void warm();
    // solves with playerId, the player whose signatureTimestamp the request carried
    void solve(const QString &playerId, const QStringList &n, const QStringList &sig, QObject *context,
               Callback callback);

signals:
    // prepare ended, with a new solved player or without
    void prepared();

private:
    void loadCached();
    bool useCached(const QString &playerId);
    QString path(const QString &playerId) const;
    void endPrepare();
    void preprocess(const QString &playerId, const QByteArray &playerJs);
    QString cacheDir() const;

    InnerTube *m_innerTube = nullptr;
    QThread m_thread;
    JsWorker *m_worker = nullptr;
    QString m_playerId; // the solved one in use
    int m_sts = 0;
    bool m_preparing = false;
    qint64 m_preparedAt = 0;
};
