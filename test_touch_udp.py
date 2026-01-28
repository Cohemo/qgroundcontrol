#!/usr/bin/env python3
"""
Script de prueba para recibir eventos táctiles UDP de QGroundControl
Escucha en el puerto 8000 y muestra los eventos recibidos
"""

import socket
import json
from datetime import datetime

def main():
    # Crear socket UDP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Bind al puerto 8000 en todas las interfaces
    server_address = ('0.0.0.0', 8000)
    sock.bind(server_address)

    print(f"Escuchando eventos táctiles UDP en {server_address[0]}:{server_address[1]}")
    print("Esperando eventos de QGroundControl...")
    print("-" * 60)

    try:
        while True:
            # Recibir datos
            data, address = sock.recvfrom(4096)

            # Decodificar JSON
            try:
                event = json.loads(data.decode('utf-8'))

                # Obtener timestamp legible
                timestamp_ms = event.get('timestamp', 0)
                timestamp_str = datetime.fromtimestamp(timestamp_ms / 1000.0).strftime('%H:%M:%S.%f')[:-3]

                # Mostrar evento
                event_type = event.get('type', 'unknown')
                x = event.get('x', 0)
                y = event.get('y', 0)

                print(f"[{timestamp_str}] {event_type:15s} -> X: {x:7.2f}, Y: {y:7.2f}")

            except json.JSONDecodeError as e:
                print(f"Error decodificando JSON: {e}")
                print(f"Datos recibidos: {data}")

    except KeyboardInterrupt:
        print("\n\nCerrando servidor...")
    finally:
        sock.close()

if __name__ == '__main__':
    main()
