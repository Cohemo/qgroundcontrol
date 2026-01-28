## 🔧 CONFIGURACIÓN IP UDP - Guía Rápida

### ⚠️ IMPORTANTE
La IP de destino se configura en el **código fuente C++**, no en el script de compilación.

---

## 📍 DÓNDE CONFIGURAR LA IP

**Archivo**: `src/Comms/TouchEventUdpSender.cc`
**Línea**: 18

```cpp
TouchEventUdpSender::TouchEventUdpSender(QObject* parent)
    : QObject(parent)
    , _udpSocket(new QUdpSocket(this))
    , _targetAddress("192.168.168.163")  // ⚠️ CAMBIAR AQUÍ
    , _targetPort(8000)                 // Puerto UDP
```

---

## 🚀 PASOS PARA COMPILAR CON IP PERSONALIZADA

### 1️⃣ Encontrar tu IP
```bash
# En Linux
hostname -I

# O más específico
ip addr show | grep "inet " | grep -v "127.0.0.1"
```

Ejemplo de salida: `192.168.1.100`

### 2️⃣ Editar el archivo C++
```bash
nano src/Comms/TouchEventUdpSender.cc
```

Cambiar línea 18:
```cpp
, _targetAddress("192.168.1.100")  // Tu IP aquí
```

Guardar: `Ctrl+O`, `Enter`, `Ctrl+X`

### 3️⃣ Compilar
```bash
./build_android.sh
```

### 4️⃣ Probar
```bash
# En la computadora
python3 test_touch_udp_android.py

# Instalar APK en Android
adb install QGroundControl.apk

# Ejecutar y tocar pantalla
```

---

## ✅ VERIFICACIÓN

El script `build_android.sh` te recordará al inicio:
```
⚠ RECORDATORIO: Verifica la IP en src/Comms/TouchEventUdpSender.cc
   Línea 18: _targetAddress("TU_IP")
```

---

## 📝 CONFIGURACIÓN ACTUAL

- **IP**: `192.168.168.163`
- **Puerto**: `8000`
- **Throttling**: 50ms (20 eventos/seg)

---

## 🔄 SI NECESITAS CAMBIAR LA IP DESPUÉS

1. Editar `src/Comms/TouchEventUdpSender.cc`
2. Recompilar con `./build_android.sh`
3. Reinstalar APK

**NO** es necesario limpiar el build, CMake detectará el cambio.

---

## 💡 ALTERNATIVA: Configuración dinámica desde QML

Si prefieres no recompilar cada vez, puedes configurar desde QML:

**Archivo**: `src/FlightDisplay/FlyViewVideo.qml`

```qml
TouchEventUdpSender {
    id: touchEventSender
    Component.onCompleted: {
        // Sobrescribir IP del código C++
        touchEventSender.setTarget("192.168.1.100", 8000)
    }
}
```

Pero la forma **RECOMENDADA** es configurar en el archivo C++.
