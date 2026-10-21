#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <unicode/utrans.h>

#include <memory>

namespace MeCab {
class Tagger;
}

// Latin letters for Japanese, Korean and Chinese lyrics, and Simplified <->
// Traditional Chinese. Japanese goes through MeCab and IPADIC (the
// pronunciation of each word, then Hepburn), Korean through Revised
// Romanization with its sound changes, Chinese through ICU's pinyin.
// Not thread-safe; Romanizer runs one on its own thread.
class RomanizerEngine
{
public:
    RomanizerEngine();
    ~RomanizerEngine();

    // the language of a song's lines: "ja" (any kana), "ko" (Hangul), "zh"
    // (Han characters only) or "" (nothing to romanize)
    static QString detect(const QStringList &lines);

    // one romanized line per line, "" where the line has nothing to romanize
    QStringList romanize(const QStringList &lines, const QString &language);
    // "hant": to Traditional, "hans": to Simplified
    QStringList convertChinese(const QStringList &lines, const QString &to);
    // drops the dictionary (about 50 MB mapped) until the next Japanese song
    void unload();
    bool loaded() const { return m_tagger != nullptr; }
    // why Japanese is not romanized ("" when it is)
    QString error() const { return m_error; }

    static QString kanaToRomaji(QStringView kana);
    static QString hangulToLatin(QStringView text);

private:
    QString japanese(const QString &line);
    QString transliterate(const char16_t *id, UTransliterator *&cache, const QString &text);
    bool ensureTagger();

    std::unique_ptr<MeCab::Tagger> m_tagger;
    UTransliterator *m_pinyin = nullptr;
    UTransliterator *m_toTraditional = nullptr;
    UTransliterator *m_toSimplified = nullptr;
    QString m_error;
};

class RomanizerWorker : public QObject
{
    Q_OBJECT

public:
    RomanizerWorker();

    // converts and romanizes lines; answers with done()
    void process(int generation, const QStringList &lines, bool romanize, const QString &chinese);

signals:
    void done(int generation, const QStringList &texts, const QStringList &romanized);

private:
    RomanizerEngine m_engine;
    QTimer m_idle;
};

// The worker on its own thread, for Lyrics.
class Romanizer : public QObject
{
    Q_OBJECT

public:
    explicit Romanizer(QObject *parent = nullptr);
    ~Romanizer() override;

    // texts: the lines after the Chinese conversion ("" off, "hant", "hans")
    void process(int generation, const QStringList &lines, bool romanize, const QString &chinese);

signals:
    void done(int generation, const QStringList &texts, const QStringList &romanized);

private:
    QThread m_thread;
    RomanizerWorker *m_worker = nullptr;
};
