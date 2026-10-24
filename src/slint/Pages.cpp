#include "Pages.h"
#include "app/AppController.h"
#include "app/Settings.h"
#include "ytm/Account.h"
#include "ytm/BrowsePage.h"

#include <QHash>

namespace {

const QString kDownloads = QStringLiteral("downloads");

QVariantMap mapAt(const QVariantList &list, int index)
{
    return index >= 0 && index < list.size() ? list.at(index).toMap() : QVariantMap();
}

} // namespace

Pages::Pages(AppController &app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

void Pages::goSection(const QString &name, const QString &browseId)
{
    m_section = name;
    m_move = Move::Section;
    m_forward.clear();
    clear();
    if (name == kDownloads) {
        m_stack.append(Entry());
        m_hooks.changed();
        return;
    }
    push({browseId, {}, {}});
}

void Pages::openPage(const Request &request)
{
    m_forward.clear();
    m_move = Move::Push;
    push(request);
}

void Pages::back()
{
    if (m_stack.size() <= 1)
        return;
    m_move = Move::Back;
    const Entry top = m_stack.takeLast();
    m_forward.append({top.page->browseId(), top.page->params(), top.page->query()});
    drop(top);
    m_hooks.changed();
}

void Pages::forward()
{
    if (m_forward.isEmpty())
        return;
    m_move = Move::Push;
    push(m_forward.takeLast());
}

void Pages::goStart()
{
    const QString page = m_app.settings()->startPage();
    static const QHash<QString, QString> ids{
        {QStringLiteral("explore"), QStringLiteral("FEmusic_explore")},
        {QStringLiteral("library"), QStringLiteral("FEmusic_library_landing")},
        {QStringLiteral("liked"), QStringLiteral("VLLM")},
    };
    if (ids.contains(page) && (page == u"explore" || m_app.account()->signedIn()))
        goSection(page, ids.value(page));
    else
        goSection(QStringLiteral("home"), {});
}

BrowsePage *Pages::current() const
{
    return m_stack.isEmpty() ? nullptr : m_stack.last().page;
}

int Pages::currentId() const
{
    return m_stack.isEmpty() ? -1 : m_stack.last().id;
}

QString Pages::shownSection() const
{
    if (!m_stack.isEmpty() && !m_stack.first().page)
        return m_stack.size() == 1 ? kDownloads : QString();
    return m_section;
}

QString Pages::rowType(int row) const
{
    const BrowsePage *page = current();
    return page ? page->data(page->index(row), BrowsePage::RowTypeRole).toString() : QString();
}

QVariantMap Pages::row(int row) const
{
    const BrowsePage *page = current();
    return page ? page->data(page->index(row), BrowsePage::RowRole).toMap() : QVariantMap();
}

QVariantMap Pages::item(int row, int card) const
{
    const QString type = rowType(row);
    const QVariantMap data = this->row(row);
    if (card >= 0)
        return mapAt(data.value(QStringLiteral("items")).toList(), card);
    if (type == u"item" || type == u"card")
        return data.value(QStringLiteral("item")).toMap();
    if (type == u"header")
        return data;
    return {};
}

void Pages::applyChip(int row, int chip)
{
    BrowsePage *page = current();
    const QVariantMap c = mapAt(this->row(row).value(QStringLiteral("chips")).toList(), chip);
    if (!page || c.isEmpty())
        return;
    if (c.value(QStringLiteral("selected")).toBool()) {
        page->setParams({});
        // library filters lead back to the library itself
        const QString back = c.value(QStringLiteral("deselectBrowseId")).toString();
        if (!back.isEmpty())
            page->setBrowseId(back);
        return;
    }
    page->setParams(c.value(QStringLiteral("params")).toString());
    const QString browseId = c.value(QStringLiteral("browseId")).toString();
    if (!browseId.isEmpty())
        page->setBrowseId(browseId);
}

void Pages::sortBy(int row, int option)
{
    BrowsePage *page = current();
    const QVariantMap sort = this->row(row).value(QStringLiteral("sort")).toMap();
    const QVariantMap o = mapAt(sort.value(QStringLiteral("options")).toList(), option);
    if (page)
        page->sortBy(o.value(QStringLiteral("token")).toString());
}

void Pages::push(const Request &request)
{
    auto *page = new BrowsePage(this);
    page->setBrowseId(request.browseId);
    page->setParams(request.params);
    page->setQuery(request.query);
    const Entry entry{page, m_nextId++};
    m_stack.append(entry);
    m_hooks.created(page, entry.id);
    m_hooks.changed();
    page->complete();
}

void Pages::clear()
{
    while (!m_stack.isEmpty())
        drop(m_stack.takeLast());
}

void Pages::drop(const Entry &entry)
{
    if (!entry.page)
        return;
    // its answers may still be on the way: they change nothing the views see
    entry.page->disconnect();
    entry.page->deleteLater();
    m_hooks.dropped(entry.id);
}
