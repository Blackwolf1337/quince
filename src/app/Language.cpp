#include "app/Language.h"
#include "app/Settings.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>

Language::Language(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_code(settings->language())
{
    load();
    connect(m_settings, &Settings::changed, this, [this] {
        if (m_settings->language() == m_code)
            return;
        m_code = m_settings->language();
        QCoreApplication::removeTranslator(&m_app);
        QCoreApplication::removeTranslator(&m_qt);
        load();
        emit changed();
    });
}

QString Language::code() const
{
    const QLocale locale = m_code.isEmpty() ? QLocale::system() : QLocale(m_code);
    const QString code = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
    return code.isEmpty() ? QStringLiteral("en") : code;
}

void Language::load()
{
    // the texts are written in English: nothing to load for it
    const QLocale locale = m_code.isEmpty() ? QLocale() : QLocale(m_code);
    if (locale.language() == QLocale::English || locale.language() == QLocale::C)
        return;
    if (m_app.load(locale, QStringLiteral("quince"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&m_app);
    if (m_qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&m_qt);
}
