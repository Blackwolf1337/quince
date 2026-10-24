#pragma once

class QNetworkAccessManager;
class QObject;

// A network manager for covers that answers from a 256 MiB disk cache before
// asking the network. Two caches must not share a directory: the n-th one
// (from 0) gets its own.
QNetworkAccessManager *createArtNetwork(QObject *parent, int n = 0);
