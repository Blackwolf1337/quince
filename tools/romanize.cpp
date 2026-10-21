// Prints what the lyrics romanizer makes of lines:
//   quince-romanize [--hant|--hans] line...
#include "lyrics/Romanizer.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList lines = app.arguments().mid(1);
    QString chinese;
    if (!lines.isEmpty() && (lines.front() == u"--hant" || lines.front() == u"--hans"))
        chinese = lines.takeFirst().mid(2);

    RomanizerEngine engine;
    const QString language = RomanizerEngine::detect(lines);
    const QStringList texts = language == u"zh" && !chinese.isEmpty() ? engine.convertChinese(lines, chinese) : lines;
    const QStringList latin = engine.romanize(texts, language);
    QTextStream out(stdout);
    out << "language: " << (language.isEmpty() ? QStringLiteral("-") : language) << '\n';
    for (qsizetype i = 0; i < texts.size(); ++i)
        out << texts.at(i) << "\n  " << latin.value(i) << '\n';
    if (!engine.error().isEmpty())
        out << "error: " << engine.error() << '\n';
    return 0;
}
