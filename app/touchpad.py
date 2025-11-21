#!/usr/bin/env python3
"""
Tauwerk Framebuffer UI - Low Latency Alternative zu PyGame
Komplett mit Fadern, Buttons und Touch-Input
"""

import os
import mmap
import time
from typing import Tuple, Dict, List, Optional, Callable
from enum import Enum
from PIL import Image, ImageFont, ImageDraw
from evdev import InputDevice, ecodes, list_devices
import select

# Enums für bessere Lesbarkeit
class TouchState(Enum):
    TOUCH_DOWN = 1
    TOUCH_DRAG = 3  
    TOUCH_UP = 4

class FaderMode(Enum):
    JUMP = "jump"
    INCREMENTAL = "incremental"  
    SMOOTH = "smooth"

class TouchEvent:
    __slots__ = ('finger_id', 'x', 'y', 'state', 'target')
    
    def __init__(self, finger_id: int, x: int, y: int, state: TouchState):
        self.finger_id = finger_id
        self.x = x
        self.y = y
        self.state = state
        self.target = None

class UIElement:
    def __init__(self, uid: str, x: int, y: int, width: int, height: int):
        self.uid = uid
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.visible = True
        
        self.on_touch_down: Optional[Callable] = None
        self.on_touch_up: Optional[Callable] = None  
        self.on_touch_move: Optional[Callable] = None
        
    def contains(self, x: int, y: int) -> bool:
        return (self.x <= x <= self.x + self.width and 
                self.y <= y <= self.y + self.height)
    
    def handle_touch_event(self, event: TouchEvent) -> bool:
        if not self.contains(event.x, event.y):
            return False
            
        if event.state == TouchState.TOUCH_DOWN and self.on_touch_down:
            self.on_touch_down(event)
            return True
        elif event.state == TouchState.TOUCH_UP and self.on_touch_up:
            self.on_touch_up(event)
            return True
        elif event.state == TouchState.TOUCH_DRAG and self.on_touch_move:
            self.on_touch_move(event)
            return True
            
        return False
    
    def draw(self, display: 'FramebufferDisplay'):
        raise NotImplementedError

class Button(UIElement):
    def __init__(self, x: int, y: int, width: int, height: int, text: str = ""):
        super().__init__(f"button_{x}_{y}", x, y, width, height)
        self.text = text
        self.pressed = False
        
        self.on_click: Optional[Callable] = None
        self.on_release: Optional[Callable] = None
    
    def handle_touch_event(self, event: TouchEvent) -> bool:
        if not super().handle_touch_event(event):
            return False
            
        if event.state == TouchState.TOUCH_DOWN:
            self.pressed = True
            if self.on_click:
                self.on_click()
        elif event.state == TouchState.TOUCH_UP:
            self.pressed = False  
            if self.on_release:
                self.on_release()
                
        return True
    
    def draw(self, display: 'FramebufferDisplay'):
        if not self.visible:
            return
            
        # Button Hintergrund
        if self.pressed:
            display.draw_rect(self.x, self.y, self.width, self.height, (100, 150, 100))
        else:
            display.draw_rect(self.x, self.y, self.width, self.height, (60, 90, 60))
            
        # Button Rahmen
        display.draw_rect(self.x, self.y, self.width, 2, (160, 200, 160))
        display.draw_rect(self.x, self.y + self.height - 2, self.width, 2, (160, 200, 160))
        
        # Text
        if self.text:
            text_x = self.x + (self.width // 2) - (len(self.text) * 3)
            text_y = self.y + (self.height // 2) - 6
            display.draw_text(text_x, text_y, self.text, (160, 200, 160), 'small')

class Fader(UIElement):
    def __init__(self, x: int, y: int, width: int, height: int, text: str = "", 
                 min_val: int = 0, max_val: int = 100, mode: FaderMode = FaderMode.INCREMENTAL):
        super().__init__(f"fader_{x}_{y}", x, y, width, height)
        self.text = text
        self.min_val = min_val
        self.max_val = max_val
        self.value = min_val
        self.target_value = min_val
        self.mode = mode
        
        self.on_change: Optional[Callable] = None
        
        # Mode-spezifische Einstellungen
        self.step_size = 1
        self.smooth_speed = 0.1
        self.last_touch_x = None
    
    def set_value(self, new_value: float):
        new_value = max(self.min_val, min(new_value, self.max_val))
        
        if abs(new_value - self.value) > 0.01:
            self.value = new_value
            if self.on_change:
                self.on_change(self.value)
    
    def handle_touch_event(self, event: TouchEvent) -> bool:
        if not self.contains(event.x, event.y):
            return False
            
        if event.state == TouchState.TOUCH_DOWN:
            self.last_touch_x = event.x
            
            if self.mode == FaderMode.JUMP:
                self.update_value_from_pos(event.x)
            elif self.mode == FaderMode.SMOOTH:
                self.set_target_from_pos(event.x)
                        
        elif event.state == TouchState.TOUCH_DRAG:
            if self.mode == FaderMode.JUMP:
                self.update_value_from_pos(event.x)
            elif self.mode == FaderMode.INCREMENTAL:
                self.update_value_incremental(event.x)
            elif self.mode == FaderMode.SMOOTH:
                self.set_target_from_pos(event.x)
                            
        return True

    def update_value_from_pos(self, x_pos: int):
        percentage = (x_pos - self.x) / self.width
        percentage = max(0.0, min(1.0, percentage))
        new_value = self.min_val + percentage * (self.max_val - self.min_val)
        self.set_value(new_value)
  
    def update_value_incremental(self, x_pos: int):
        if self.last_touch_x is None:
            self.last_touch_x = x_pos
            return
            
        delta_x = x_pos - self.last_touch_x
        movement_factor = abs(delta_x) / 50.0
        value_change = self.step_size * movement_factor * (1 if delta_x > 0 else -1)
        
        new_value = self.value + value_change
        self.last_touch_x = x_pos
        self.set_value(new_value)
  
    def set_target_from_pos(self, x_pos: int):
        percentage = (x_pos - self.x) / self.width
        percentage = max(0.0, min(1.0, percentage))
        self.target_value = self.min_val + percentage * (self.max_val - self.min_val)
  
    def update_smooth(self):
        if self.value == self.target_value:
            return
            
        difference = self.target_value - self.value
        step = difference * self.smooth_speed
        
        if abs(step) < 0.1:
            step = 0.1 if difference > 0 else -0.1
            
        new_value = self.value + step
        
        if abs(difference) < 0.5:
            new_value = self.target_value
            
        self.set_value(new_value)
  
    def loop(self):
        if self.mode == FaderMode.SMOOTH:
            self.update_smooth()
    
    def draw(self, display: 'FramebufferDisplay'):
        if not self.visible:
            return
            
        # Fader Hintergrund
        display.draw_rect(self.x, self.y, self.width, self.height, (40, 40, 40))
        
        # Fader Wert
        percentage = (self.value - self.min_val) / (self.max_val - self.min_val)
        fader_width = int(self.width * percentage)
        display.draw_rect(self.x, self.y, fader_width, self.height, (160, 200, 160))
        
        # Text und Wert
        if self.text:
            display.draw_text(self.x + 5, self.y - 15, self.text, (160, 200, 160), 'small')
            
        value_text = f"{int(self.value)}"
        display.draw_text(self.x + self.width - 30, self.y - 15, value_text, (160, 200, 160), 'small')

class FramebufferDisplay:
    def __init__(self):
        self.width = 800
        self.height = 480
        self.fb_map = None
        self.fd = None
        
        # Fonts aus Assets
        self.fonts = {
            'small': self.load_font("assets/fonts/ds_digital/ds_digi_bold.ttf", 12),
            'medium': self.load_font("assets/fonts/ds_digital/ds_digi_bold.ttf", 18), 
            'large': self.load_font("assets/fonts/ds_digital/ds_digi_bold.ttf", 24),
            'digits': self.load_font("assets/fonts/ds_digital/ds_digi_bold.ttf", 36)
        }
    
    def load_font(self, font_path: str, size: int):
        """Lädt Font aus Assets - fallback zu System Font"""
        try:
            if os.path.exists(font_path):
                return ImageFont.truetype(font_path, size)
            else:
                print(f"⚠️  Font nicht gefunden: {font_path} - verwende Default")
                return ImageFont.load_default()
        except Exception as e:
            print(f"⚠️  Font Fehler: {e} - verwende Default")
            return ImageFont.load_default()
    
    def open(self) -> bool:
        """Öffnet Framebuffer"""
        try:
            self.fd = os.open("/dev/fb0", os.O_RDWR)
            
            # Display Info lesen
            with open('/sys/class/graphics/fb0/virtual_size', 'r') as f:
                self.width, self.height = map(int, f.read().strip().split(','))
            
            # Framebuffer mappen
            fb_size = self.width * self.height * 4
            self.fb_map = mmap.mmap(self.fd, fb_size, mmap.MAP_SHARED, 
                                  mmap.PROT_READ | mmap.PROT_WRITE)
            
            print(f"✅ Framebuffer: {self.width}x{self.height}")
            return True
            
        except Exception as e:
            print(f"❌ Framebuffer open: {e}")
            return False
    
    def close(self):
        """Schließt Framebuffer"""
        if self.fb_map:
            self.fb_map.close()
        if self.fd:
            os.close(self.fd)
    
    def clear(self, color: Tuple[int, int, int] = (0, 0, 0)):
        """Löscht Screen mit Farbe (R,G,B)"""
        r, g, b = color
        color_bytes = bytes([b, g, r, 255])  # BGR Format!
        
        self.fb_map.seek(0)
        self.fb_map.write(color_bytes * (self.width * self.height))
    
    def draw_pixel(self, x: int, y: int, color: Tuple[int, int, int]):
        """Zeichnet einzelnen Pixel (R,G,B)"""
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            return
            
        r, g, b = color
        color_bytes = bytes([b, g, r, 255])  # BGR Format!
        
        offset = (y * self.width + x) * 4
        self.fb_map.seek(offset)
        self.fb_map.write(color_bytes)
    
    def draw_rect(self, x: int, y: int, width: int, height: int, 
                  color: Tuple[int, int, int] = (255, 0, 0)):
        """Zeichnet Rechteck (R,G,B)"""
        for py in range(y, y + height):
            if py < 0 or py >= self.height:
                continue
            for px in range(x, x + width):
                if px < 0 or px >= self.width:
                    continue
                self.draw_pixel(px, py, color)
    
    def draw_text(self, x: int, y: int, text: str, color: Tuple[int, int, int] = (255, 255, 255), 
                  font_key: str = 'medium'):
        """Zeichnet Text mit Bitmap-Font Rendering"""
        font = self.fonts.get(font_key, self.fonts['medium'])
        
        try:
            # Text mit PIL rendern
            with Image.new('RGBA', (self.width, self.height), (0, 0, 0, 0)) as image:
                draw = ImageDraw.Draw(image)
                draw.text((x, y), text, font=font, fill=color + (255,))
                
                # Text-Bitmap in Framebuffer kopieren
                pixels = image.load()
                for py in range(image.height):
                    for px in range(image.width):
                        r, g, b, a = pixels[px, py]
                        if a > 128:  # Nur nicht-transparente Pixel
                            self.draw_pixel(px, py, (r, g, b))
        except Exception as e:
            print(f"⚠️  Text Fehler: {e}")

class TouchInput:
    def __init__(self):
        self.device_path = self.autodetect_touch()
        self.device = None
        self.setup()
        
    def autodetect_touch(self) -> str:
        """Automatische Touch-Device Erkennung"""
        try:
            for device_path in list_devices():
                device = InputDevice(device_path)
                if self.is_touch_device(device):
                    return device.path
        except Exception as e:
            print(f"⚠️  Auto-detect error: {e}")
        return "/dev/input/event2"  # Fallback
    
    def is_touch_device(self, device) -> bool:
        """Prüft ob Device Touchscreen ist"""
        try:
            caps = device.capabilities()
            has_abs_x = ecodes.ABS_X in caps.get(ecodes.EV_ABS, [])
            has_abs_y = ecodes.ABS_Y in caps.get(ecodes.EV_ABS, [])
            has_btn_touch = ecodes.BTN_TOUCH in caps.get(ecodes.EV_KEY, [])
            return has_abs_x and has_abs_y and has_btn_touch
        except:
            return False
    
    def setup(self):
        """Setup Touch Device"""
        try:
            self.device = InputDevice(self.device_path)
            print(f"✅ Touch Device: {self.device.name}")
        except Exception as e:
            print(f"❌ Touch setup: {e}")
    
    def get_events(self) -> List[TouchEvent]:
        """Liest Touch-Events und gibt als TouchEvent Liste zurück"""
        events = []
        
        if not self.device:
            return events
            
        try:
            r, w, x = select.select([self.device], [], [], 0.001)
            if r:
                for event in self.device.read():
                    if event.type == ecodes.EV_ABS:
                        if event.code == ecodes.ABS_X:
                            current_x = event.value
                        elif event.code == ecodes.ABS_Y:
                            current_y = event.value
                    elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH:
                        if event.value == 1:  # Touch down
                            touch_event = TouchEvent(0, current_x, current_y, TouchState.TOUCH_DOWN)
                            events.append(touch_event)
                        else:  # Touch up
                            touch_event = TouchEvent(0, current_x, current_y, TouchState.TOUCH_UP)
                            events.append(touch_event)
        except Exception as e:
            print(f"⚠️  Touch read error: {e}")
            
        return events

class Touchpad:
    def __init__(self):
        self.display = FramebufferDisplay()
        self.touch = TouchInput()
        self.elements: List[UIElement] = []
        
        self.running = False
        
    def setup(self):
        """Setup Display und UI"""
        if not self.display.open():
            return False
            
        self.create_test_ui()
        self.running = True
        return True
    
    def create_test_ui(self):
        """Erstellt Test-UI mit Fadern und Buttons"""
        # Buttons
        button1 = Button(50, 50, 100, 60, "PLAY")
        button1.on_click = lambda: print("▶️ Play pressed")
        self.elements.append(button1)
        
        button2 = Button(170, 50, 100, 60, "STOP") 
        button2.on_click = lambda: print("⏹️ Stop pressed")
        self.elements.append(button2)
        
        # Fader mit verschiedenen Modes
        fader1 = Fader(50, 150, 700, 40, "VOLUME", 0, 100, FaderMode.JUMP)
        fader1.on_change = lambda v: print(f"🔊 Volume: {v}")
        self.elements.append(fader1)
        
        fader2 = Fader(50, 220, 700, 40, "PAN", -50, 50, FaderMode.INCREMENTAL)
        fader2.on_change = lambda v: print(f"🎛️ Pan: {v}")
        self.elements.append(fader2)
        
        fader3 = Fader(50, 290, 700, 40, "FILTER", 0, 100, FaderMode.SMOOTH)
        fader3.on_change = lambda v: print(f"🔧 Filter: {v}")
        self.elements.append(fader3)
    
    def handle_touch_events(self):
        """Verarbeitet Touch-Events"""
        events = self.touch.get_events()
        for event in events:
            for element in self.elements:
                if element.handle_touch_event(event):
                    break
    
    def update_elements(self):
        """Updated UI-Elemente (z.B. Smooth Fader)"""
        for element in self.elements:
            if hasattr(element, 'loop'):
                element.loop()
    
    def draw_ui(self):
        """Zeichnet komplette UI"""
        self.display.clear((0, 0, 0))  # Schwarzer Hintergrund
        
        # UI-Elemente zeichnen
        for element in self.elements:
            element.draw(self.display)
        
        # Titel
        self.display.draw_text(320, 10, "TAUWERK", (160, 200, 160), 'large')
    
    def close(self):
      self.display.clear((0, 0, 0))
      self.display.close()
      print("\n🛑 Touchpad UI beendet")

    def run(self):
      if self.running:
        self.handle_touch_events()
        self.update_elements()
        self.draw_ui()

if __name__ == "__main__":
    ui = Touchpad()
    ui.run()