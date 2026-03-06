#include "AndroidNetworkMonitor.h"
#include "LinkManager.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QJniEnvironment>

QGC_LOGGING_CATEGORY(AndroidNetworkMonitorLog, "qgc.android.androidnetworkmonitor");

namespace AndroidNetworkMonitor
{

jclass getNetworkMonitorClass()
{
    static jclass javaClass = nullptr;

    if (!javaClass) {
        QJniEnvironment env;
        if (!env.isValid()) {
            qCWarning(AndroidNetworkMonitorLog) << "Invalid QJniEnvironment";
            return nullptr;
        }

        const char* className = "org/mavlink/qgroundcontrol/QGCNetworkMonitor";
        if (!QJniObject::isClassAvailable(className)) {
            qCWarning(AndroidNetworkMonitorLog) << "Class Not Available:" << className;
            return nullptr;
        }

        javaClass = env.findClass(className);
        if (!javaClass) {
            qCWarning(AndroidNetworkMonitorLog) << "Class Not Found:" << className;
            return nullptr;
        }

        (void) env.checkAndClearExceptions();
    }

    return javaClass;
}

void setNativeMethods()
{
    qCDebug(AndroidNetworkMonitorLog) << "Registering Native Functions";

    const JNINativeMethod javaMethods[] {
        {"nativeNetworkAvailable", "(Ljava/lang/String;)V", reinterpret_cast<void*>(jniNetworkAvailable)},
        {"nativeNetworkLost", "(Ljava/lang/String;)V", reinterpret_cast<void*>(jniNetworkLost)},
        {"nativeNetworkChanged", "(Ljava/lang/String;)V", reinterpret_cast<void*>(jniNetworkChanged)},
    };

    QJniEnvironment jniEnv;
    const jclass javaClass = getNetworkMonitorClass();
    if (!javaClass) {
        qCWarning(AndroidNetworkMonitorLog) << "Couldn't find class for RegisterNatives";
        (void) jniEnv.checkAndClearExceptions();
        return;
    }

    const jint regResult = jniEnv->RegisterNatives(javaClass, javaMethods, std::size(javaMethods));
    if (regResult != JNI_OK) {
        qCWarning(AndroidNetworkMonitorLog) << "Error registering native methods:" << regResult;
        (void) jniEnv.checkAndClearExceptions();
        return;
    }

    qCDebug(AndroidNetworkMonitorLog) << "Native Functions Registered Successfully";
    (void) jniEnv.checkAndClearExceptions();
}

void jniNetworkAvailable(JNIEnv *env, jobject obj, jstring networkType)
{
    Q_UNUSED(obj);

    if (!networkType) {
        qCWarning(AndroidNetworkMonitorLog) << "nativeNetworkAvailable called with null networkType";
        return;
    }

    const char *typeStr = env->GetStringUTFChars(networkType, nullptr);
    if (!typeStr) {
        qCWarning(AndroidNetworkMonitorLog) << "Failed to get UTF chars from jstring";
        return;
    }

    const QString type = QString::fromUtf8(typeStr);
    env->ReleaseStringUTFChars(networkType, typeStr);

    qCDebug(AndroidNetworkMonitorLog) << "Network available:" << type;
    
    if (type == "ethernet") {
        LinkManager::instance()->startAutoConnectedLinks();
    }

    if (QJniEnvironment::checkAndClearExceptions(env)) {
        qCWarning(AndroidNetworkMonitorLog) << "Exception occurred in nativeNetworkAvailable";
    }
}

void jniNetworkLost(JNIEnv *env, jobject obj, jstring networkType)
{
    Q_UNUSED(obj);

    if (!networkType) {
        qCWarning(AndroidNetworkMonitorLog) << "nativeNetworkLost called with null networkType";
        return;
    }

    const char *typeStr = env->GetStringUTFChars(networkType, nullptr);
    if (!typeStr) {
        qCWarning(AndroidNetworkMonitorLog) << "Failed to get UTF chars from jstring";
        return;
    }

    const QString type = QString::fromUtf8(typeStr);
    env->ReleaseStringUTFChars(networkType, typeStr);

    qCDebug(AndroidNetworkMonitorLog) << "Network lost:" << type;

    if (QJniEnvironment::checkAndClearExceptions(env)) {
        qCWarning(AndroidNetworkMonitorLog) << "Exception occurred in nativeNetworkLost";
    }
}

void jniNetworkChanged(JNIEnv *env, jobject obj, jstring networkType)
{
    Q_UNUSED(obj);

    if (!networkType) {
        qCWarning(AndroidNetworkMonitorLog) << "nativeNetworkChanged called with null networkType";
        return;
    }

    const char *typeStr = env->GetStringUTFChars(networkType, nullptr);
    if (!typeStr) {
        qCWarning(AndroidNetworkMonitorLog) << "Failed to get UTF chars from jstring";
        return;
    }

    const QString type = QString::fromUtf8(typeStr);
    env->ReleaseStringUTFChars(networkType, typeStr);

    qCDebug(AndroidNetworkMonitorLog) << "Network changed:" << type;

    if (QJniEnvironment::checkAndClearExceptions(env)) {
        qCWarning(AndroidNetworkMonitorLog) << "Exception occurred in nativeNetworkChanged";
    }
}

} // namespace AndroidNetworkMonitor
