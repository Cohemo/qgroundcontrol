package org.mavlink.qgroundcontrol;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.util.Log;

public class QGCNetworkMonitor {
    private static final String TAG = QGCNetworkMonitor.class.getSimpleName();
    
    private static ConnectivityManager connectivityManager;
    private static NetworkCallback networkCallback;
    
    private static native void nativeNetworkAvailable(String networkType);
    private static native void nativeNetworkLost(String networkType);
    private static native void nativeNetworkChanged(String networkType);
    
    public static void initialize(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            QGCLogger.w(TAG, "Network monitoring requires Android 5.0+");
            return;
        }
        
        connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) {
            QGCLogger.e(TAG, "Failed to get ConnectivityManager");
            return;
        }
        
        networkCallback = new NetworkCallback();
        
        NetworkRequest.Builder builder = new NetworkRequest.Builder();
        builder.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
        builder.addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET);
        builder.addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
        
        NetworkRequest request = builder.build();
        
        try {
            connectivityManager.registerNetworkCallback(request, networkCallback);
            QGCLogger.i(TAG, "Network monitoring initialized");
        } catch (Exception e) {
            QGCLogger.e(TAG, "Failed to register network callback", e);
        }
    }
    
    public static void cleanup() {
        if (connectivityManager != null && networkCallback != null) {
            try {
                connectivityManager.unregisterNetworkCallback(networkCallback);
                QGCLogger.i(TAG, "Network monitoring stopped");
            } catch (Exception e) {
                QGCLogger.w(TAG, "Failed to unregister network callback: " + e.getMessage());
            }
        }
    }
    
    private static class NetworkCallback extends ConnectivityManager.NetworkCallback {
        @Override
        public void onAvailable(Network network) {
            String networkType = getNetworkType(network);
            QGCLogger.i(TAG, "Network available: " + networkType);
            try {
                nativeNetworkAvailable(networkType);
            } catch (Exception e) {
                QGCLogger.e(TAG, "Error calling nativeNetworkAvailable", e);
            }
        }
        
        @Override
        public void onLost(Network network) {
            String networkType = getNetworkType(network);
            QGCLogger.i(TAG, "Network lost: " + networkType);
            try {
                nativeNetworkLost(networkType);
            } catch (Exception e) {
                QGCLogger.e(TAG, "Error calling nativeNetworkLost", e);
            }
        }
        
        @Override
        public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) {
            String networkType = getNetworkType(network);
            QGCLogger.d(TAG, "Network capabilities changed: " + networkType);
            try {
                nativeNetworkChanged(networkType);
            } catch (Exception e) {
                QGCLogger.e(TAG, "Error calling nativeNetworkChanged", e);
            }
        }
        
        private String getNetworkType(Network network) {
            if (connectivityManager == null) {
                return "unknown";
            }
            
            try {
                NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
                if (capabilities == null) {
                    return "unknown";
                }
                
                if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                    return "ethernet";
                } else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                    return "wifi";
                } else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                    return "cellular";
                }
            } catch (Exception e) {
                QGCLogger.w(TAG, "Failed to get network type: " + e.getMessage());
            }
            
            return "unknown";
        }
    }
}
