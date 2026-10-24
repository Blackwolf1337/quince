#pragma once

#include "main.h"

#include <QObject>
#include <QString>

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

class QNetworkAccessManager;

// Covers as slint::Image. get() runs on the Slint thread; the download (with
// the disk cache the QML frontend uses too) runs on the core thread, the
// decoding on Qt's thread pool, the image is made on the Slint thread.
class ImageCache
{
public:
    using Done = std::function<void(const slint::Image &)>;

    ImageCache();
    ~ImageCache();

    // core thread, once its event loop exists: the side that downloads
    void startCore(QObject *coreParent);
    // Slint thread: drops the images (before the window goes)
    void clear();

    // Slint thread: the cover at url in about pixels x pixels; done() runs on
    // the Slint thread, at once when the image is cached. An empty url gives
    // an empty image.
    void get(const std::string &url, int pixels, Done done);

private:
    class Fetcher;
    struct Entry
    {
        slint::Image image;
        std::list<std::string>::iterator age;
    };

    void send(const std::string &key, const std::string &url, int pixels);
    void arrived(const std::string &key, int width, int height, std::vector<unsigned char> rgba);

    Fetcher *m_fetcher = nullptr; // core thread object, known to the Slint thread once it exists
    std::unordered_map<std::string, Entry> m_images;
    std::list<std::string> m_ages; // front: used last
    std::unordered_map<std::string, std::vector<Done>> m_waiting;
    // asked for before the core side existed: key -> url, pixels
    std::unordered_map<std::string, std::pair<std::string, int>> m_unsent;
};
