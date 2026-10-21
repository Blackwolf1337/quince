#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Suggestions while typing in the search field, fetched a moment after the
// last key press.
class SearchSuggestions : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString input READ input WRITE setInput NOTIFY inputChanged)
    Q_PROPERTY(QVariantList queries READ queries NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY resultsChanged)

public:
    explicit SearchSuggestions(QObject *parent = nullptr);

    QString input() const { return m_input; }
    void setInput(const QString &input);

    QVariantList queries() const { return m_queries; }
    QVariantList items() const { return m_items; }

signals:
    void inputChanged();
    void resultsChanged();

private:
    void fetch();

    QString m_input;
    QVariantList m_queries;
    QVariantList m_items;
    QTimer m_delay;
    int m_generation = 0;
};
