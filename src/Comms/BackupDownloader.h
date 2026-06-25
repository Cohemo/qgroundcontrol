/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QFile>
#include <QQueue>
#include <QHash>
#include <QtQmlIntegration/QtQmlIntegration>

/// @brief Native auto-downloader for the UGV backup server.
/// Connects via SSE to receive real-time POI events and downloads pois.json
/// on each save/delete. Periodically downloads all map ZIPs.
/// Files are saved under QGC's save path in "Mapas Descargados".
/// The dashboard UI itself is opened in an external browser (QtWebView in
/// QGC swallows touch events on Android — see feature/backup-dashboard-launcher).
class BackupDownloader : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(int     zipIntervalMinutes READ zipIntervalMinutes WRITE setZipIntervalMinutes NOTIFY zipIntervalMinutesChanged)
    Q_PROPERTY(bool    connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString lastStatus READ lastStatus NOTIFY lastStatusChanged)

public:
    explicit BackupDownloader(QObject* parent = nullptr);
    ~BackupDownloader();

    QString serverUrl() const { return _serverUrl; }
    void setServerUrl(const QString& url);

    int zipIntervalMinutes() const { return _zipIntervalMinutes; }
    void setZipIntervalMinutes(int minutes);

    bool connected() const { return _sseConnected; }
    QString lastStatus() const { return _lastStatus; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void downloadPoisNow();
    Q_INVOKABLE void downloadAllZipsNow();
    Q_INVOKABLE void openDashboardInBrowser();

signals:
    void serverUrlChanged();
    void zipIntervalMinutesChanged();
    void connectedChanged();
    void lastStatusChanged();
    void downloadComplete(const QString& filename);
    void downloadError(const QString& error);

private slots:
    void _onSseReadyRead();
    void _onSseFinished();
    void _onSseError(QNetworkReply::NetworkError error);
    void _onZipTimerFired();
    void _onFileReadyRead();
    void _onFileFinished();

private:
    void _connectSse();
    void _disconnectSse();
    void _processSseData(const QByteArray& data);
    void _enqueueDownload(const QUrl& url, const QString& filename, double lastModified = 0.0);
    void _startNextDownload();
    void _abortFileDownloads();
    void _setStatus(const QString& status);
    QString _downloadDir() const;
    /// @brief Subcarpeta por vehículo derivada de la IP del servidor
    /// (192.168.N.163 → "UGV N"), o cadena vacía si no se puede determinar.
    QString _vehicleSubdir() const;
    /// @brief Clave de control de versiones cualificada por vehículo
    /// ("UGV N/fichero"), para que el dedup no se cruce entre UGVs.
    QString _versionKey(const QString& filename) const;

    struct PendingDownload {
        QUrl    url;
        QString filename;
        double  lastModified = 0.0;  ///< last_modified del listado /api/maps (epoch)
    };

    QNetworkAccessManager*  _nam            = nullptr;
    QNetworkReply*          _sseReply       = nullptr;
    QTimer*                 _zipTimer       = nullptr;
    QTimer*                 _sseReconnect   = nullptr;

    QString     _serverUrl;
    int         _zipIntervalMinutes = 5;
    bool        _sseConnected       = false;
    bool        _running            = false;
    QString     _lastStatus;
    QByteArray  _sseBuffer;

    // Descargas de ficheros: una en vuelo cada vez (cola secuencial) para no
    // competir con el vídeo y la telemetría por el ancho de banda de la radio.
    QQueue<PendingDownload>     _downloadQueue;
    QNetworkReply*              _fileReply      = nullptr;
    QFile                       _fileOut;
    QString                     _fileFinalPath;
    double                      _filePendingMtime = 0.0;
    QHash<QString, QByteArray>  _lastModified;      ///< filename → Last-Modified HTTP (para If-Modified-Since)
    QHash<QString, double>      _mapLastModified;   ///< filename → last_modified ya descargado (persistido en QSettings)
};
