/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "UgvSelector.h"
#include "LinkManager.h"
#include "LinkInterface.h"
#include "UDPLink.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

QGC_LOGGING_CATEGORY(UgvSelectorLog, "Comms.UgvSelector")

UgvSelector::UgvSelector(QObject *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
{
}

QString UgvSelector::_onboardIp(int ugv) const
{
    // La IP va adelantada un número respecto al del UGV: UGV N -> 10.0.(N+1).0
    return QStringLiteral("10.0.%1.0").arg(ugv + 1);
}

void UgvSelector::selectUgv(int ugv)
{
    if ((ugv != 1) && (ugv != 2)) {
        return;
    }
    // Idempotente: re-seleccionar el UGV ya activo no reenvía "start" (evita un
    // reinicio innecesario de la PX4, que /stream start provoca en el servidor).
    if (ugv == _selectedUgv) {
        return;
    }
    const int other = (ugv == 1) ? 2 : 1;

    qCDebug(UgvSelectorLog) << "Select UGV" << ugv << "(start" << _onboardIp(ugv) << "/ stop" << _onboardIp(other) << ")";

    // Solo un UGV activo a la vez: start al elegido, stop al otro.
    _sendStreamCommand(other, QStringLiteral("stop"));
    _sendStreamCommand(ugv, QStringLiteral("start"));

    // Link MAVLink con la PX4: desconecta el del otro y conecta el del elegido,
    // pero SOLO cuando el otro haya terminado de desconectar (su socket UDP libera
    // el puerto 8234). disconnect()/_connect() son asíncronos, así que conectar de
    // inmediato solaparía dos binds del mismo puerto durante el cambio.
    _disconnectPx4(other);
    _pendingUgv = ugv;
    _pendingRetries = 0;
    _tryConnectPending();

    if (_selectedUgv != ugv) {
        _selectedUgv = ugv;
        emit selectedUgvChanged();
    }
}

void UgvSelector::disconnectCurrent()
{
    if (_selectedUgv == 0) {
        return;
    }
    const int ugv = _selectedUgv;
    qCDebug(UgvSelectorLog) << "Disconnect UGV" << ugv;

    _sendStreamCommand(ugv, QStringLiteral("stop"));
    _disconnectPx4(ugv);
    _pendingUgv = 0;  // cancela cualquier conexión pendiente

    _selectedUgv = 0;
    emit selectedUgvChanged();
}

void UgvSelector::_tryConnectPending()
{
    if (_pendingUgv == 0) {
        return;
    }
    const int ugv = _pendingUgv;
    const int other = (ugv == 1) ? 2 : 1;

    // ¿El otro UGV sigue desconectándose? link() devuelve null en cuanto el link
    // se destruye; mientras tanto isConnected() sigue true. Reintentamos hasta que
    // libere el puerto (o agotamos el tope, por si nunca reporta la desconexión).
    LinkInterface *const otherLink = _px4Config[other] ? _px4Config[other]->link() : nullptr;
    if (otherLink && otherLink->isConnected() && (_pendingRetries < kConnectMaxRetries)) {
        ++_pendingRetries;
        QTimer::singleShot(kConnectRetryMs, this, &UgvSelector::_tryConnectPending);
        return;
    }

    _pendingUgv = 0;
    _connectPx4(ugv);
}

void UgvSelector::_sendStreamCommand(int ugv, const QString &command)
{
    const QString url = QStringLiteral("http://%1:%2/stream").arg(_onboardIp(ugv)).arg(kStreamPort);

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray postData = QStringLiteral("{\"mensaje\":\"%1\"}").arg(command).toUtf8();

    qCDebug(UgvSelectorLog) << "Stream" << command << "->" << url;

    QNetworkReply *const reply = _nam->post(request, postData);
    (void) connect(reply, &QNetworkReply::finished, this, [reply, url, command]() {
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(UgvSelectorLog) << "Stream" << command << "failed to" << url << ":" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void UgvSelector::_connectPx4(int ugv)
{
    // Ya conectado: nada que hacer.
    if (_px4Config[ugv] && _px4Config[ugv]->link() && _px4Config[ugv]->link()->isConnected()) {
        return;
    }

    if (!_px4Config[ugv]) {
        SharedLinkConfigurationPtr cfg = std::make_shared<UDPConfiguration>(QStringLiteral("UGV%1 PX4").arg(ugv));
        UDPConfiguration *const udp = qobject_cast<UDPConfiguration*>(cfg.get());
        udp->addHost(_onboardIp(ugv), kPx4Port);
        udp->setLocalPort(kPx4Port);
        _px4Config[ugv] = cfg;
    }

    qCDebug(UgvSelectorLog) << "Connecting PX4 link UGV" << ugv << "-> UDP" << _onboardIp(ugv) << kPx4Port;
    (void) LinkManager::instance()->createConnectedLink(_px4Config[ugv]);
}

void UgvSelector::_disconnectPx4(int ugv)
{
    if (_px4Config[ugv] && _px4Config[ugv]->link()) {
        qCDebug(UgvSelectorLog) << "Disconnecting PX4 link UGV" << ugv;
        _px4Config[ugv]->link()->disconnect();
    }
}
