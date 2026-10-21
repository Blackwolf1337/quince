#include "ytm/SearchSuggestions.h"
#include "app/AppController.h"
#include "ytm/InnerTube.h"
#include "ytm/Parser.h"

#include <QJsonObject>

namespace {

// typing pause before suggestions are fetched
constexpr int kDelayMs = 150;

} // namespace

SearchSuggestions::SearchSuggestions(QObject *parent)
    : QObject(parent)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(kDelayMs);
    connect(&m_delay, &QTimer::timeout, this, &SearchSuggestions::fetch);
}

void SearchSuggestions::setInput(const QString &input)
{
    if (m_input == input)
        return;
    m_input = input;
    emit inputChanged();

    ++m_generation;
    if (m_input.trimmed().isEmpty()) {
        m_delay.stop();
        m_queries.clear();
        m_items.clear();
        emit resultsChanged();
        return;
    }
    m_delay.start();
}

void SearchSuggestions::fetch()
{
    const int generation = m_generation;
    AppController::instance()->innerTube()->post(
        QStringLiteral("music/get_search_suggestions"),
        QJsonObject{{QStringLiteral("input"), m_input}}, this,
        [this, generation](const QJsonObject &response, const QString &error) {
            if (generation != m_generation || !error.isEmpty())
                return;
            const QVariantMap result = ytm::parseSuggestions(response);
            m_queries = result.value(QStringLiteral("queries")).toList();
            m_items = result.value(QStringLiteral("items")).toList();
            emit resultsChanged();
        });
}
