#!/bin/bash
################################################################################
# Script para compilar QGroundControl APK para Android
# Autor: Sistema de compilación QGC
# Fecha: Octubre 2025
#
# IMPORTANTE - Configuración de IP UDP Touch Events:
#   La IP de destino se configura en el código fuente C++, NO en este script.
#   Editar ANTES de ejecutar este script:
#
#   Archivo: src/Comms/TouchEventUdpSender.cc
#   Línea 18: _targetAddress("192.168.x.x")  // <-- Cambiar aquí
#
################################################################################

set -e  # Salir si hay error

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}QGroundControl - Build APK${NC}"
echo -e "${BLUE}================================${NC}\n"

echo -e "${YELLOW}⚠ RECORDATORIO:${NC} Verifica la IP en src/Comms/TouchEventUdpSender.cc"
echo -e "${YELLOW}   Línea 18: _targetAddress(\"TU_IP\")${NC}\n"

################################################################################
# CONFIGURACIÓN - EDITAR ESTAS VARIABLES SEGÚN TU SISTEMA
################################################################################

# Qt Installation Path (cambiar según tu instalación)
# IMPORTANTE: Usar android_arm64_v8a para ARM 64-bit
export QT_ROOT="/usr/local/Qt/6.8.3"
export QT_ANDROID="$QT_ROOT/android_arm64_v8a"  # ARM64 (64-bit)
export QT_HOST="$QT_ROOT/gcc_64"

# Para compilar para otras arquitecturas, cambiar QT_ANDROID:
# - android_arm64_v8a  -> ARM 64-bit (RECOMENDADO - dispositivos modernos)
# - android_armv7      -> ARM 32-bit (dispositivos antiguos)
# - android_x86_64     -> Intel 64-bit (emuladores)
# - android_x86        -> Intel 32-bit (emuladores antiguos)

# Android SDK (cambiar si está en otra ubicación)
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"

# Android NDK (cambiar versión si es necesaria)
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/26.1.10909125"

# Java JDK (cambiar según tu sistema)
export JAVA_HOME="/opt/jdk-17.0.16+8"

# Configuración de compilación
ANDROID_ABI="arm64-v8a"  # ARM64-v8a (64-bit) - Dispositivos modernos
ANDROID_API_LEVEL="24"    # Android 7.0+ (mínimo recomendado para ARM64)
BUILD_TYPE="Release"      # Release o Debug

# Configuraciones adicionales para estabilidad
export GRADLE_OPTS="-Xmx4g -XX:MaxMetaspaceSize=512m"
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
export ANDROID_HOME="$ANDROID_SDK_ROOT"

# Nota: arm64-v8a es la arquitectura de 64 bits para ARM
# Compatible con la mayoría de dispositivos Android modernos (2015+)
# Si necesitas ARMv7 (32-bit) cambiar a: armeabi-v7a

# Directorio de trabajo
QGC_ROOT="/home/diego/Escritorio/qgroundcontrol"
BUILD_DIR="$QGC_ROOT/build-android-arm64-release"

################################################################################
# FUNCIONES AUXILIARES
################################################################################

print_header() {
    echo -e "\n${BLUE}▶ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

check_command() {
    if command -v "$1" &> /dev/null; then
        print_success "$1 encontrado"
        return 0
    else
        print_error "$1 NO encontrado"
        return 1
    fi
}

check_directory() {
    if [ -d "$1" ]; then
        print_success "Directorio encontrado: $1"
        return 0
    else
        print_error "Directorio NO encontrado: $1"
        return 1
    fi
}

################################################################################
# VERIFICACIÓN DE REQUISITOS
################################################################################

print_header "Verificando requisitos..."

# Verificar comandos necesarios
REQUIREMENTS_OK=true

if ! check_command "cmake"; then
    print_error "CMake no instalado. Instalar: sudo apt install cmake"
    REQUIREMENTS_OK=false
fi

if ! check_command "java"; then
    print_error "Java no instalado. Instalar: sudo apt install openjdk-11-jdk"
    REQUIREMENTS_OK=false
else
    JAVA_VERSION=$(java -version 2>&1 | head -n 1 | cut -d'"' -f2)
    echo -e "  Java version: $JAVA_VERSION"
fi

if ! check_command "python3"; then
    print_warning "Python3 no encontrado (opcional para scripts de prueba)"
fi

# Verificar directorios
if ! check_directory "$QT_ANDROID"; then
    print_error "Qt Android no encontrado en: $QT_ANDROID"
    print_warning "Descargar desde: https://www.qt.io/download-open-source"
    print_warning "Asegúrate de instalar: android_arm64_v8a durante la instalación de Qt"
    REQUIREMENTS_OK=false
else
    # Verificar que la arquitectura Qt coincida con ANDROID_ABI
    if [[ "$QT_ANDROID" == *"arm64"* ]] && [ "$ANDROID_ABI" = "arm64-v8a" ]; then
        print_success "Qt ARM64 y ANDROID_ABI coinciden ✓"
    elif [[ "$QT_ANDROID" == *"armv7"* ]] && [ "$ANDROID_ABI" = "armeabi-v7a" ]; then
        print_success "Qt ARMv7 y ANDROID_ABI coinciden ✓"
    else
        print_warning "Advertencia: QT_ANDROID ($QT_ANDROID) puede no coincidir con ANDROID_ABI ($ANDROID_ABI)"
    fi
fi

if ! check_directory "$ANDROID_SDK_ROOT"; then
    print_error "Android SDK no encontrado en: $ANDROID_SDK_ROOT"
    print_warning "Descargar desde: https://developer.android.com/studio"
    REQUIREMENTS_OK=false
fi

if ! check_directory "$ANDROID_NDK_ROOT"; then
    print_error "Android NDK no encontrado en: $ANDROID_NDK_ROOT"
    print_warning "Instalar desde Android Studio > SDK Manager > SDK Tools > NDK"
    REQUIREMENTS_OK=false
fi

if [ "$REQUIREMENTS_OK" = false ]; then
    print_error "Faltan requisitos. Por favor, instalar e intentar nuevamente."
    exit 1
fi

print_success "Todos los requisitos cumplidos\n"

################################################################################
# VERIFICACIONES ADICIONALES
################################################################################

print_header "Verificaciones adicionales..."

# Verificar espacio en disco (necesario al menos 10GB)
AVAILABLE_SPACE=$(df -BG . | tail -1 | awk '{print $4}' | sed 's/G//')
if [ "$AVAILABLE_SPACE" -lt 10 ]; then
    print_warning "Espacio disponible: ${AVAILABLE_SPACE}GB (recomendado: 10GB+)"
    read -p "¿Continuar de todos modos? (s/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Ss]$ ]]; then
        exit 1
    fi
else
    print_success "Espacio en disco suficiente: ${AVAILABLE_SPACE}GB"
fi

# Limpiar cache de Gradle si existe
if [ -d "$HOME/.gradle/caches" ]; then
    print_header "Limpiando cache de Gradle..."
    rm -rf "$HOME/.gradle/caches" 2>/dev/null || true
    print_success "Cache de Gradle limpiado"
fi

# Verificar memoria disponible
AVAILABLE_RAM=$(free -g | awk '/^Mem:/{print $7}')
if [ "$AVAILABLE_RAM" -lt 2 ]; then
    print_warning "RAM disponible baja: ${AVAILABLE_RAM}GB (recomendado: 4GB+)"
    export GRADLE_OPTS="-Xmx2g -XX:MaxMetaspaceSize=256m"
else
    print_success "RAM disponible: ${AVAILABLE_RAM}GB"
fi

################################################################################
# MOSTRAR CONFIGURACIÓN DE COMPILACIÓN
################################################################################

print_header "Configuración de compilación"

echo -e "${BLUE}Arquitectura:${NC} ${GREEN}$ANDROID_ABI${NC} (ARM 64-bit)"
echo -e "${BLUE}API Level:${NC} Android $ANDROID_API_LEVEL (Android 7.0+)"
echo -e "${BLUE}Build Type:${NC} $BUILD_TYPE"
echo -e "${BLUE}Qt Android:${NC} $QT_ANDROID"

if [ "$ANDROID_ABI" = "arm64-v8a" ]; then
    print_success "Compilando para ARM64-v8a (64-bit)"
    echo -e "  ${GREEN}✓${NC} Compatible con dispositivos Android modernos (2015+)"
    echo -e "  ${GREEN}✓${NC} Mejor rendimiento que ARMv7 (32-bit)"
    echo -e "  ${GREEN}✓${NC} Soporta más de 4GB RAM"
elif [ "$ANDROID_ABI" = "armeabi-v7a" ]; then
    print_warning "Compilando para ARMv7 (32-bit)"
    echo -e "  ${YELLOW}⚠${NC} Solo para dispositivos antiguos"
else
    echo -e "  Arquitectura: $ANDROID_ABI"
fi

echo ""

################################################################################
# CONFIGURAR VARIABLES DE ENTORNO
################################################################################

print_header "Configurando variables de entorno..."

export PATH="$QT_HOST/bin:$ANDROID_SDK_ROOT/platform-tools:$ANDROID_SDK_ROOT/tools:$PATH"
export CMAKE_PREFIX_PATH="$QT_ANDROID"

echo "  QT_ANDROID: $QT_ANDROID"
echo "  ANDROID_SDK_ROOT: $ANDROID_SDK_ROOT"
echo "  ANDROID_NDK_ROOT: $ANDROID_NDK_ROOT"
echo "  JAVA_HOME: $JAVA_HOME"

print_success "Variables de entorno configuradas"

################################################################################
# LIMPIAR BUILD ANTERIOR (OPCIONAL)
################################################################################

if [ -d "$BUILD_DIR" ]; then
    print_header "Directorio de compilación existente encontrado"
    read -p "¿Desea limpiar compilación anterior? (s/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Ss]$ ]]; then
        rm -rf "$BUILD_DIR"
        print_success "Directorio limpiado"
    fi
fi

################################################################################
# CREAR DIRECTORIO DE BUILD
################################################################################

print_header "Creando directorio de compilación..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

print_success "Directorio: $BUILD_DIR"

################################################################################
# CONFIGURAR CON CMAKE
################################################################################

print_header "Configurando proyecto con CMake..."

echo -e "${YELLOW}Esto puede tomar varios minutos...${NC}\n"

# Limpiar variables CMake anteriores
unset CMAKE_PREFIX_PATH

cmake "$QGC_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM="android-$ANDROID_API_LEVEL" \
    -DANDROID_NDK="$ANDROID_NDK_ROOT" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DCMAKE_PREFIX_PATH="$QT_ANDROID" \
    -DQT_ANDROID_ABIS="$ANDROID_ABI" \
    -DCMAKE_FIND_ROOT_PATH="$QT_ANDROID" \
    -DANDROID_STL=c++_shared \
    -DANDROID_CPP_FEATURES="rtti exceptions" \
    -DCMAKE_ANDROID_ARCH_ABI="$ANDROID_ABI" \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_SYSTEM_VERSION="$ANDROID_API_LEVEL"

if [ $? -eq 0 ]; then
    print_success "Configuración CMake completada"
else
    print_error "Error en configuración CMake"
    exit 1
fi

################################################################################
# COMPILAR
################################################################################

print_header "Compilando QGroundControl..."

echo -e "${YELLOW}Esto puede tomar 15-30 minutos dependiendo de tu sistema...${NC}\n"

# Usar todos los cores disponibles
CORES=$(nproc)
echo "Usando $CORES cores para compilación"

make -j"$CORES"

if [ $? -eq 0 ]; then
    print_success "Compilación completada"
else
    print_error "Error en compilación"
    exit 1
fi

################################################################################
# GENERAR APK
################################################################################

print_header "Generando APK..."

# Intentar con make apk primero
echo "Intentando generar APK con make apk..."
make apk

if [ $? -eq 0 ]; then
    print_success "APK generado exitosamente con make apk"
else
    print_warning "Error con make apk, intentando método alternativo..."

    # Método alternativo: usar gradlew directamente
    echo "Intentando con gradlew assembleRelease..."
    cd android-build

    # Limpiar cache de Gradle
    ./gradlew clean

    # Generar APK con configuración específica
    ./gradlew assembleRelease --stacktrace --info

    if [ $? -eq 0 ]; then
        print_success "APK generado exitosamente con gradlew"
        cd ..
    else
        print_warning "Error con gradlew, intentando método directo..."
        cd ..

        # Método directo: usar target específico de CMake
        echo "Intentando con target QGroundControl_make_apk..."
        make QGroundControl_make_apk

        if [ $? -eq 0 ]; then
            print_success "APK generado exitosamente con target específico"
        else
            print_error "Todos los métodos fallaron. Verificando si hay APK parcial..."

            # Buscar APK aunque haya fallado
            APK_PARTIAL=$(find . -name "*.apk" -type f 2>/dev/null | head -n 1)
            if [ -n "$APK_PARTIAL" ]; then
                print_warning "Se encontró APK parcial, continuando..."
            else
                print_error "No se pudo generar APK"
                echo -e "\n${YELLOW}Posibles soluciones:${NC}"
                echo "1. Verificar espacio en disco: df -h"
                echo "2. Limpiar cache de Gradle: rm -rf ~/.gradle/caches"
                echo "3. Verificar permisos de escritura en build directory"
                echo "4. Intentar compilación debug: cambiar BUILD_TYPE a Debug"
                exit 1
            fi
        fi
    fi
fi

################################################################################
# ENCONTRAR Y MOSTRAR APK
################################################################################

print_header "Buscando APK generado..."

# Buscar APK en múltiples ubicaciones posibles
APK_LOCATIONS=(
    "$BUILD_DIR/android-build/build/outputs/apk/release/android-build-release.apk"
    "$BUILD_DIR/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
    "$BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk"
    "$BUILD_DIR/*.apk"
    "$BUILD_DIR/android-build/*.apk"
)

APK_PATH=""
for location in "${APK_LOCATIONS[@]}"; do
    if [ -f "$location" ]; then
        APK_PATH="$location"
        break
    else
        # Usar find para ubicaciones con wildcards
        FOUND_APK=$(find $(dirname "$location") -name "$(basename "$location")" -type f 2>/dev/null | head -n 1)
        if [ -n "$FOUND_APK" ]; then
            APK_PATH="$FOUND_APK"
            break
        fi
    fi
done

# Si no se encontró, buscar cualquier APK
if [ -z "$APK_PATH" ]; then
    APK_PATH=$(find "$BUILD_DIR" -name "*.apk" -type f 2>/dev/null | head -n 1)
fi

if [ -n "$APK_PATH" ]; then
    APK_SIZE=$(du -h "$APK_PATH" | cut -f1)
    print_success "APK encontrado:"
    echo -e "${GREEN}  Ubicación: $APK_PATH${NC}"
    echo -e "${GREEN}  Tamaño: $APK_SIZE${NC}"

    # Crear enlace simbólico en directorio raíz
    ln -sf "$APK_PATH" "$QGC_ROOT/QGroundControl.apk"
    print_success "Enlace creado: $QGC_ROOT/QGroundControl.apk"
else
    print_error "APK no encontrado en $BUILD_DIR"
    exit 1
fi

################################################################################
# INSTRUCCIONES FINALES
################################################################################

echo -e "\n${BLUE}================================${NC}"
echo -e "${GREEN}✓ COMPILACIÓN COMPLETADA${NC}"
echo -e "${BLUE}================================${NC}\n"

echo -e "${YELLOW}Próximos pasos:${NC}\n"

echo -e "1. ${BLUE}Instalar APK en Android:${NC}"
echo -e "   ${GREEN}adb install $APK_PATH${NC}"
echo -e "   O copiar a dispositivo y instalar manualmente\n"

echo -e "2. ${BLUE}Verificar IP configurada en el código C++:${NC}"
echo -e "   ${GREEN}nano $QGC_ROOT/src/Comms/TouchEventUdpSender.cc${NC}"
echo -e "   Línea 18: ${YELLOW}_targetAddress(\"TU_IP_AQUI\")${NC}\n"

echo -e "3. ${BLUE}Iniciar receptor UDP en computadora:${NC}"
echo -e "   ${GREEN}python3 $QGC_ROOT/test_touch_udp_android.py${NC}\n"

echo -e "4. ${BLUE}Verificar que ambos dispositivos estén en la misma red WiFi${NC}\n"

echo -e "5. ${BLUE}Ejecutar QGroundControl en Android${NC}"
echo -e "   Tocar el área de video → eventos aparecerán en computadora\n"

echo -e "${YELLOW}IMPORTANTE - Configuración de IP:${NC}"
echo -e "  La IP se configura en: ${GREEN}src/Comms/TouchEventUdpSender.cc${NC}"
echo -e "  Línea 18: ${YELLOW}_targetAddress(\"192.168.x.x\")${NC}"
echo -e "  ${RED}Debe editarse ANTES de compilar${NC}\n"

################################################################################
# PREGUNTAR SI INSTALAR
################################################################################

if command -v adb &> /dev/null; then
    if adb devices | grep -q "device$"; then
        echo -e "${YELLOW}Dispositivo Android detectado via ADB${NC}"
        read -p "¿Desea instalar el APK ahora? (s/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Ss]$ ]]; then
            print_header "Instalando APK..."
            adb install -r "$APK_PATH"
            if [ $? -eq 0 ]; then
                print_success "APK instalado correctamente"
            else
                print_error "Error al instalar APK"
            fi
        fi
    fi
fi

print_success "Script completado\n"
