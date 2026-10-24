#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class AppController;
class BrowsePage;

// The page stack of the main window, as qml/Main.qml keeps it: one
// BrowsePage per page; the pages left by going back stay as requests for
// going forward again. The downloads view sits at the bottom of the stack as
// a place without a page. Core thread; knows nothing of Slint.
class Pages : public QObject
{
public:
    struct Request
    {
        QString browseId;
        QString params;
        QString query;
    };
    struct Hooks
    {
        // a page was made (before it starts loading), and went away
        std::function<void(BrowsePage *page, int id)> created;
        std::function<void(int id)> dropped;
        // another page is open, or back and forward changed
        std::function<void()> changed;
    };

    explicit Pages(AppController &app, QObject *parent = nullptr);

    // before the first page
    void setHooks(Hooks hooks) { m_hooks = std::move(hooks); }

    // a section of the sidebar: home, explore, library, liked, history,
    // downloads or a playlist (its browse id); starts a new stack
    void goSection(const QString &name, const QString &browseId);
    void openPage(const Request &request);
    void back();
    void forward();
    // the start section of the settings (after signing in or out too)
    void goStart();

    BrowsePage *current() const;
    // -1 while the downloads view shows
    int currentId() const;
    bool canBack() const { return m_stack.size() > 1; }
    bool canForward() const { return !m_forward.isEmpty(); }
    // what the sidebar marks: the section, "downloads" while that view shows,
    // nothing for a page opened from the downloads view
    QString shownSection() const;

    // of the current page: the row's data, an item in it (card -1: the row's
    // own item, the header's map for the header)
    QString rowType(int row) const;
    QVariantMap row(int row) const;
    QVariantMap item(int row, int card) const;

    // a chip of the chips row: the page with that filter, or without it
    void applyChip(int row, int chip);
    // an option of the chips row's sort menu
    void sortBy(int row, int option);

private:
    struct Entry
    {
        BrowsePage *page = nullptr; // none: the downloads view
        int id = -1;
    };

    void push(const Request &request);
    void clear();
    void drop(const Entry &entry);

    AppController &m_app;
    Hooks m_hooks;
    QString m_section = QStringLiteral("home");
    QList<Entry> m_stack;
    QList<Request> m_forward;
    int m_nextId = 0;
};
