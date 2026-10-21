#pragma once

#include <QQmlNetworkAccessManagerFactory>

// Network for QML images: cover art goes through a disk cache and is taken
// from it without asking the server again, since cover URLs never change
// their content.
class ArtNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override;
};
