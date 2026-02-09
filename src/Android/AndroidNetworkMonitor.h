#pragma once

#include <jni.h>

namespace AndroidNetworkMonitor
{
    jclass getNetworkMonitorClass();
    void setNativeMethods();
    
    void jniNetworkAvailable(JNIEnv *env, jobject obj, jstring networkType);
    void jniNetworkLost(JNIEnv *env, jobject obj, jstring networkType);
    void jniNetworkChanged(JNIEnv *env, jobject obj, jstring networkType);
}
