#!/usr/bin/env python3
"""
Touch to Mouse Bridge - Convierte eventos táctiles UDP en movimientos del ratón
Recibe eventos de QGroundControl y los traduce a acciones del ratón del sistema
"""

import socket
import json
import sys
import time
from datetime import datetime

try:
    import pyautogui
except ImportError:
    sys.exit(1)

class TouchToMouseBridge:
    def __init__(self, port=8000):
        self.port = port
        self.sock = None
        self.screen_width, self.screen_height = pyautogui.size()
        self.is_dragging = False
        self.last_position = (0, 0)

        # Configuración de pyautogui
        pyautogui.FAILSAFE = True  # Mover ratón a esquina superior izquierda para parar
        pyautogui.PAUSE = 0.01     # Pausa mínima entre comandos

        # Estadísticas
        self.stats = {
            'total_events': 0,
            'pressed_events': 0,
            'moved_events': 0,
            'released_events': 0,
            'clicked_events': 0,
            'double_clicked_events': 0,
            'start_time': time.time()
        }

    def start_server(self):
        """Inicia el servidor UDP"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            server_address = ('0.0.0.0', self.port)
            self.sock.bind(server_address)
            return True

        except Exception as e:
            return False

    def normalize_coordinates(self, touch_x, touch_y, touch_width=None, touch_height=None):
        """
        Convierte coordenadas táctiles a coordenadas de pantalla

        Args:
            touch_x, touch_y: Coordenadas del evento táctil
            touch_width, touch_height: Tamaño del área táctil (opcional)

        Returns:
            (screen_x, screen_y): Coordenadas de pantalla
        """
        # Dimensiones reales del área táctil QGroundControl
        if touch_width is None:
            touch_width = 850  # Área de video QGroundControl
        if touch_height is None:
            touch_height = 470  # Área de video QGroundControl

        # Mapear coordenadas táctiles a coordenadas de pantalla
        screen_x = int((touch_x / touch_width) * self.screen_width)
        screen_y = int((touch_y / touch_height) * self.screen_height)

        # Asegurar que están dentro de los límites
        screen_x = max(0, min(screen_x, self.screen_width - 1))
        screen_y = max(0, min(screen_y, self.screen_height - 1))

        return screen_x, screen_y

    def handle_touch_event(self, event):
        """Procesa un evento táctil y lo convierte en acción del ratón"""
        try:
            event_type = event.get('type', 'unknown')
            touch_x = float(event.get('x', 0))
            touch_y = float(event.get('y', 0))

            # Convertir coordenadas
            screen_x, screen_y = self.normalize_coordinates(touch_x, touch_y)

            # Actualizar estadísticas
            self.stats['total_events'] += 1
            self.stats[f'{event_type}_events'] = self.stats.get(f'{event_type}_events', 0) + 1

            # Procesar según tipo de evento
            if event_type == 'pressed':
                self.handle_pressed(screen_x, screen_y)

            elif event_type == 'moved':
                self.handle_moved(screen_x, screen_y)

            elif event_type == 'released':
                self.handle_released(screen_x, screen_y)

            elif event_type == 'clicked':
                self.handle_clicked(screen_x, screen_y)

            elif event_type == 'doubleClicked':
                self.handle_double_clicked(screen_x, screen_y)

            else:
                pass

            self.last_position = (screen_x, screen_y)

        except Exception as e:
            pass

    def handle_pressed(self, x, y):
        """Maneja evento de presión (inicio de arrastre)"""
        pyautogui.moveTo(x, y)
        pyautogui.mouseDown(button='left')
        self.is_dragging = True

    def handle_moved(self, x, y):
        """Maneja evento de movimiento"""
        if self.is_dragging:
            pyautogui.dragTo(x, y, duration=0.01)
        else:
            pyautogui.moveTo(x, y)

    def handle_released(self, x, y):
        """Maneja evento de liberación (fin de arrastre)"""
        pyautogui.moveTo(x, y)
        if self.is_dragging:
            pyautogui.mouseUp(button='left')
            self.is_dragging = False

    def handle_clicked(self, x, y):
        """Maneja evento de click simple"""
        pyautogui.click(x, y)

    def handle_double_clicked(self, x, y):
        """Maneja evento de doble click"""
        pyautogui.doubleClick(x, y)

    def process_packet(self, data, address):
        """Procesa un paquete UDP recibido"""
        try:
            # Decodificar JSON
            text_data = data.decode('utf-8')
            event = json.loads(text_data)

            # Verificar si es un evento táctil válido
            if event.get('type') in ['pressed', 'moved', 'released', 'clicked', 'doubleClicked']:
                self.handle_touch_event(event)

        except json.JSONDecodeError:
            pass
        except Exception as e:
            pass

    def show_stats(self):
        """Muestra estadísticas del bridge"""
        pass

    def run(self):
        """Ejecuta el bridge principal"""
        if not self.start_server():
            return False

        try:
            while True:
                self.sock.settimeout(1.0)
                try:
                    data, address = self.sock.recvfrom(4096)
                    self.process_packet(data, address)
                except socket.timeout:
                    continue

        except KeyboardInterrupt:
            pass

        except Exception as e:
            pass

        finally:
            if self.sock:
                self.sock.close()

def main():
    """Función principal"""
    # Permitir puerto personalizado
    port = 8000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            sys.exit(1)

    bridge = TouchToMouseBridge(port)
    bridge.run()

if __name__ == '__main__':
    main()
