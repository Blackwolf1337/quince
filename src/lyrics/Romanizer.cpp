#include "lyrics/Romanizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <mecab.h>

Q_LOGGING_CATEGORY(lcRomanizer, "quince.romanizer")

namespace {

// the dictionary is dropped after this long without a Japanese song
constexpr int kIdleMs = 60 * 1000;

bool isKana(char16_t c)
{
    return (c >= 0x3041 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF)
        || (c >= 0xFF66 && c <= 0xFF9D);
}

bool isHan(char16_t c)
{
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF);
}

bool isHangul(char16_t c)
{
    return (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F);
}

bool contains(QStringView text, bool (*test)(char16_t))
{
    for (QChar c : text) {
        if (test(c.unicode()))
            return true;
    }
    return false;
}

bool isJapanese(char16_t c)
{
    return isKana(c) || isHan(c);
}

// hiragana to katakana, so one table serves both
char16_t katakana(char16_t c)
{
    return c >= 0x3041 && c <= 0x3096 ? char16_t(c + 0x60) : c;
}

// modified Hepburn
const QHash<QString, QString> &kanaTable()
{
    static const QHash<QString, QString> table = [] {
        static const struct {
            const char16_t *kana;
            const char *latin;
        } entries[] = {
            {u"ア", "a"}, {u"イ", "i"}, {u"ウ", "u"}, {u"エ", "e"}, {u"オ", "o"},
            {u"カ", "ka"}, {u"キ", "ki"}, {u"ク", "ku"}, {u"ケ", "ke"}, {u"コ", "ko"},
            {u"ガ", "ga"}, {u"ギ", "gi"}, {u"グ", "gu"}, {u"ゲ", "ge"}, {u"ゴ", "go"},
            {u"サ", "sa"}, {u"シ", "shi"}, {u"ス", "su"}, {u"セ", "se"}, {u"ソ", "so"},
            {u"ザ", "za"}, {u"ジ", "ji"}, {u"ズ", "zu"}, {u"ゼ", "ze"}, {u"ゾ", "zo"},
            {u"タ", "ta"}, {u"チ", "chi"}, {u"ツ", "tsu"}, {u"テ", "te"}, {u"ト", "to"},
            {u"ダ", "da"}, {u"ヂ", "ji"}, {u"ヅ", "zu"}, {u"デ", "de"}, {u"ド", "do"},
            {u"ナ", "na"}, {u"ニ", "ni"}, {u"ヌ", "nu"}, {u"ネ", "ne"}, {u"ノ", "no"},
            {u"ハ", "ha"}, {u"ヒ", "hi"}, {u"フ", "fu"}, {u"ヘ", "he"}, {u"ホ", "ho"},
            {u"バ", "ba"}, {u"ビ", "bi"}, {u"ブ", "bu"}, {u"ベ", "be"}, {u"ボ", "bo"},
            {u"パ", "pa"}, {u"ピ", "pi"}, {u"プ", "pu"}, {u"ペ", "pe"}, {u"ポ", "po"},
            {u"マ", "ma"}, {u"ミ", "mi"}, {u"ム", "mu"}, {u"メ", "me"}, {u"モ", "mo"},
            {u"ヤ", "ya"}, {u"ユ", "yu"}, {u"ヨ", "yo"},
            {u"ラ", "ra"}, {u"リ", "ri"}, {u"ル", "ru"}, {u"レ", "re"}, {u"ロ", "ro"},
            {u"ワ", "wa"}, {u"ヰ", "i"}, {u"ヱ", "e"}, {u"ヲ", "o"}, {u"ン", "n"}, {u"ヴ", "vu"},
            {u"ァ", "a"}, {u"ィ", "i"}, {u"ゥ", "u"}, {u"ェ", "e"}, {u"ォ", "o"},
            {u"ャ", "ya"}, {u"ュ", "yu"}, {u"ョ", "yo"}, {u"ヮ", "wa"}, {u"ヵ", "ka"}, {u"ヶ", "ke"},
            {u"キャ", "kya"}, {u"キュ", "kyu"}, {u"キョ", "kyo"}, {u"ギャ", "gya"}, {u"ギュ", "gyu"}, {u"ギョ", "gyo"},
            {u"シャ", "sha"}, {u"シュ", "shu"}, {u"ショ", "sho"}, {u"シェ", "she"},
            {u"ジャ", "ja"}, {u"ジュ", "ju"}, {u"ジョ", "jo"}, {u"ジェ", "je"},
            {u"チャ", "cha"}, {u"チュ", "chu"}, {u"チョ", "cho"}, {u"チェ", "che"},
            {u"ヂャ", "ja"}, {u"ヂュ", "ju"}, {u"ヂョ", "jo"},
            {u"ニャ", "nya"}, {u"ニュ", "nyu"}, {u"ニョ", "nyo"}, {u"ヒャ", "hya"}, {u"ヒュ", "hyu"}, {u"ヒョ", "hyo"},
            {u"ビャ", "bya"}, {u"ビュ", "byu"}, {u"ビョ", "byo"}, {u"ピャ", "pya"}, {u"ピュ", "pyu"}, {u"ピョ", "pyo"},
            {u"ミャ", "mya"}, {u"ミュ", "myu"}, {u"ミョ", "myo"}, {u"リャ", "rya"}, {u"リュ", "ryu"}, {u"リョ", "ryo"},
            {u"ファ", "fa"}, {u"フィ", "fi"}, {u"フェ", "fe"}, {u"フォ", "fo"}, {u"フュ", "fyu"},
            {u"ティ", "ti"}, {u"ディ", "di"}, {u"トゥ", "tu"}, {u"ドゥ", "du"}, {u"テュ", "tyu"}, {u"デュ", "dyu"},
            {u"ウィ", "wi"}, {u"ウェ", "we"}, {u"ウォ", "wo"}, {u"イェ", "ye"},
            {u"ヴァ", "va"}, {u"ヴィ", "vi"}, {u"ヴェ", "ve"}, {u"ヴォ", "vo"},
            {u"ツァ", "tsa"}, {u"ツィ", "tsi"}, {u"ツェ", "tse"}, {u"ツォ", "tso"},
            {u"、", ","}, {u"。", "."}, {u"「", "\""}, {u"」", "\""}, {u"『", "\""}, {u"』", "\""}, {u"（", "("}, {u"）", ")"}, {u"【", "["}, {u"】", "]"}, {u"・", " "},
            {u"〜", "~"},
        };
        QHash<QString, QString> t;
        for (const auto &e : entries)
            t.insert(QString::fromUtf16(e.kana), QString::fromLatin1(e.latin));
        return t;
    }();
    return table;
}

QChar macron(QChar vowel)
{
    switch (vowel.unicode()) {
    case u'a': return QChar(u'ā');
    case u'i': return QChar(u'ī');
    case u'u': return QChar(u'ū');
    case u'e': return QChar(u'ē');
    case u'o': return QChar(u'ō');
    default: return vowel;
    }
}

bool startsWithVowelOrY(const QString &latin)
{
    return !latin.isEmpty() && QStringLiteral("aiueoy").contains(latin.front());
}

// Revised Romanization of Korean: jamo indexes of a syllable block
struct Syllable
{
    int initial = 0; // 0..18, 11 is the silent ㅇ
    int medial = 0; // 0..20
    int final = 0; // 0..27, 0 none
};

enum Initial { G, GG, N, D, DD, R, M, B, BB, S, SS, Silent, J, JJ, CH, K, T, P, H };
enum Final { None, FG, FGG, FGS, FN, FNJ, FNH, FD, FL, FLG, FLM, FLB, FLS, FLT, FLP, FLH, FM, FB, FBS, FS, FSS, FNG,
             FJ, FCH, FK, FT, FP, FH };

const char *const kInitials[] = {"g", "kk", "n", "d", "tt", "r", "m", "b", "pp", "s", "ss", "", "j", "jj", "ch", "k",
                                 "t", "p", "h"};
const char *const kMedials[] = {"a", "ae", "ya", "yae", "eo", "e", "yeo", "ye", "o", "wa", "wae", "oe", "yo", "u",
                                "wo", "we", "wi", "yu", "eu", "ui", "i"};
const char *const kFinals[] = {"", "k", "k", "k", "n", "n", "n", "t", "l", "k", "m", "l", "l", "l", "p", "l", "m",
                               "p", "p", "t", "t", "ng", "t", "t", "k", "t", "p", "t"};

// before a vowel a final moves over: what stays, and the initial it becomes
struct Liaison
{
    int stays;
    int moves; // -1: nothing moves (ㅎ falls silent)
};
const Liaison kLiaison[] = {
    {None, -1}, {None, G}, {None, GG}, {FG, S}, {None, N}, {FN, J}, {None, N}, {None, D}, {None, R},
    {FL, G}, {FL, M}, {FL, B}, {FL, S}, {FL, T}, {FL, P}, {None, R}, {None, M}, {None, B}, {FB, S},
    {None, S}, {None, SS}, {FNG, Silent}, {None, J}, {None, CH}, {None, K}, {None, T}, {None, P}, {None, -1},
};

// the sound class of a final before another consonant
enum Sound { NoSound, SoundK, SoundT, SoundP, SoundN, SoundL, SoundM, SoundNg };
Sound soundOf(int final)
{
    switch (final) {
    case FG: case FGG: case FGS: case FLG: case FK: return SoundK;
    case FD: case FS: case FSS: case FJ: case FCH: case FT: case FH: return SoundT;
    case FB: case FBS: case FLP: case FP: return SoundP;
    case FN: case FNJ: case FNH: return SoundN;
    case FL: case FLB: case FLS: case FLT: case FLH: return SoundL;
    case FM: case FLM: return SoundM;
    case FNG: return SoundNg;
    default: return NoSound;
    }
}

// a final that stands for the sound (for the output table)
int finalFor(Sound sound)
{
    switch (sound) {
    case SoundK: return FG;
    case SoundT: return FD;
    case SoundP: return FB;
    case SoundN: return FN;
    case SoundL: return FL;
    case SoundM: return FM;
    case SoundNg: return FNG;
    default: return None;
    }
}

// the sound changes between two syllables of one word
void assimilate(Syllable &a, Syllable &b)
{
    if (a.final == None)
        return;
    if (b.initial == Silent) {
        // ㄷ and ㅌ before 이 are said ㅈ and ㅊ (같이: gachi)
        constexpr int kI = 20;
        if (b.medial == kI && (a.final == FD || a.final == FT || a.final == FLT)) {
            b.initial = a.final == FD ? J : CH;
            a.final = a.final == FLT ? FL : None;
            return;
        }
        const Liaison l = kLiaison[a.final];
        if (l.moves == Silent)
            return; // ㅇ stays a final
        a.final = l.stays;
        if (l.moves >= 0)
            b.initial = l.moves;
        return;
    }
    // ㅎ with ㄱ, ㄷ, ㅈ, ㅂ makes them aspirated
    const bool hFinal = a.final == FH || a.final == FNH || a.final == FLH;
    if (hFinal && (b.initial == G || b.initial == D || b.initial == J)) {
        b.initial = b.initial == G ? K : b.initial == D ? T : CH;
        a.final = a.final == FNH ? FN : a.final == FLH ? FL : None;
        return;
    }
    if (b.initial == H) {
        switch (soundOf(a.final)) {
        case SoundK: b.initial = K; a.final = None; return;
        case SoundT: b.initial = a.final == FJ ? CH : T; a.final = None; return;
        case SoundP: b.initial = P; a.final = None; return;
        default: break;
        }
    }
    const Sound sound = soundOf(a.final);
    // ㄹ after ㄴ or ㄹ is l, ㄴ next to ㄹ turns into l
    if (b.initial == R) {
        if (sound == SoundN || sound == SoundL) {
            a.final = FL;
            return;
        }
        b.initial = N;
        if (sound == SoundK)
            a.final = FNG;
        else if (sound == SoundP)
            a.final = FM;
        else if (sound == SoundT)
            a.final = FN;
        return;
    }
    if (b.initial == N && sound == SoundL) {
        a.final = FL;
        b.initial = R; // written as l after the final l
        return;
    }
    // nasalization before ㄴ and ㅁ
    if (b.initial == N || b.initial == M) {
        if (sound == SoundK)
            a.final = FNG;
        else if (sound == SoundT)
            a.final = FN;
        else if (sound == SoundP)
            a.final = FM;
        return;
    }
    a.final = finalFor(sound);
}

QString romanizeWord(QList<Syllable> &word)
{
    for (qsizetype i = 0; i + 1 < word.size(); ++i)
        assimilate(word[i], word[i + 1]);
    QString out;
    for (qsizetype i = 0; i < word.size(); ++i) {
        const Syllable &s = word.at(i);
        // ㄹ after the final l is l too (ll)
        if (s.initial == R && i > 0 && word.at(i - 1).final == FL)
            out += u'l';
        else
            out += QLatin1StringView(kInitials[s.initial]);
        out += QLatin1StringView(kMedials[s.medial]);
        out += QLatin1StringView(kFinals[s.final]);
    }
    return out;
}

QString dictionaryDir()
{
    QStringList candidates;
    if (const QString env = qEnvironmentVariable("QUINCE_MECAB_DIC"); !env.isEmpty())
        candidates << env;
    const QString app = QCoreApplication::applicationDirPath();
    candidates << app + QStringLiteral("/mecab/ipadic") << app + QStringLiteral("/../share/quince/mecab/ipadic")
               << app + QStringLiteral("/../Resources/mecab/ipadic");
    candidates << QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("quince/mecab/ipadic"),
                                            QStandardPaths::LocateDirectory);
    for (const QString &dir : std::as_const(candidates)) {
        if (QFileInfo::exists(dir + QStringLiteral("/sys.dic")))
            return QDir(dir).absolutePath();
    }
    return {};
}

} // namespace

RomanizerEngine::RomanizerEngine() = default;

RomanizerEngine::~RomanizerEngine()
{
    for (UTransliterator *t : {m_pinyin, m_toTraditional, m_toSimplified}) {
        if (t)
            utrans_close(t);
    }
}

QString RomanizerEngine::detect(const QStringList &lines)
{
    bool hangul = false;
    bool han = false;
    for (const QString &line : lines) {
        if (contains(line, isKana))
            return QStringLiteral("ja");
        hangul = hangul || contains(line, isHangul);
        han = han || contains(line, isHan);
    }
    return hangul ? QStringLiteral("ko") : han ? QStringLiteral("zh") : QString();
}

QString RomanizerEngine::kanaToRomaji(QStringView kana)
{
    const QHash<QString, QString> &table = kanaTable();
    QString out;
    bool sokuon = false; // ッ: the next consonant doubles
    bool afterN = false; // the last letter came from ン
    for (qsizetype i = 0; i < kana.size();) {
        const char16_t c = katakana(kana.at(i).unicode());
        if (c == u'ッ') {
            sokuon = true;
            ++i;
            continue;
        }
        if (c == u'ー') {
            if (!out.isEmpty())
                out.back() = macron(out.back());
            ++i;
            continue;
        }
        QString latin;
        if (i + 1 < kana.size()) {
            const QString pair = QChar(c) + QString(QChar(katakana(kana.at(i + 1).unicode())));
            if (const auto it = table.constFind(pair); it != table.cend()) {
                latin = *it;
                i += 2;
            }
        }
        if (latin.isEmpty()) {
            const auto it = table.constFind(QString(QChar(c)));
            latin = it != table.cend() ? *it : QString(kana.at(i));
            ++i;
        }
        if (sokuon && !latin.isEmpty() && latin.front().isLetter() && !startsWithVowelOrY(latin))
            latin.prepend(latin.startsWith(u"ch") ? u't' : latin.front());
        sokuon = false;
        // ン before a vowel or y: n'
        if (afterN && startsWithVowelOrY(latin))
            out += u'\'';
        afterN = c == u'ン';
        out += latin;
    }
    return out;
}

QString RomanizerEngine::hangulToLatin(QStringView text)
{
    QString out;
    QList<Syllable> word;
    for (QChar ch : text) {
        const char16_t c = ch.unicode();
        if (c >= 0xAC00 && c <= 0xD7A3) {
            const int s = c - 0xAC00;
            word.append({s / 588, (s % 588) / 28, s % 28});
            continue;
        }
        if (!word.isEmpty()) {
            out += romanizeWord(word);
            word.clear();
        }
        out += ch;
    }
    if (!word.isEmpty())
        out += romanizeWord(word);
    return out;
}

bool RomanizerEngine::ensureTagger()
{
    if (m_tagger)
        return true;
    if (!m_error.isEmpty())
        return false;
    const QString dir = dictionaryDir();
    if (dir.isEmpty()) {
        m_error = QStringLiteral("no MeCab dictionary (mecab/ipadic) found");
        qCWarning(lcRomanizer) << m_error;
        return false;
    }
    // MeCab insists on a mecabrc; the build puts an empty one next to the dictionary
    QByteArray args[] = {"mecab", "-d", QFile::encodeName(dir), "-r",
                         QFile::encodeName(dir + QStringLiteral("/mecabrc"))};
    char *argv[std::size(args)];
    for (size_t i = 0; i < std::size(args); ++i)
        argv[i] = args[i].data();
    m_tagger.reset(MeCab::createTagger(int(std::size(args)), argv));
    if (!m_tagger) {
        m_error = QString::fromUtf8(MeCab::getTaggerError());
        qCWarning(lcRomanizer) << "MeCab:" << m_error;
        return false;
    }
    qCDebug(lcRomanizer) << "dictionary loaded from" << dir;
    return true;
}

void RomanizerEngine::unload()
{
    if (m_tagger) {
        m_tagger.reset();
        qCDebug(lcRomanizer) << "dictionary unloaded";
    }
}

// the words of a line in Hepburn, spaced; particles come out as they are
// spoken (は "wa", へ "e", を "o") because MeCab gives the pronunciation
QString RomanizerEngine::japanese(const QString &line)
{
    if (!ensureTagger())
        return {};
    const QByteArray utf8 = line.toUtf8();
    QString out;
    // the kana of one word with its endings, so a ッ at the end of a token
    // still doubles the next one (待っ + て: matte)
    QString word;
    // after an opening bracket the next word follows without a space
    bool opened = false;
    const auto flush = [&] {
        if (word.isEmpty())
            return;
        if (!out.isEmpty() && !opened)
            out += u' ';
        opened = false;
        out += kanaToRomaji(word);
        word.clear();
    };
    for (const MeCab::Node *node = m_tagger->parseToNode(utf8.constData(), utf8.size()); node; node = node->next) {
        if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE)
            continue;
        const QString surface = QString::fromUtf8(node->surface, node->length);
        // IPADIC: part of speech, its subtypes (3), conjugation (2), base form, reading, pronunciation
        const QStringList feature = QString::fromUtf8(node->feature).split(u',');
        QString spoken = surface;
        if (node->stat != MECAB_UNK_NODE) {
            if (feature.size() > 8 && feature.at(8) != u"*")
                spoken = feature.at(8);
            else if (feature.size() > 7 && feature.at(7) != u"*")
                spoken = feature.at(7);
        }
        const QString pos = feature.value(0);
        const QString sub = feature.value(1);
        // endings stay with their word: auxiliary verbs, suffixes, dependent verbs, punctuation
        const bool attach = pos == u"記号" || pos == u"助動詞" || sub == u"接尾" || (pos == u"動詞" && sub == u"非自立")
                         || (pos == u"助詞" && sub == u"接続助詞");
        const bool spaced = node->rlength > node->length;
        if (spaced || !attach)
            flush();
        if (pos == u"記号" && QStringView(u"「『（(【〈《").contains(surface)) {
            flush();
            if (!out.isEmpty())
                out += u' ';
            out += kanaToRomaji(surface);
            opened = true;
            continue;
        }
        word += spoken;
    }
    flush();
    // full-width letters and digits to plain ones
    return out.normalized(QString::NormalizationForm_KC).simplified();
}

QString RomanizerEngine::transliterate(const char16_t *id, UTransliterator *&cache, const QString &text)
{
    UErrorCode status = U_ZERO_ERROR;
    if (!cache) {
        cache = utrans_openU(reinterpret_cast<const UChar *>(id), -1, UTRANS_FORWARD, nullptr, 0, nullptr, &status);
        if (U_FAILURE(status)) {
            qCWarning(lcRomanizer) << "ICU has no" << QString::fromUtf16(id) << u_errorName(status);
            cache = nullptr;
            return text;
        }
    }
    QString buffer = text;
    for (int32_t capacity = int32_t(text.size()) * 4 + 16;; capacity *= 2) {
        buffer = text;
        int32_t length = int32_t(text.size());
        int32_t limit = length;
        buffer.resize(capacity);
        status = U_ZERO_ERROR;
        utrans_transUChars(cache, reinterpret_cast<UChar *>(buffer.data()), &length, capacity, 0, &limit, &status);
        if (status == U_BUFFER_OVERFLOW_ERROR)
            continue;
        if (U_FAILURE(status))
            return text;
        buffer.truncate(length);
        return buffer;
    }
}

QStringList RomanizerEngine::romanize(const QStringList &lines, const QString &language)
{
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        QString latin;
        if (language == u"ja" && contains(line, isJapanese))
            latin = japanese(line);
        else if (language == u"ko" && contains(line, isHangul))
            latin = hangulToLatin(line);
        else if (language == u"zh" && contains(line, isHan))
            latin = transliterate(u"Han-Latin", m_pinyin, line).simplified();
        out.append(latin == line ? QString() : latin);
    }
    return out;
}

QStringList RomanizerEngine::convertChinese(const QStringList &lines, const QString &to)
{
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        if (to == u"hant")
            out.append(transliterate(u"Hans-Hant", m_toTraditional, line));
        else if (to == u"hans")
            out.append(transliterate(u"Hant-Hans", m_toSimplified, line));
        else
            out.append(line);
    }
    return out;
}

RomanizerWorker::RomanizerWorker()
{
    // a child, so it moves to the worker thread along with it
    m_idle.setParent(this);
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] { m_engine.unload(); });
}

void RomanizerWorker::process(int generation, const QStringList &lines, bool romanize, const QString &chinese)
{
    const QString language = RomanizerEngine::detect(lines);
    // only Chinese lyrics are converted, never the kanji of Japanese ones
    const QStringList texts = language == u"zh" && !chinese.isEmpty() ? m_engine.convertChinese(lines, chinese) : lines;
    const QStringList romanized = romanize && !language.isEmpty() ? m_engine.romanize(texts, language) : QStringList();
    if (m_engine.loaded())
        m_idle.start();
    emit done(generation, texts, romanized);
}

Romanizer::Romanizer(QObject *parent)
    : QObject(parent)
    , m_worker(new RomanizerWorker)
{
    m_thread.setObjectName(QStringLiteral("romanizer"));
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RomanizerWorker::done, this, &Romanizer::done);
    m_thread.start(QThread::LowPriority);
}

Romanizer::~Romanizer()
{
    m_thread.quit();
    m_thread.wait();
}

void Romanizer::process(int generation, const QStringList &lines, bool romanize, const QString &chinese)
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, generation, lines, romanize, chinese] {
        worker->process(generation, lines, romanize, chinese);
    });
}
