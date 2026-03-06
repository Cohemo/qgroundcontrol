/****************************************************************************
 *
 * (c) 2009-2025 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TouchEventUdpSender.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "LinkInterface.h"
#include "UDPLink.h"
#include "TCPLink.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

TouchEventUdpSender::TouchEventUdpSender(QObject* parent)
    : QObject(parent)
    , _udpSocket(new QUdpSocket(this))
    , _targetAddress("192.168.168.163")  // IP por defecto, se actualizará con el vehículo activo
    , _targetPort(12345)                 // Puerto UDP (cambiar si es necesario)
    , _lastMovementTimestamp(0)
    , _throttleInterval(50)  // Throttling: máximo 20 eventos de movimiento por segundo
{
    // No es necesario bind() para enviar paquetes UDP
    // El socket está listo para enviar sin configuración adicional

    // Conectar con MultiVehicleManager para recibir cambios de vehículo activo
    MultiVehicleManager* multiVehicleMgr = MultiVehicleManager::instance();
    if (multiVehicleMgr) {
        connect(multiVehicleMgr, &MultiVehicleManager::activeVehicleChanged,
                this, &TouchEventUdpSender::_onActiveVehicleChanged);

        // Actualizar con el vehículo activo actual si existe
        if (multiVehicleMgr->activeVehicle()) {
            _updateTargetFromVehicle(multiVehicleMgr->activeVehicle());
        }
    }
}

TouchEventUdpSender::~TouchEventUdpSender()
{
    // El socket se eliminará automáticamente por Qt parent-child
}

void TouchEventUdpSender::sendPressed(double x, double y)
{
    sendEvent("pressed", x, y);
}

void TouchEventUdpSender::sendPositionChanged(double x, double y)
{
    // Aplicar throttling para eventos de movimiento
    if (shouldThrottle()) {
        return;
    }
    sendEvent("moved", x, y);
}

void TouchEventUdpSender::sendReleased(double x, double y)
{
    sendEvent("released", x, y);
}

void TouchEventUdpSender::sendClicked(double x, double y)
{
    sendEvent("clicked", x, y);
}

void TouchEventUdpSender::sendDoubleClicked(double x, double y)
{
    sendEvent("doubleClicked", x, y);
}

void TouchEventUdpSender::setThrottleInterval(int interval)
{
    _throttleInterval = interval;
}

void TouchEventUdpSender::setTarget(const QString& address, quint16 port)
{
    _targetAddress = QHostAddress(address);
    _targetPort = port;
}

bool TouchEventUdpSender::shouldThrottle()
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeSinceLastEvent = currentTime - _lastMovementTimestamp;

    if (timeSinceLastEvent >= _throttleInterval) {
        _lastMovementTimestamp = currentTime;
        return false;  // No throttle, enviar el evento
    }

    return true;  // Throttle, ignorar el evento
}

void TouchEventUdpSender::sendEvent(const QString& eventType, double x, double y)
{
    // Validar que el socket esté disponible
    if (!_udpSocket) {
        return;
    }

    // Crear un objeto JSON con la información del evento
    QJsonObject eventObject;
    eventObject["type"] = eventType;
    eventObject["x"] = x;
    eventObject["y"] = y;
    eventObject["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    // Convertir a JSON y enviar por UDP
    QJsonDocument doc(eventObject);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Enviar el datagrama UDP
    // writeDatagram retorna el número de bytes enviados, o -1 si hay error
    qint64 bytesSent = _udpSocket->writeDatagram(data, _targetAddress, _targetPort);

    // Log para debug (descomentar si es necesario)
    // if (bytesSent < 0) {
    //     qWarning() << "Failed to send touch event:" << eventType << _udpSocket->errorString();
    // } else {
    //     qDebug() << "Touch event sent:" << eventType << "(" << x << "," << y << ") - Bytes:" << bytesSent;
    // }

    Q_UNUSED(bytesSent);  // Evitar warning en release builds
}

void TouchEventUdpSender::_onActiveVehicleChanged(Vehicle* vehicle)
{
    _updateTargetFromVehicle(vehicle);
}

void TouchEventUdpSender::_updateTargetFromVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        // Sin vehículo activo, mantener IP por defecto
        return;
    }

    // Obtener la IP del enlace del vehículo (PX4)
    const QString vehicleLinkIp = _getVehicleLinkIp(vehicle);
    if (vehicleLinkIp.isEmpty()) {
        qWarning() << "TouchEventUdpSender: Could not get link IP for vehicle:" << vehicle->id();
        return;
    }

    // Extraer subnet (primeros 3 octetos) de la IP del vehículo
    const QStringList ipParts = vehicleLinkIp.split('.');
    if (ipParts.size() < 3) {
        qWarning() << "TouchEventUdpSender: Invalid IP format:" << vehicleLinkIp;
        return;
    }

    // Construir IP del ordenador de abordo: 192.168.X.163
    const QString onboardIp = QString("%1.%2.%3.%4")
                                  .arg(ipParts[0])
                                  .arg(ipParts[1])
                                  .arg(ipParts[2])
                                  .arg(kOnboardComputerLastOctet);

    // Actualizar la dirección destino
    _targetAddress = QHostAddress(onboardIp);

    qDebug() << "TouchEventUdpSender: Target updated for vehicle" << vehicle->id()
             << "-> IP:" << onboardIp << "Port:" << _targetPort;
}

QString TouchEventUdpSender::_getVehicleLinkIp(Vehicle* vehicle) const
{
    if (!vehicle) {
        return QString();
    }

    VehicleLinkManager* linkManager = vehicle->vehicleLinkManager();
    if (!linkManager) {
        return QString();
    }

    SharedLinkInterfacePtr primaryLink = linkManager->primaryLink().lock();
    if (!primaryLink) {
        return QString();
    }

    SharedLinkConfigurationPtr config = primaryLink->linkConfiguration();
    if (!config) {
        return QString();
    }

    QString ipAddress;

    // Intentar obtener IP según el tipo de enlace
    if (config->type() == LinkConfiguration::TypeUdp) {
        const UDPLink* udpLink = qobject_cast<const UDPLink*>(primaryLink.get());
        if (udpLink) {
            // Buscar la IP de origen del vehículo por su sysid MAVLink
            const QHostAddress sysidAddr = udpLink->sourceAddressForSysid(static_cast<uint8_t>(vehicle->id()));
            if (!sysidAddr.isNull()) {
                ipAddress = sysidAddr.toString();
            } else {
                // Fallback: intentar target hosts configurados estáticamente
                const UDPConfiguration* udpConfig = qobject_cast<const UDPConfiguration*>(config.get());
                if (udpConfig) {
                    const QList<std::shared_ptr<UDPClient>> targets = udpConfig->targetHosts();
                    if (!targets.isEmpty()) {
                        ipAddress = targets.first()->address.toString();
                    }
                }
            }
        }
    } else if (config->type() == LinkConfiguration::TypeTcp) {
        const TCPConfiguration* tcpConfig = qobject_cast<const TCPConfiguration*>(config.get());
        if (tcpConfig) {
            ipAddress = tcpConfig->host();
        }
    }

    return ipAddress;
}
