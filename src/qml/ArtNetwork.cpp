#include "qml/ArtNetwork.h"
#include "app/ArtCache.h"

#include <QAtomicInt>

// QML creates one manager per thread that loads from the network (normally
// only the image reader thread); each gets its own cache directory.
QNetworkAccessManager *ArtNetworkFactory::create(QObject *parent)
{
    static QAtomicInt instances;
    return createArtNetwork(parent, instances.fetchAndAddRelaxed(1));
}
