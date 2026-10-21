#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

class Settings;

// Quince's texts and Qt's own (dialog buttons) in the language of the
// settings: the system's, English or German. A change applies at once;
// changed() tells the parts that keep texts (QML, the tray menu).
class Language : public QObject
{
    Q_OBJECT

public:
    explicit Language(Settings *settings, QObject *parent = nullptr);

    // ISO 639-1 code of the language in use ("de"), for YouTube's own texts
    QString code() const;

signals:
    void changed();

private:
    void load();

    Settings *m_settings = nullptr;
    QString m_code;
    QTranslator m_app;
    QTranslator m_qt;
};
