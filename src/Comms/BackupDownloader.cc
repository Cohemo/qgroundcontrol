/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "BackupDownloader.h"
#include "Settings/SettingsManager.h"
#include "Settings/AppSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(BackupDownloaderLog, "BackupDownloader")

BackupDownloader::BackupDownloader(QObject* parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
    , _zipTimer(new QTimer(this))
    , _sseReconnect(new QTimer(this))
{
    _zipTimer->setInterval(_zipIntervalMinutes * 60 * 1000);
    connect(_zipTimer, &QTimer::timeout, this, &BackupDownloader::_onZipTimerFired);

    _sseReconnect->setInterval(5000);
    _sseReconnect->setSingleShot(true);
    connect(_sseReconnect, &QTimer::timeout, this, &BackupDownloader::_connectSse);

    // Recuperar qué versión de cada mapa tenemos ya, para no re-descargar
    // todo tras reiniciar la app
    QSettings settings;
    settings.beginGroup("BackupDownloader");
    const QVariantMap stored = settings.value("mapLastModified").toMap();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        _mapLastModified.insert(it.key(), it.value().toDouble());
    }
}

BackupDownloader::~BackupDownloader()
{
    stop();
}

void BackupDownloader::setServerUrl(const QString& url)
{
    if (_serverUrl != url) {
        _serverUrl = url;
        emit serverUrlChanged();
        if (_running) {
            // Las descargas pendientes apuntan al servidor anterior: abortarlas
            _abortFileDownloads();
            _disconnectSse();
            _connectSse();
        }
    }
}

void BackupDownloader::setZipIntervalMinutes(int minutes)
{
    if (minutes < 1) minutes = 1;
    if (_zipIntervalMinutes != minutes) {
        _zipIntervalMinutes = minutes;
        _zipTimer->setInterval(minutes * 60 * 1000);
        emit zipIntervalMinutesChanged();
    }
}

void BackupDownloader::start()
{
    if (_running || _serverUrl.isEmpty()) return;
    _running = true;
    _connectSse();
    _zipTimer->start();
    _setStatus("Started");
}

void BackupDownloader::stop()
{
    _running = false;
    _disconnectSse();
    _abortFileDownloads();
    _zipTimer->stop();
    _sseReconnect->stop();
    _setStatus("Stopped");
}

void BackupDownloader::openDashboardInBrowser()
{
    if (_serverUrl.isEmpty()) {
        _setStatus("Cannot open browser: server URL is empty");
        return;
    }
    QDesktopServices::openUrl(QUrl(_serverUrl));
}

// ─── SSE Connection ──────────────────────────────────────────────────────────

void BackupDownloader::_connectSse()
{
    if (!_running || _serverUrl.isEmpty()) return;

    _disconnectSse();

    QNetworkRequest request(QUrl(_serverUrl + "/events"));
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    _sseReply = _nam->get(request);
    connect(_sseReply, &QNetworkReply::readyRead, this, &BackupDownloader::_onSseReadyRead);
    connect(_sseReply, &QNetworkReply::finished, this, &BackupDownloader::_onSseFinished);
    connect(_sseReply, &QNetworkReply::errorOccurred, this, &BackupDownloader::_onSseError);

    _sseBuffer.clear();
    _setStatus("Connecting SSE...");
}

void BackupDownloader::_disconnectSse()
{
    if (_sseReply) {
        // abort() emite finished() de forma síncrona → _onSseFinished() reentraría
        // aquí. Anulamos el puntero y desconectamos las señales antes de abortar.
        QNetworkReply* reply = _sseReply;
        _sseReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (_sseConnected) {
        _sseConnected = false;
        emit connectedChanged();
    }
}

void BackupDownloader::_onSseReadyRead()
{
    if (!_sseReply) return;

    if (!_sseConnected) {
        _sseConnected = true;
        emit connectedChanged();
        _setStatus("SSE connected");
    }

    _sseBuffer.append(_sseReply->readAll());

    while (_sseBuffer.contains("\n\n")) {
        int idx = _sseBuffer.indexOf("\n\n");
        QByteArray event = _sseBuffer.left(idx);
        _sseBuffer.remove(0, idx + 2);
        _processSseData(event);
    }
}

void BackupDownloader::_onSseFinished()
{
    _disconnectSse();
    if (_running) {
        _setStatus("SSE disconnected, reconnecting...");
        _sseReconnect->start();
    }
}

void BackupDownloader::_onSseError(QNetworkReply::NetworkError error)
{
    Q_UNUSED(error);
    const QString errStr = _sseReply ? _sseReply->errorString() : "Unknown";
    qCWarning(BackupDownloaderLog) << "SSE error:" << errStr;
    _setStatus("SSE error: " + errStr);
}

void BackupDownloader::_processSseData(const QByteArray& data)
{
    QString dataStr;
    for (const QByteArray& line : data.split('\n')) {
        if (line.startsWith("data: ")) {
            dataStr = QString::fromUtf8(line.mid(6));
        }
    }

    if (dataStr.isEmpty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(dataStr.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    const QString status = obj.value("status").toString();

    if (status == "saved" || status == "deleted" || status == "poi_saved" || status == "poi_deleted") {
        qCDebug(BackupDownloaderLog) << "POI event received:" << status;
        _setStatus("POI " + status + " — downloading...");
        downloadPoisNow();
    } else if (status == "map_saved" || status == "map_deleted") {
        // Si el servidor emite eventos de mapas, descargamos al cambio en vez de
        // depender solo del timer periódico
        qCDebug(BackupDownloaderLog) << "Map event received:" << status;
        _setStatus("Map " + status + " — downloading...");
        downloadAllZipsNow();
    }
}

// ─── Downloads ───────────────────────────────────────────────────────────────

void BackupDownloader::downloadPoisNow()
{
    if (_serverUrl.isEmpty()) return;

    QNetworkRequest request(QUrl(_serverUrl + "/api/pois"));
    request.setTransferTimeout(30000);
    QNetworkReply* reply = _nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            _setStatus("POI download failed: " + reply->errorString());
            emit downloadError(reply->errorString());
            return;
        }

        // Nombre fijo: es un espejo del servidor, no un historial. QSaveFile
        // garantiza que un corte a mitad de escritura no corrompe la copia previa.
        const QString filepath = _downloadDir() + "/pois.json";

        QSaveFile file(filepath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            if (file.commit()) {
                _setStatus("Downloaded: pois.json");
                emit downloadComplete("pois.json");
                qCDebug(BackupDownloaderLog) << "POIs saved to:" << filepath;
                return;
            }
        }
        _setStatus("Failed to write: pois.json");
        emit downloadError("Cannot write " + filepath);
    });
}

void BackupDownloader::downloadAllZipsNow()
{
    if (_serverUrl.isEmpty()) return;

    _setStatus("Fetching map list...");

    QNetworkRequest request(QUrl(_serverUrl + "/api/maps"));
    request.setTransferTimeout(30000);
    QNetworkReply* reply = _nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            _setStatus("Map list failed: " + reply->errorString());
            emit downloadError(reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray maps = doc.object().value("maps").toArray();

        int queued = 0;
        int skipped = 0;
        for (const QJsonValue& mapVal : maps) {
            QJsonObject map = mapVal.toObject();
            const QString name = map.value("name").toString();
            const QString downloadUrl = map.value("download_url").toString();

            if (name.isEmpty() || downloadUrl.isEmpty()) continue;

            // Nombre fijo: cada descarga sobrescribe la copia anterior
            const QString filename = name + ".zip";

            // El listado incluye last_modified (mtime más reciente del mapa):
            // si ya tenemos esa versión, no gastamos radio en re-descargarla
            const double lastModified = map.value("last_modified").toDouble();
            if (lastModified > 0.0 && _mapLastModified.value(filename, -1.0) >= lastModified) {
                skipped++;
                continue;
            }

            const QUrl url = downloadUrl.startsWith("http")
                ? QUrl(downloadUrl)
                : QUrl(_serverUrl + downloadUrl);

            _enqueueDownload(url, filename, lastModified);
            queued++;
        }

        _setStatus(QString("Queued %1 map(s), %2 unchanged").arg(queued).arg(skipped));
    });
}

void BackupDownloader::_enqueueDownload(const QUrl& url, const QString& filename, double lastModified)
{
    // Evitar encolar dos veces el mismo fichero (p. ej. timer + evento SSE)
    if (_fileReply && _fileFinalPath == _downloadDir() + "/" + filename) return;
    for (const PendingDownload& pending : _downloadQueue) {
        if (pending.filename == filename) return;
    }

    _downloadQueue.enqueue({url, filename, lastModified});
    if (!_fileReply) {
        _startNextDownload();
    }
}

void BackupDownloader::_startNextDownload()
{
    if (_fileReply || _downloadQueue.isEmpty()) return;

    const PendingDownload next = _downloadQueue.dequeue();
    _fileFinalPath = _downloadDir() + "/" + next.filename;
    _filePendingMtime = next.lastModified;

    // Se escribe a .part y se renombra al terminar: si la radio se corta a
    // mitad, la copia buena anterior queda intacta
    _fileOut.setFileName(_fileFinalPath + ".part");
    if (!_fileOut.open(QIODevice::WriteOnly)) {
        _setStatus("Cannot write: " + next.filename);
        emit downloadError("Cannot write " + _fileOut.fileName());
        _startNextDownload();
        return;
    }

    QNetworkRequest request(next.url);
    // Tope generoso para ZIPs grandes por radio lenta, pero evita que una
    // conexión colgada bloquee la cola indefinidamente
    request.setTransferTimeout(10 * 60 * 1000);

    // Si el servidor soporta If-Modified-Since responderá 304 y nos ahorramos
    // re-descargar mapas que no han cambiado; si lo ignora, simplemente llega un 200
    const QByteArray lastModified = _lastModified.value(next.filename);
    if (!lastModified.isEmpty()) {
        request.setRawHeader("If-Modified-Since", lastModified);
    }

    _fileReply = _nam->get(request);
    connect(_fileReply, &QNetworkReply::readyRead, this, &BackupDownloader::_onFileReadyRead);
    connect(_fileReply, &QNetworkReply::finished, this, &BackupDownloader::_onFileFinished);

    _setStatus("Downloading: " + next.filename);
}

void BackupDownloader::_onFileReadyRead()
{
    // Escritura incremental: el fichero nunca se acumula entero en RAM
    if (_fileReply && _fileOut.isOpen()) {
        _fileOut.write(_fileReply->readAll());
    }
}

void BackupDownloader::_onFileFinished()
{
    QNetworkReply* reply = _fileReply;
    _fileReply = nullptr;
    if (!reply) return;
    reply->deleteLater();

    _fileOut.close();

    const QString filename = QFileInfo(_fileFinalPath).fileName();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatus == 304) {
        // El servidor confirma que no cambió: conservamos la copia existente
        QFile::remove(_fileFinalPath + ".part");
        _setStatus("Unchanged: " + filename);
    } else if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(_fileFinalPath + ".part");
        _setStatus("Download failed: " + filename);
        emit downloadError(filename + ": " + reply->errorString());
    } else {
        QFile::remove(_fileFinalPath);  // rename() falla si el destino existe
        if (QFile::rename(_fileFinalPath + ".part", _fileFinalPath)) {
            const QByteArray lastModified = reply->rawHeader("Last-Modified");
            if (!lastModified.isEmpty()) {
                _lastModified.insert(filename, lastModified);
            }
            // Registrar (y persistir) qué versión tenemos, solo tras éxito:
            // una descarga fallida debe reintentarse en la siguiente pasada
            if (_filePendingMtime > 0.0) {
                _mapLastModified.insert(filename, _filePendingMtime);
                QSettings settings;
                settings.beginGroup("BackupDownloader");
                QVariantMap stored;
                for (auto it = _mapLastModified.constBegin(); it != _mapLastModified.constEnd(); ++it) {
                    stored.insert(it.key(), it.value());
                }
                settings.setValue("mapLastModified", stored);
            }
            _setStatus("Downloaded: " + filename);
            emit downloadComplete(filename);
            qCDebug(BackupDownloaderLog) << "File saved:" << _fileFinalPath;
        } else {
            _setStatus("Write failed: " + filename);
            emit downloadError("Cannot rename " + _fileFinalPath + ".part");
        }
    }

    _startNextDownload();
}

void BackupDownloader::_abortFileDownloads()
{
    _downloadQueue.clear();

    if (_fileReply) {
        // Mismo patrón que _disconnectSse(): anular y desconectar antes de
        // abort() para evitar la reentrada por finished()
        QNetworkReply* reply = _fileReply;
        _fileReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    if (_fileOut.isOpen()) {
        _fileOut.close();
        QFile::remove(_fileFinalPath + ".part");
    }
}

void BackupDownloader::_onZipTimerFired()
{
    qCDebug(BackupDownloaderLog) << "Periodic ZIP download triggered";
    downloadAllZipsNow();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void BackupDownloader::_setStatus(const QString& status)
{
    _lastStatus = status;
    emit lastStatusChanged();
    qCDebug(BackupDownloaderLog) << status;
}

QString BackupDownloader::_downloadDir() const
{
    QString baseDir;
    if (SettingsManager::instance() && SettingsManager::instance()->appSettings()) {
        baseDir = SettingsManager::instance()->appSettings()->savePath()->rawValue().toString();
    }

    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    const QString dir = QDir(baseDir).filePath("Mapas Descargados");
    QDir().mkpath(dir);
    return dir;
}
