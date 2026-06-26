/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtQmlIntegration/QtQmlIntegration>

#include "LinkConfiguration.h"  // SharedLinkConfigurationPtr

class QNetworkAccessManager;

/// @brief Selector de UGV para la barra de vuelo (singleton QML).
///
/// El usuario elige UGV1 o UGV2 (excluyente, solo uno a la vez). Al elegir un UGV:
///   - manda "start" al ordenador de abordo del elegido y "stop" al del otro
///     (HTTP POST {"mensaje":...} a 10.0.(N+1).0:8000/stream).
///   - conecta el link MAVLink con su PX4 (UDP a 10.0.(N+1).0:8234) y desconecta
///     el del otro.
/// La IP va adelantada un número respecto al del UGV: UGV N -> 10.0.(N+1).0.
class UgvSelector : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int selectedUgv READ selectedUgv NOTIFY selectedUgvChanged)

public:
    explicit UgvSelector(QObject *parent = nullptr);

    int selectedUgv() const { return _selectedUgv; }  ///< 0 = ninguno, 1, 2

    /// Selecciona el UGV (1 o 2): start a su onboard, stop al otro, conecta su
    /// PX4 y desconecta el del otro.
    Q_INVOKABLE void selectUgv(int ugv);

    /// Desconecta el UGV activo: manda "stop" a su stream, desconecta su link PX4
    /// y vuelve a estado "ninguno seleccionado".
    Q_INVOKABLE void disconnectCurrent();

signals:
    void selectedUgvChanged();

private:
    QString _onboardIp(int ugv) const;  ///< 10.0.(ugv+1).0
    void _sendStreamCommand(int ugv, const QString &command);
    void _connectPx4(int ugv);
    void _disconnectPx4(int ugv);
    /// Conecta el PX4 pendiente (_pendingUgv) en cuanto el del otro UGV haya
    /// terminado de desconectar y liberado el puerto UDP; reintenta si aún no.
    void _tryConnectPending();

    int _selectedUgv = 0;
    int _pendingUgv = 0;                        ///< UGV a conectar cuando el otro libere el puerto
    int _pendingRetries = 0;
    QNetworkAccessManager *_nam = nullptr;
    SharedLinkConfigurationPtr _px4Config[3];  ///< Config del link PX4 por UGV (índices 1 y 2)

    static constexpr quint16 kPx4Port = 8234;     ///< Puerto UDP del link con la PX4
    static constexpr int kStreamPort = 8000;      ///< Puerto HTTP de control del stream
    static constexpr int kConnectRetryMs = 100;   ///< Espera entre comprobaciones de desconexión del otro UGV
    static constexpr int kConnectMaxRetries = 30; ///< Tope (~3 s) por si el otro link no reporta la desconexión
};
