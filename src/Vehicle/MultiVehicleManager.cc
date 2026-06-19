/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "MultiVehicleManager.h"
#include "MAVLinkProtocol.h"
#include "QGCApplication.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
#include "MavlinkSettings.h"
#include "FirmwareUpgradeSettings.h"
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "LinkManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "LinkInterface.h"
#include "QmlObjectListModel.h"
#include "UDPLink.h"
#include "TCPLink.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#ifdef Q_OS_IOS
#include "MobileScreenMgr.h"
#elif defined(Q_OS_ANDROID)
#include "AndroidInterface.h"
#endif
#include "QGCLoggingCategory.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QTimer>

QGC_LOGGING_CATEGORY(MultiVehicleManagerLog, "Vehicle.MultiVehicleManager")

Q_APPLICATION_STATIC(MultiVehicleManager, _multiVehicleManagerInstance);

MultiVehicleManager::MultiVehicleManager(QObject *parent)
    : QObject(parent)
    , _gcsHeartbeatTimer(new QTimer(this))
    , _vehicles(new QmlObjectListModel(this))
    , _selectedVehicles(new QmlObjectListModel(this))
    , _networkManager(new QNetworkAccessManager(this))
{
    qCDebug(MultiVehicleManagerLog) << this;

    (void) qRegisterMetaType<Vehicle::MavCmdResultFailureCode_t>("MavCmdResultFailureCode_t");
}

MultiVehicleManager::~MultiVehicleManager()
{
    qCDebug(MultiVehicleManagerLog) << this;
}

MultiVehicleManager *MultiVehicleManager::instance()
{
    return _multiVehicleManagerInstance();
}

void MultiVehicleManager::init()
{
    if (_initialized) {
        return;
    }

    _offlineEditingVehicle = new Vehicle(Vehicle::MAV_AUTOPILOT_TRACK, Vehicle::MAV_TYPE_TRACK, this);

    (void) connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::vehicleHeartbeatInfo, this, &MultiVehicleManager::_vehicleHeartbeatInfo);

    _gcsHeartbeatTimer->setInterval(kGCSHeartbeatRateMSecs);
    _gcsHeartbeatTimer->setSingleShot(false);
    (void) connect(_gcsHeartbeatTimer, &QTimer::timeout, this, &MultiVehicleManager::_sendGCSHeartbeat);
    _gcsHeartbeatTimer->start();

    _initialized = true;
}

void MultiVehicleManager::_vehicleHeartbeatInfo(LinkInterface* link, int vehicleId, int componentId, int vehicleFirmwareType, int vehicleType)
{
    if (componentId != MAV_COMP_ID_AUTOPILOT1) {
        // Don't create vehicles for components other than the autopilot
        qCDebug(MultiVehicleManagerLog) << "Ignoring heartbeat from unknown component port:vehicleId:componentId:fwType:vehicleType"
                                        << link->linkConfiguration()->name()
                                        << vehicleId
                                        << componentId
                                        << vehicleFirmwareType
                                        << vehicleType;
        return;
    }

#ifndef QGC_NO_ARDUPILOT_DIALECT
    // When you flash a new ArduCopter it does not set a FRAME_CLASS for some reason. This is the only ArduPilot variant which
    // works this way. Because of this the vehicle type is not known at first connection. In order to make QGC work reasonably
    // we assume ArduCopter for this case.
    if ((vehicleType == MAV_TYPE_GENERIC) && (vehicleFirmwareType == MAV_AUTOPILOT_ARDUPILOTMEGA)) {
        vehicleType = MAV_TYPE_QUADROTOR;
    }
#endif

    switch (vehicleType) {
    case MAV_TYPE_GCS:
    case MAV_TYPE_ONBOARD_CONTROLLER:
    case MAV_TYPE_GIMBAL:
    case MAV_TYPE_ADSB:
        // These are not vehicles, so don't create a vehicle for them
        return;
    default:
        break;
    }

    if ((_vehicles->count() > 0) && !QGCCorePlugin::instance()->options()->multiVehicleEnabled()) {
        return;
    }

    if (_ignoreVehicleIds.contains(vehicleId) || getVehicleById(vehicleId) || (vehicleId == 0)) {
        return;
    }

    qCDebug(MultiVehicleManagerLog) << "Adding new vehicle link:vehicleId:componentId:vehicleFirmwareType:vehicleType "
                                    << link->linkConfiguration()->name()
                                    << vehicleId
                                    << componentId
                                    << vehicleFirmwareType
                                    << vehicleType;

    if (vehicleId == MAVLinkProtocol::instance()->getSystemId()) {
        qgcApp()->showAppMessage(tr("Warning: A vehicle is using the same system id as %1: %2").arg(QCoreApplication::applicationName()).arg(vehicleId));
    }

    Vehicle *const vehicle = new Vehicle(link, vehicleId, componentId, (MAV_AUTOPILOT)vehicleFirmwareType, (MAV_TYPE)vehicleType, this);
    (void) connect(vehicle, &Vehicle::requestProtocolVersion, this, &MultiVehicleManager::_requestProtocolVersion);
    (void) connect(vehicle->vehicleLinkManager(), &VehicleLinkManager::allLinksRemoved, this, &MultiVehicleManager::_deleteVehiclePhase1);
    (void) connect(vehicle->parameterManager(), &ParameterManager::parametersReadyChanged, this, &MultiVehicleManager::_vehicleParametersReadyChanged);

    _vehicles->append(vehicle);

    // Send QGC heartbeat ASAP, this allows PX4 to start accepting commands
    _sendGCSHeartbeat();

    SettingsManager::instance()->firmwareUpgradeSettings()->defaultFirmwareType()->setRawValue(vehicleFirmwareType);

    emit vehicleAdded(vehicle);

    if (_vehicles->count() > 1) {
        qgcApp()->showAppMessage(tr("Connected to Vehicle %1").arg(vehicleId));
    } else {
        setActiveVehicle(vehicle);
    }

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (_vehicles->count() == 1) {
        qCDebug(MultiVehicleManagerLog) << "keepScreenOn";
        #if defined(Q_OS_ANDROID)
            AndroidInterface::setKeepScreenOn(true);
        #elif defined(Q_OS_IOS)
            MobileScreenMgr::setKeepScreenOn(true);
        #endif
    }
#endif
}

void MultiVehicleManager::_requestProtocolVersion(unsigned version) const
{
    if (_vehicles->count() == 0) {
        MAVLinkProtocol::instance()->setVersion(version);
        return;
    }

    unsigned maxversion = 0;
    for (int i = 0; i < _vehicles->count(); i++) {
        const Vehicle *const vehicle = qobject_cast<const Vehicle*>(_vehicles->get(i));
        if (vehicle && (vehicle->maxProtoVersion() > maxversion)) {
            maxversion = vehicle->maxProtoVersion();
        }
    }

    if (MAVLinkProtocol::instance()->getCurrentVersion() != maxversion) {
        MAVLinkProtocol::instance()->setVersion(maxversion);
    }
}

void MultiVehicleManager::_deleteVehiclePhase1(Vehicle *vehicle)
{
    qCDebug(MultiVehicleManagerLog) << Q_FUNC_INFO << vehicle;

    bool found = false;
    for (int i = 0; i < _vehicles->count(); i++) {
        if (_vehicles->get(i) == vehicle) {
            (void) _vehicles->removeAt(i);
            found = true;
            break;
        }
    }

    if (!found) {
        qCWarning(MultiVehicleManagerLog) << "Vehicle not found in map!";
        return;
    }

    deselectVehicle(vehicle->id());

    _setActiveVehicleAvailable(false);
    _setParameterReadyVehicleAvailable(false);
    emit vehicleRemoved(vehicle);

#if defined(Q_OS_ANDROID) || defined (Q_OS_IOS)
    if (_vehicles->count() == 0) {
        qCDebug(MultiVehicleManagerLog) << "restoreScreenOn";
        #if defined(Q_OS_ANDROID)
            AndroidInterface::setKeepScreenOn(false);
        #elif defined(Q_OS_IOS)
            MobileScreenMgr::setKeepScreenOn(false);
        #endif
    }
#endif

    // We must let the above signals flow through the system as well as get back to the main loop event queue
    // before we can actually delete the Vehicle. The reason is that Qml may be holding on to references to it.
    // Even though the above signals should unload any Qml which has references, that Qml will not be destroyed
    // until we get back to the main loop. So we set a short timer which will then fire after Qt has finished
    // doing all of its internal nastiness to clean up the Qml. This works for both the normal running case
    // as well as the unit testing case which of course has a different signal flow!
    QTimer::singleShot(20, this, [this, vehicle]() {
        _deleteVehiclePhase2(vehicle);
    });
}

void MultiVehicleManager::_deleteVehiclePhase2(Vehicle *vehicle)
{
    qCDebug(MultiVehicleManagerLog) << Q_FUNC_INFO << vehicle;

    /// Qml has been notified of vehicle about to go away and should be disconnected from it by now.
    /// This means we can now clear the active vehicle property and delete the Vehicle for real.

    Vehicle *newActiveVehicle = nullptr;
    if (_vehicles->count() > 0) {
        newActiveVehicle = qobject_cast<Vehicle*>(_vehicles->get(0));
    }

    _setActiveVehicle(newActiveVehicle);

    if (_activeVehicle) {
        _setActiveVehicleAvailable(true);
        if (_activeVehicle->parameterManager()->parametersReady()) {
            _setParameterReadyVehicleAvailable(true);
        }
    }

    vehicle->deleteLater();
}

void MultiVehicleManager::setActiveVehicle(Vehicle *vehicle)
{
    qCDebug(MultiVehicleManagerLog) << Q_FUNC_INFO << vehicle;

    if (vehicle != _activeVehicle) {
        if (_activeVehicle) {
            // The sequence of signals is very important in order to not leave Qml elements connected
            // to a non-existent vehicle.

            // First we must signal that there is no active vehicle available. This will disconnect
            // any existing ui from the currently active vehicle.
            _setActiveVehicleAvailable(false);
            _setParameterReadyVehicleAvailable(false);
        }

        QTimer::singleShot(20, this, [this, vehicle]() {
            _setActiveVehiclePhase2(vehicle);
        });
    }
}

void MultiVehicleManager::_setActiveVehiclePhase2(Vehicle *vehicle)
{
    qCDebug(MultiVehicleManagerLog) << Q_FUNC_INFO << vehicle;

    _setActiveVehicle(vehicle);

    if (_activeVehicle) {
        _setActiveVehicleAvailable(true);

        if (_activeVehicle->parameterManager()->parametersReady()) {
            _setParameterReadyVehicleAvailable(true);
        }
    }
}

void MultiVehicleManager::_vehicleParametersReadyChanged(bool parametersReady)
{
    ParameterManager *const paramMgr = qobject_cast<ParameterManager*>(sender());
    if (!paramMgr) {
        return;
    }

    if (paramMgr->vehicle() == _activeVehicle) {
        _setParameterReadyVehicleAvailable(parametersReady);
    }
}

void MultiVehicleManager::_sendGCSHeartbeat()
{
    if (!SettingsManager::instance()->mavlinkSettings()->sendGCSHeartbeat()->rawValue().toBool()) {
        return;
    }

    const QList<SharedLinkInterfacePtr> sharedLinks = LinkManager::instance()->links();
    for (const SharedLinkInterfacePtr link: sharedLinks) {
        if (!link->isConnected()) {
            continue;
        }

        const SharedLinkConfigurationPtr linkConfiguration = link->linkConfiguration();
        if (linkConfiguration->isHighLatency()) {
            continue;
        }

        mavlink_message_t message{};
        (void) mavlink_msg_heartbeat_pack_chan(
            MAVLinkProtocol::instance()->getSystemId(),
            MAVLinkProtocol::instance()->getComponentId(),
            link->mavlinkChannel(),
            &message,
            MAV_TYPE_GCS,
            MAV_AUTOPILOT_INVALID,
            MAV_MODE_MANUAL_ARMED,
            0,
            MAV_STATE_ACTIVE
        );

        uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
        const uint16_t len = mavlink_msg_to_send_buffer(buffer, &message);
        (void) link->writeBytesThreadSafe(reinterpret_cast<const char*>(buffer), len);
    }
}

void MultiVehicleManager::selectVehicle(int vehicleId)
{
    if(!_vehicleSelected(vehicleId)) {
        Vehicle *const vehicle = getVehicleById(vehicleId);
        _selectedVehicles->append(vehicle);
        return;
    }
}

void MultiVehicleManager::deselectVehicle(int vehicleId)
{
    for (int i = 0; i < _selectedVehicles->count(); i++) {
        Vehicle *const vehicle = qobject_cast<Vehicle*>(_selectedVehicles->get(i));
        if (vehicle->id() == vehicleId) {
            _selectedVehicles->removeAt(i);
            return;
        }
    }
}

void MultiVehicleManager::deselectAllVehicles()
{
    _selectedVehicles->clear();
}

bool MultiVehicleManager::_vehicleSelected(int vehicleId)
{
    for (int i = 0; i < _selectedVehicles->count(); i++) {
        Vehicle *const vehicle = qobject_cast<Vehicle*>(_selectedVehicles->get(i));
        if (vehicle->id() == vehicleId) {
            return true;
        }
    }
    return false;
}

Vehicle *MultiVehicleManager::getVehicleById(int vehicleId) const
{
    for (int i = 0; i < _vehicles->count(); i++) {
        Vehicle *const vehicle = qobject_cast<Vehicle*>(_vehicles->get(i));
        if (vehicle->id() == vehicleId) {
            return vehicle;
        }
    }

    return nullptr;
}

void MultiVehicleManager::_setActiveVehicle(Vehicle *vehicle)
{
    if (vehicle != _activeVehicle) {
        // Stop stream on previous vehicle if any
        if (!_previousOnboardIp.isEmpty()) {
            qCDebug(MultiVehicleManagerLog) << "Stopping stream on previous vehicle IP:" << _previousOnboardIp;
            _sendStreamCommand(_previousOnboardIp, "stop");
            _previousOnboardIp.clear();
        }

        _activeVehicle = vehicle;

        // Start stream on new vehicle if any
        if (_activeVehicle) {
            _previousActiveVehicleId = _activeVehicle->id();
            const QString newOnboardIp = _getVehicleOnboardIp(_activeVehicle);
            qCDebug(MultiVehicleManagerLog) << "Starting stream on new vehicle:" << _previousActiveVehicleId << "IP:" << newOnboardIp;

            if (!newOnboardIp.isEmpty()) {
                _previousOnboardIp = newOnboardIp;
                // Delay para asegurar que VehicleLinkManager esté inicializado
                QTimer::singleShot(500, this, [this, onboardIp = newOnboardIp]() {
                    _sendStreamCommand(onboardIp, "start");
                });
            }
            // Exponer la IP del ordenador de abordo a QML (p.ej. para los backups)
            _setActiveVehicleOnboardIp(newOnboardIp);
        } else {
            _previousActiveVehicleId = -1;
            _setActiveVehicleOnboardIp(QString());
        }

        emit activeVehicleChanged(vehicle);
    }
}

void MultiVehicleManager::_setActiveVehicleOnboardIp(const QString &ip)
{
    if (_activeVehicleOnboardIp != ip) {
        _activeVehicleOnboardIp = ip;
        emit activeVehicleOnboardIpChanged();
    }
}

void MultiVehicleManager::_sendStreamCommand(const QString &onboardIp, const QString &command)
{
    if (onboardIp.isEmpty()) {
        qCWarning(MultiVehicleManagerLog) << "Cannot send stream command: empty onboard IP";
        return;
    }

    // Build HTTP URL: http://192.168.X.163:8000/stream
    const QString url = QString("http://%1:8000/stream").arg(onboardIp);

    qCDebug(MultiVehicleManagerLog) << "Sending stream command to:" << url;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // JSON body for POST request
    const QString postDataStr = QString("{ \"mensaje\":\"%1\"}").arg(command);
    const QByteArray postData = postDataStr.toUtf8();

    QNetworkReply *reply = _networkManager->post(request, postData);

    // Handle response asynchronously
    connect(reply, &QNetworkReply::finished, this, [reply, onboardIp, command, url]() {
        if (reply->error() == QNetworkReply::NoError) {
            qCDebug(MultiVehicleManagerLog) << "Stream command successful for IP:" << onboardIp
                                            << "command:" << command;
        } else {
            qCWarning(MultiVehicleManagerLog) << "Failed to send stream command to:" << url
                                              << "error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

QString MultiVehicleManager::_getVehicleOnboardIp(Vehicle *vehicle) const
{
    const QString vehicleLinkIp = _getVehicleLinkIp(vehicle);
    if (vehicleLinkIp.isEmpty()) {
        return QString();
    }

    const QStringList ipParts = vehicleLinkIp.split('.');
    if (ipParts.size() < 4) {
        return QString();
    }

    return QString("%1.%2.%3.%4")
        .arg(ipParts[0])
        .arg(ipParts[1])
        .arg(ipParts[2])
        .arg(kOnboardComputerLastOctet);
}

QString MultiVehicleManager::_getVehicleLinkIp(Vehicle *vehicle) const
{
    if (!vehicle) {
        return QString();
    }

    VehicleLinkManager *linkManager = vehicle->vehicleLinkManager();
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

    // Try to get IP based on link type
    if (config->type() == LinkConfiguration::TypeUdp) {
        const UDPLink *udpLink = qobject_cast<const UDPLink*>(primaryLink.get());
        if (udpLink) {
            // Buscar la IP de origen del vehículo por su sysid MAVLink
            const QHostAddress sysidAddr = udpLink->sourceAddressForSysid(static_cast<uint8_t>(vehicle->id()));
            if (!sysidAddr.isNull()) {
                ipAddress = sysidAddr.toString();
            } else {
                // Fallback: intentar target hosts configurados estáticamente
                const UDPConfiguration *udpConfig = qobject_cast<const UDPConfiguration*>(config.get());
                if (udpConfig) {
                    const QList<std::shared_ptr<UDPClient>> targets = udpConfig->targetHosts();
                    if (!targets.isEmpty()) {
                        ipAddress = targets.first()->address.toString();
                    }
                }
            }
        }
    } else if (config->type() == LinkConfiguration::TypeTcp) {
        const TCPConfiguration *tcpConfig = qobject_cast<const TCPConfiguration*>(config.get());
        if (tcpConfig) {
            ipAddress = tcpConfig->host();
        }
    }

    return ipAddress;
}

void MultiVehicleManager::_setActiveVehicleAvailable(bool activeVehicleAvailable)
{
    if (activeVehicleAvailable != _activeVehicleAvailable) {
        _activeVehicleAvailable = activeVehicleAvailable;
        emit activeVehicleAvailableChanged(activeVehicleAvailable);
    }
}

void MultiVehicleManager::_setParameterReadyVehicleAvailable(bool parametersReady)
{
    if (parametersReady != _parameterReadyVehicleAvailable) {
        _parameterReadyVehicleAvailable = parametersReady;
        parameterReadyVehicleAvailableChanged(parametersReady);
    }
}
