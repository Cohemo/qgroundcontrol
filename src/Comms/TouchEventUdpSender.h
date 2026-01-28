/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QtQmlIntegration/QtQmlIntegration>

class Vehicle;

/// @brief Envía eventos táctiles por UDP
/// Esta clase captura eventos táctiles del QML y los envía como mensajes UDP
/// a una dirección IP y puerto especificados
class TouchEventUdpSender : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit TouchEventUdpSender(QObject* parent = nullptr);
    ~TouchEventUdpSender();

    /// @brief Envía un evento táctil presionado
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    Q_INVOKABLE void sendPressed(double x, double y);

    /// @brief Envía un evento de movimiento táctil
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    Q_INVOKABLE void sendPositionChanged(double x, double y);

    /// @brief Envía un evento táctil liberado
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    Q_INVOKABLE void sendReleased(double x, double y);

    /// @brief Envía un evento de clic
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    Q_INVOKABLE void sendClicked(double x, double y);

    /// @brief Envía un evento de doble clic
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    Q_INVOKABLE void sendDoubleClicked(double x, double y);

    /// @brief Establece el intervalo mínimo entre eventos de movimiento (en ms)
    /// @param interval Intervalo en milisegundos (por defecto 50ms = 20 eventos/segundo)
    Q_INVOKABLE void setThrottleInterval(int interval);

    /// @brief Configura la dirección IP y puerto de destino
    /// @param address Dirección IP de destino (ej: "192.168.1.100")
    /// @param port Puerto de destino
    Q_INVOKABLE void setTarget(const QString& address, quint16 port);

    /// @brief Obtiene la dirección IP actual
    Q_INVOKABLE QString targetAddress() const { return _targetAddress.toString(); }

    /// @brief Obtiene el puerto actual
    Q_INVOKABLE quint16 targetPort() const { return _targetPort; }

private slots:
    /// @brief Actualiza la IP destino cuando cambia el vehículo activo
    void _onActiveVehicleChanged(Vehicle* vehicle);

private:
    /// @brief Envía un mensaje UDP con el evento
    /// @param eventType Tipo de evento (pressed, moved, released, clicked, doubleClicked)
    /// @param x Coordenada X del evento
    /// @param y Coordenada Y del evento
    void sendEvent(const QString& eventType, double x, double y);

    /// @brief Verifica si debe throttlear los eventos de movimiento
    /// @return true si debe enviar el evento, false si debe ignorarlo
    bool shouldThrottle();

    /// @brief Obtiene la IP del enlace del vehículo
    QString _getVehicleLinkIp(Vehicle* vehicle) const;

    /// @brief Actualiza la IP destino basada en el vehículo
    void _updateTargetFromVehicle(Vehicle* vehicle);

    QUdpSocket*     _udpSocket;
    QHostAddress    _targetAddress;
    quint16         _targetPort;
    qint64          _lastMovementTimestamp;
    int             _throttleInterval;  // Intervalo mínimo entre eventos de movimiento en ms

    static constexpr int kOnboardComputerLastOctet = 163; ///< Last octet of onboard computer IP (192.168.X.163)
};
