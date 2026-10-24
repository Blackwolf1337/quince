#pragma once

#include "ytm/BrowsePage.h"

#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

// BrowsePage as QML creates it: loads once all its properties are set.
class QmlBrowsePage : public BrowsePage, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_NAMED_ELEMENT(BrowsePage)

public:
    explicit QmlBrowsePage(QObject *parent = nullptr)
        : BrowsePage(parent)
    {
    }

    void classBegin() override {}
    void componentComplete() override { complete(); }
};
