#pragma once

#include "Bridge.h"

#include <QAbstractItemModel>

#include <functional>
#include <memory>
#include <vector>

// Mirrors a Qt list model (core thread) into a slint::VectorModel (Slint
// thread). read() turns a row into plain data on the core thread, make()
// turns that into the Slint row on the Slint thread. The Slint model belongs
// to the window (attach() hands it over once); the core side only keeps a
// weak pointer, so it never ends up owning a Slint object.
template<typename Data, typename Row>
struct ModelMirror
{
    using Model = slint::VectorModel<Row>;
    using Read = std::function<Data(const QModelIndex &)>;
    using Make = std::function<Row(const Data &)>;
    using Attach = std::function<void(MainWindow &, std::shared_ptr<Model>)>;
    // after a row was made, e.g. to load its picture (Slint thread)
    using Made = std::function<void(const std::shared_ptr<Model> &, size_t row, const Data &)>;

    static void start(const bridge::Window &window, QAbstractItemModel *model, Read read, Make make,
                      Attach attach, Made made = {})
    {
        auto slot = std::make_shared<std::weak_ptr<Model>>();
        auto readRows = [model, read](int first, int last) {
            std::vector<Data> rows;
            for (int i = first; i <= last; i++)
                rows.push_back(read(model->index(i, 0)));
            return rows;
        };
        auto put = [slot, make, made](const std::shared_ptr<Model> &m, size_t first, const std::vector<Data> &rows,
                                      bool insert) {
            for (size_t i = 0; i < rows.size(); i++) {
                if (insert)
                    m->insert(first + i, make(rows[i]));
                else if (first + i < m->row_count())
                    m->set_row_data(first + i, make(rows[i]));
                if (made)
                    made(m, first + i, rows[i]);
            }
        };

        auto reset = [window, model, readRows, slot, put, attach] {
            bridge::toUi(window, [rows = readRows(0, model->rowCount() - 1), slot, put, attach](MainWindow &w) {
                auto m = slot->lock();
                if (!m) {
                    m = std::make_shared<Model>();
                    *slot = m;
                    attach(w, m);
                }
                m->clear();
                put(m, 0, rows, true);
            });
        };
        QObject::connect(model, &QAbstractItemModel::modelReset, model, reset);
        QObject::connect(model, &QAbstractItemModel::layoutChanged, model, reset);
        QObject::connect(model, &QAbstractItemModel::rowsMoved, model, reset);
        QObject::connect(model, &QAbstractItemModel::rowsInserted, model,
                         [window, readRows, slot, put](const QModelIndex &, int first, int last) {
            bridge::toUi(window, [rows = readRows(first, last), first, slot, put](MainWindow &) {
                if (auto m = slot->lock())
                    put(m, size_t(first), rows, true);
            });
        });
        QObject::connect(model, &QAbstractItemModel::rowsRemoved, model,
                         [window, slot](const QModelIndex &, int first, int last) {
            bridge::toUi(window, [first, last, slot](MainWindow &) {
                if (auto m = slot->lock())
                    for (int i = last; i >= first; i--)
                        if (size_t(i) < m->row_count())
                            m->erase(size_t(i));
            });
        });
        QObject::connect(model, &QAbstractItemModel::dataChanged, model,
                         [window, readRows, slot, put](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
            bridge::toUi(window, [rows = readRows(topLeft.row(), bottomRight.row()), first = topLeft.row(), slot,
                                  put](MainWindow &) {
                if (auto m = slot->lock())
                    put(m, size_t(first), rows, false);
            });
        });
        reset();
    }
};
