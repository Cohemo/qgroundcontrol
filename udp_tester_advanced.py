#!/usr/bin/env python3
"""
Tester UDP mejorado para recibir eventos táctiles de QGroundControl
Versión con diagnósticos avanzados y múltiples formatos de datos
"""

import socket
import json
import sys
import time
from datetime import datetime

class UDPTester:
    def __init__(self, port=8000):
        self.port = port
        self.sock = None
        self.stats = {
            'total_packets': 0,
            'json_packets': 0,
            'invalid_packets': 0,
            'start_time': time.time()
        }

    def start_server(self):
        """Inicia el servidor UDP"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # Permitir reutilizar la dirección
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # Bind a todas las interfaces
            server_address = ('0.0.0.0', self.port)
            self.sock.bind(server_address)

            print("=" * 80)
            print(f"🚀 UDP TESTER INICIADO")
            print("=" * 80)
            print(f"📡 Escuchando en: {server_address[0]}:{server_address[1]}")
            print(f"🕒 Iniciado: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"🔧 Socket configurado con SO_REUSEADDR")
            print("=" * 80)
            print("📋 FORMATOS SOPORTADOS:")
            print("   • JSON QGroundControl touch events")
            print("   • JSON test messages")
            print("   • Raw text data")
            print("=" * 80)
            print("⏳ Esperando datos UDP...")
            print()

            return True

        except Exception as e:
            print(f"❌ ERROR iniciando servidor: {e}")
            return False

    def process_packet(self, data, address):
        """Procesa un paquete UDP recibido"""
        self.stats['total_packets'] += 1

        # Información del remitente
        print(f"📦 Paquete #{self.stats['total_packets']} desde {address[0]}:{address[1]}")

        # Intentar decodificar como JSON
        try:
            # Decodificar bytes a string
            text_data = data.decode('utf-8')
            print(f"📝 Datos raw: {text_data}")

            # Intentar parsear JSON
            try:
                event = json.loads(text_data)
                self.stats['json_packets'] += 1
                self.process_json_event(event)

            except json.JSONDecodeError:
                print("⚠️  No es JSON válido, procesando como texto plano")
                self.process_raw_text(text_data)

        except UnicodeDecodeError as e:
            print(f"❌ Error decodificando UTF-8: {e}")
            print(f"🔢 Datos hex: {data.hex()}")
            self.stats['invalid_packets'] += 1

        print("-" * 60)

    def process_json_event(self, event):
        """Procesa un evento JSON"""
        print("✅ JSON VÁLIDO detectado")

        # Detectar tipo de evento
        event_type = event.get('type', 'unknown')

        if event_type in ['pressed', 'released', 'moved', 'clicked', 'doubleClicked']:
            self.process_touch_event(event)
        elif event_type == 'test' or 'test' in event:
            self.process_test_event(event)
        else:
            self.process_generic_json(event)

    def process_touch_event(self, event):
        """Procesa eventos táctiles de QGroundControl"""
        print("🎯 EVENTO TÁCTIL QGROUNDCONTROL")

        event_type = event.get('type', 'unknown')
        x = event.get('x', 0)
        y = event.get('y', 0)
        timestamp_ms = event.get('timestamp', 0)
        source = event.get('source', 'unknown')

        # Convertir timestamp
        if timestamp_ms > 0:
            try:
                timestamp_str = datetime.fromtimestamp(timestamp_ms / 1000.0).strftime('%H:%M:%S.%f')[:-3]
            except:
                timestamp_str = "invalid"
        else:
            timestamp_str = "no-timestamp"

        print(f"   📍 Tipo: {event_type}")
        print(f"   🎯 Coordenadas: X={x:7.2f}, Y={y:7.2f}")
        print(f"   🕒 Timestamp: {timestamp_str}")
        print(f"   📱 Fuente: {source}")

    def process_test_event(self, event):
        """Procesa eventos de prueba"""
        print("🧪 EVENTO DE PRUEBA")

        for key, value in event.items():
            print(f"   {key}: {value}")

    def process_generic_json(self, event):
        """Procesa JSON genérico"""
        print("📄 JSON GENÉRICO")

        for key, value in event.items():
            print(f"   {key}: {value}")

    def process_raw_text(self, text):
        """Procesa texto plano"""
        print("📝 TEXTO PLANO")
        print(f"   Contenido: {text}")

    def show_stats(self):
        """Muestra estadísticas"""
        duration = time.time() - self.stats['start_time']
        print("\n" + "=" * 60)
        print("📊 ESTADÍSTICAS")
        print("=" * 60)
        print(f"⏱️  Tiempo activo: {duration:.1f} segundos")
        print(f"📦 Total paquetes: {self.stats['total_packets']}")
        print(f"✅ Paquetes JSON: {self.stats['json_packets']}")
        print(f"❌ Paquetes inválidos: {self.stats['invalid_packets']}")
        if duration > 0:
            rate = self.stats['total_packets'] / duration
            print(f"📈 Tasa: {rate:.2f} paquetes/segundo")

    def run(self):
        """Ejecuta el servidor principal"""
        if not self.start_server():
            return False

        try:
            while True:
                # Recibir datos con timeout
                self.sock.settimeout(1.0)
                try:
                    data, address = self.sock.recvfrom(4096)
                    self.process_packet(data, address)
                except socket.timeout:
                    # Timeout silencioso para permitir Ctrl+C
                    continue

        except KeyboardInterrupt:
            print("\n🛑 Interrumpido por usuario")
            self.show_stats()

        except Exception as e:
            print(f"\n❌ ERROR en servidor: {e}")

        finally:
            if self.sock:
                self.sock.close()
                print("🔌 Socket cerrado")

def main():
    """Función principal"""
    print("🎯 UDP Tester para QGroundControl Touch Events")
    print("Presiona Ctrl+C para salir\n")

    # Permitir puerto personalizado
    port = 8000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print(f"❌ Puerto inválido: {sys.argv[1]}")
            sys.exit(1)

    tester = UDPTester(port)
    tester.run()

if __name__ == '__main__':
    main()
