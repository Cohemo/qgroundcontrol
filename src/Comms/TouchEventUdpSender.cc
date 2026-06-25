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
#include "SettingsManager.h"
#include "FlyViewSettings.h"
#include "Fact.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

TouchEventUdpSender::TouchEventUdpSender(QObject* parent)
    : QObject(parent)
    , _udpSocket(new QUdpSocket(this))
    , _targetAddress("10.0.3.0")  // IP por defecto, se actualizará con el vehículo activo
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
    }

    // Timer de reintento: re-resuelve la IP destino periódicamente (solo activo
    // en modo derivación). Cubre el caso en que el mapa sysid->IP todavía no está
    // poblado cuando el vehículo pasa a activo (los primeros heartbeats aún no han
    // llegado), y también re-resuelve tras una reconexión o un cambio de IP.
    _resolveTimer = new QTimer(this);
    _resolveTimer->setInterval(kResolveRetryIntervalMs);
    connect(_resolveTimer, &QTimer::timeout, this, &TouchEventUdpSender::_tryResolveTarget);

    // Reaccionar a cambios de la IP configurada en los ajustes
    SettingsManager* settingsMgr = SettingsManager::instance();
    if (settingsMgr && settingsMgr->flyViewSettings()) {
        connect(settingsMgr->flyViewSettings()->onboardComputerTouchAddress(), &Fact::rawValueChanged,
                this, &TouchEventUdpSender::_onConfiguredAddressChanged);
    }

    // Estado inicial: IP fija si está configurada en ajustes, si no derivar del vehículo
    _applyConfiguredAddress();
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
    // Un target explícito tiene prioridad: fija la IP y desactiva la derivación
    // del vehículo para que el timer de reintento no la sobreescriba.
    _fixedAddressMode = true;
    if (_resolveTimer) {
        _resolveTimer->stop();
    }

    const QString old = _targetAddress.toString();
    _targetAddress = QHostAddress(address);
    _targetPort = port;
    if (_targetAddress.toString() != old) emit targetAddressChanged();
    _setTargetResolved(true);
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

    // Si la IP del vehículo todavía no se ha resuelto, intentar resolverla ahora
    // mismo (p.ej. el primer toque justo tras conectar, antes de que salte el
    // timer de reintento) para no enviar a la IP por defecto.
    if (!_targetResolved) {
        _tryResolveTarget();
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
    // En modo IP fija el vehículo no influye en la dirección destino
    if (_fixedAddressMode) {
        return;
    }
    _updateTargetFromVehicle(vehicle);
}

void TouchEventUdpSender::_onConfiguredAddressChanged()
{
    _applyConfiguredAddress();
}

void TouchEventUdpSender::_applyConfiguredAddress()
{
    SettingsManager* settingsMgr = SettingsManager::instance();
    QString configured;
    if (settingsMgr && settingsMgr->flyViewSettings()) {
        configured = settingsMgr->flyViewSettings()->onboardComputerTouchAddress()->rawValue().toString().trimmed();
    }

    const QHostAddress configuredAddr(configured);
    if (!configured.isEmpty() && !configuredAddr.isNull()) {
        // IP fija configurada: se usa siempre, ignorando la derivación del vehículo
        _fixedAddressMode = true;
        if (_resolveTimer) {
            _resolveTimer->stop();
        }

        const QString old = _targetAddress.toString();
        _targetAddress = configuredAddr;
        if (_targetAddress.toString() != old) {
            emit targetAddressChanged();
        }
        _setTargetResolved(true);

        qDebug() << "TouchEventUdpSender: Using configured fixed target IP:" << configured;
    } else {
        // Campo vacío o inválido: derivar la IP del vehículo activo (con reintento)
        _fixedAddressMode = false;
        if (_resolveTimer && !_resolveTimer->isActive()) {
            _resolveTimer->start();
        }
        _tryResolveTarget();
    }
}

void TouchEventUdpSender::_updateTargetFromVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        // Sin vehículo activo, mantener IP por defecto
        _setTargetResolved(false);
        return;
    }

    // Obtener la IP del enlace del vehículo (PX4)
    const QString vehicleLinkIp = _getVehicleLinkIp(vehicle);
    if (vehicleLinkIp.isEmpty()) {
        // Solo logueamos en la transición a no-resuelto para evitar spam del timer
        if (_targetResolved) {
            qWarning() << "TouchEventUdpSender: Could not get link IP for vehicle:" << vehicle->id();
        }
        _setTargetResolved(false);
        return;
    }

    // Extraer subnet (primeros 3 octetos) de la IP del vehículo
    const QStringList ipParts = vehicleLinkIp.split('.');
    if (ipParts.size() < 3) {
        if (_targetResolved) {
            qWarning() << "TouchEventUdpSender: Invalid IP format:" << vehicleLinkIp;
        }
        _setTargetResolved(false);
        return;
    }

    // Construir IP del ordenador de abordo: 192.168.X.163
    const QString onboardIp = QString("%1.%2.%3.%4")
                                  .arg(ipParts[0])
                                  .arg(ipParts[1])
                                  .arg(ipParts[2])
                                  .arg(kOnboardComputerLastOctet);

    // Actualizar la dirección destino
    const QString old = _targetAddress.toString();
    const bool changed = (onboardIp != old);
    _targetAddress = QHostAddress(onboardIp);
    if (changed) emit targetAddressChanged();
    const bool wasResolved = _targetResolved;
    _setTargetResolved(true);

    // Solo logueamos cuando algo cambia, para no inundar el log con el timer
    if (changed || !wasResolved) {
        qDebug() << "TouchEventUdpSender: Target updated for vehicle" << vehicle->id()
                 << "-> IP:" << onboardIp << "Port:" << _targetPort;
    }
}

void TouchEventUdpSender::_setTargetResolved(bool resolved)
{
    if (_targetResolved != resolved) {
        _targetResolved = resolved;
        emit targetResolvedChanged();
    }
}

void TouchEventUdpSender::_tryResolveTarget()
{
    // En modo IP fija no se deriva nada del vehículo
    if (_fixedAddressMode) {
        return;
    }

    MultiVehicleManager* multiVehicleMgr = MultiVehicleManager::instance();
    if (!multiVehicleMgr) {
        return;
    }

    // _updateTargetFromVehicle es idempotente: solo emite señales si algo cambia,
    // así que es seguro llamarlo periódicamente. Con vehículo nulo marca no resuelto.
    _updateTargetFromVehicle(multiVehicleMgr->activeVehicle());
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
