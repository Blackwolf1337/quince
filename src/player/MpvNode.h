#pragma once

#include <QByteArray>
#include <QVariant>

#include <mpv/client.h>

#include <deque>
#include <vector>

// Builds an mpv_node tree from Qt values and owns its memory. Live for the
// duration of one mpv_command_node(_async) call; mpv copies the arguments.
class MpvNode
{
public:
    explicit MpvNode(const QVariant &value);

    MpvNode(const MpvNode &) = delete;
    MpvNode &operator=(const MpvNode &) = delete;

    mpv_node *get() { return &m_root; }

    // mpv_node owned by mpv (property reads, command results) to Qt values.
    static QVariant toVariant(const mpv_node *node);

private:
    void build(mpv_node &dst, const QVariant &value);
    char *store(const QString &text);

    mpv_node m_root{};
    // deque keeps element addresses stable while the tree grows
    std::deque<QByteArray> m_strings;
    std::deque<std::vector<mpv_node>> m_values;
    std::deque<std::vector<char *>> m_keys;
    std::deque<mpv_node_list> m_lists;
};
