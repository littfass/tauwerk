import pygame
import math
import os
from evdev import InputDevice, ecodes, list_devices
from select import select
from collections import defaultdict
from typing import Dict, Optional, Callable, Set
import time

# Constants
DEFAULT_FONT = "dejavusans"
DEFAULT_DIGIT = "assets/fonts/ds_digital/ds_digi_bold.ttf"
DEFAULT_COLOR = (160, 200, 160)

class TouchState:
    TOUCH_DOWN = 1
    TOUCH_DRAG = 3  
    TOUCH_UP = 4

class FaderMode:
    JUMP = "jump"
    INCREMENTAL = "incremental"  
    SMOOTH = "smooth"
    # MOMENTUM entfernt um CPU zu sparen

class TouchEvent:
    __slots__ = ('finger_id', 'x', 'y', 'state', 'target', 'raw_x', 'raw_y')
    
    def __init__(self, finger_id: int, x: int, y: int, state: int, raw_x: int = None, raw_y: int = None):
        self.finger_id = finger_id
        self.x = x
        self.y = y
        self.state = state
        self.target = None
        self.raw_x = raw_x
        self.raw_y = raw_y

class CollisionRect:
    __slots__ = ('x', 'y', 'width', 'height', 'uid', '_rect')
    
    def __init__(self, x: int, y: int, width: int, height: int, uid: str = None):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.uid = uid
        self._rect = None
    
    @property
    def rect(self) -> pygame.Rect:
        if self._rect is None:
            self._rect = pygame.Rect(self.x, self.y, self.width, self.height)
        return self._rect
    
    def contains(self, x: int, y: int) -> bool:
        return (self.x <= x <= self.x + self.width and 
                self.y <= y <= self.y + self.height)

class TouchManager:
    def __init__(self):
        self.elements: Dict[str, CollisionRect] = {}
        self.active_touches: Dict[int, str] = {}
        self.hovered_touches: Dict[int, str] = {}
        self.touch_start_elements: Dict[int, str] = {}
        
        self.on_touch_enter: Optional[Callable] = None
        self.on_touch_leave: Optional[Callable] = None
        
        self._element_list = []
    
    def register_element(self, uid: str, rect: CollisionRect):
        self.elements[uid] = rect
        self._element_list = list(self.elements.values())
    
    def unregister_element(self, uid: str):
        if uid in self.elements:
            for finger_id in list(self.active_touches.keys()):
                if self.active_touches[finger_id] == uid:
                    del self.active_touches[finger_id]
                    del self.touch_start_elements[finger_id]
            
            for finger_id in list(self.hovered_touches.keys()):
                if self.hovered_touches[finger_id] == uid:
                    del self.hovered_touches[finger_id]
            
            del self.elements[uid]
            self._element_list = list(self.elements.values())
    
    def process_touch_event(self, event: TouchEvent) -> Optional[str]:
        current_element = None
        
        for element in self._element_list:
            if element.contains(event.x, event.y):
                current_element = element.uid
                break
        
        if event.state == TouchState.TOUCH_DOWN:
            if current_element and event.finger_id not in self.active_touches:
                self.active_touches[event.finger_id] = current_element
                self.touch_start_elements[event.finger_id] = current_element
                self.hovered_touches.pop(event.finger_id, None)
                event.target = current_element
                return current_element
            elif current_element:
                self.hovered_touches[event.finger_id] = current_element
                event.target = current_element
                return current_element
                
        elif event.state == TouchState.TOUCH_DRAG:
            if event.finger_id in self.active_touches:
                original_element = self.active_touches[event.finger_id]
                event.target = original_element
                return original_element
                
            elif event.finger_id in self.hovered_touches:
                event.target = self.hovered_touches.get(event.finger_id)
                return event.target
            
        elif event.state == TouchState.TOUCH_UP:
            original_element = self.touch_start_elements.get(event.finger_id)
            active_element = self.active_touches.get(event.finger_id)
            hovered_element = self.hovered_touches.get(event.finger_id)
            
            element_to_notify = active_element or hovered_element or original_element
            
            if element_to_notify:
                event.target = element_to_notify
                self.active_touches.pop(event.finger_id, None)
                self.hovered_touches.pop(event.finger_id, None)
                self.touch_start_elements.pop(event.finger_id, None)
                return element_to_notify
        
        return None

class UIElement:
    def __init__(self, uid: str, x: int, y: int, width: int, height: int):
        self.uid = uid
        self.rect = CollisionRect(x, y, width, height, uid)
        self.touch_manager = None
        
        self.active_touches: Set[int] = set()
        self.hovered_touches: Set[int] = set()
        
        self.on_touch_down: Optional[Callable] = None
        self.on_touch_up: Optional[Callable] = None  
        self.on_touch_move: Optional[Callable] = None
        self.on_touch_enter: Optional[Callable] = None
        self.on_touch_leave: Optional[Callable] = None
    
    def register_touch(self, touch_manager: TouchManager):
        self.touch_manager = touch_manager
        touch_manager.register_element(self.uid, self.rect)
    
    def unregister_touch(self):
        if self.touch_manager:
            self.touch_manager.unregister_element(self.uid)
    
    def handle_touch_event(self, event: TouchEvent) -> bool:
        if event.target != self.uid:
            return False
            
        if event.state == TouchState.TOUCH_DOWN:
            self.active_touches.add(event.finger_id)
            self.hovered_touches.discard(event.finger_id)
            if self.on_touch_down:
                self.on_touch_down(event)
            return True
                
        elif event.state == TouchState.TOUCH_UP:
            was_active = event.finger_id in self.active_touches
            self.active_touches.discard(event.finger_id)
            self.hovered_touches.discard(event.finger_id)
            if was_active and self.on_touch_up:
                self.on_touch_up(event)
            return True
                
        elif event.state == TouchState.TOUCH_DRAG:
            if event.finger_id in self.active_touches:
                if self.on_touch_move:
                    self.on_touch_move(event)
                return True
            elif event.finger_id in self.hovered_touches:
                return True
                    
        return False
    
    @property
    def is_active(self) -> bool:
        return len(self.active_touches) > 0
        
    @property 
    def is_hovered(self) -> bool:
        return len(self.hovered_touches) > 0
    
    def draw(self, surface):
        raise NotImplementedError

class Dither:
    @staticmethod
    def stripes(width, height):
        pattern = pygame.Surface((width, height))
        pattern.fill((0, 0, 0))
        
        for y in range(0, height, 2):
            for x in range(0, width, 2):
                if (x // 2 + y // 2) % 2 == 0:
                    pattern.set_at((x, y), (100, 120, 100))
                    if x + 1 < width and y + 1 < height:
                        pattern.set_at((x + 1, y + 1), (100, 120, 100))
        return pattern

    @staticmethod
    def dots(width, height):  
        pattern = pygame.Surface((width, height))
        pattern.fill((0, 0, 0))
        
        for y in range(0, height, 4):
            for x in range(0, width, 4):
                pattern.set_at((x, y), (100, 120, 100))
                if x + 2 < width:
                    pattern.set_at((x + 2, y + 2), (100, 120, 100))
        return pattern

class Button(UIElement):
    def __init__(self, x: int, y: int, width: int, height: int, text: str = ""):
        uid = f"button_{x}_{y}_{width}_{height}"
        super().__init__(uid, x, y, width, height)
        self.text = text
        self.dither_pattern = Dither.dots(width, height)
        
        self.on_click: Optional[Callable] = None
        self.on_release: Optional[Callable] = None
    
    def handle_touch_event(self, event: TouchEvent) -> bool:
        processed = super().handle_touch_event(event)
        
        if processed and event.state == TouchState.TOUCH_DOWN:
            if self.on_click:
                self.on_click()
                
        elif processed and event.state == TouchState.TOUCH_UP:
            if self.on_release:
                self.on_release()
                
        return processed
    
    def draw(self, surface):
        if self.is_active:
            pygame.draw.rect(surface, DEFAULT_COLOR, self.rect.rect)
        elif self.is_hovered:
            pygame.draw.rect(surface, (120, 180, 120), self.rect.rect)
        else:
            surface.blit(self.dither_pattern, self.rect.rect)
      
        if self.text:
            font = pygame.font.SysFont(DEFAULT_FONT, 11)
            text_surface = font.render(self.text, True, DEFAULT_COLOR)
            text_rect = text_surface.get_rect(center=(self.rect.rect.centerx, self.rect.rect.bottom + 15))
            surface.blit(text_surface, text_rect)

class Fader(UIElement):
    def __init__(self, x, y, width, height, text="", min_val=0, max_val=100, 
                 touch_padding=20, mode=FaderMode.INCREMENTAL, step_size=1, 
                 smooth_speed=0.1, overflow=50):
        
        uid = f"fader_{x}_{y}_{width}_{height}"
        super().__init__(uid, x, y, width, height)
        
        self.text = text
        self.min_val = min_val
        self.max_val = max_val
        self.value = min_val
        self.target_value = min_val  # ✅ FIX: target_value initialisieren
        self.on_change = None
        self.bg_pattern = Dither.dots(width, height)
        self.fg_rect = pygame.Rect(x, y, 0, height)
        
        self.mode = mode
        self.step_size = step_size
        self.smooth_speed = smooth_speed
        self.overflow = max(overflow, 50)
        
        self.overflow_zone = pygame.Rect(
            x - self.overflow,
            y - self.overflow,
            width + (2 * self.overflow),
            height + (2 * self.overflow)
        )
        self.overflow_zone.left = max(0, self.overflow_zone.left)
        self.overflow_zone.top = max(0, self.overflow_zone.top)
        self.overflow_zone.width = min(800 - self.overflow_zone.left, self.overflow_zone.width)
        self.overflow_zone.height = min(480 - self.overflow_zone.top, self.overflow_zone.height)
        
        self.touch_padding = touch_padding
        self.virtual_left = x
        self.virtual_right = x + width
        if x == 0:
            self.virtual_left += touch_padding
        if x + width >= 800:
            self.virtual_right -= touch_padding
        self.virtual_width = self.virtual_right - self.virtual_left
        
        # Vereinfacht: Momentum-Variablen entfernt
        self.last_touch_x = None
  
    def handle_touch_event(self, event: TouchEvent) -> bool:
        if event.finger_id in self.active_touches:
            return self._process_touch_for_active_finger(event)
        
        is_in_overflow_zone = self.overflow_zone.collidepoint(event.x, event.y)
        if not is_in_overflow_zone:
            return False
            
        processed = super().handle_touch_event(event)
        if not processed:
            return False
            
        return self._process_touch_for_active_finger(event)
    
    def _process_touch_for_active_finger(self, event: TouchEvent) -> bool:
        if event.state == TouchState.TOUCH_DOWN:
            self.last_touch_x = event.x
            
            if self.mode == FaderMode.JUMP:
                self.update_value_from_pos(event.x)
            elif self.mode == FaderMode.SMOOTH:
                self.set_target_from_pos(event.x)
                        
        elif event.state == TouchState.TOUCH_DRAG:
            if event.finger_id in self.active_touches:
                if self.mode == FaderMode.JUMP:
                    self.update_value_from_pos(event.x)
                elif self.mode == FaderMode.INCREMENTAL:
                    self.update_value_incremental(event.x)
                elif self.mode == FaderMode.SMOOTH:
                    self.set_target_from_pos(event.x)
                            
        elif event.state == TouchState.TOUCH_UP:
            if event.finger_id in self.active_touches:
                self.last_touch_x = None
                return True

        return False

    def update_value_from_pos(self, x_pos):
        clamped_x = max(self.rect.x, min(x_pos, self.rect.x + self.rect.width))
        
        if clamped_x <= self.virtual_left:
            percentage = 0.0
        elif clamped_x >= self.virtual_right:
            percentage = 1.0
        else:
            relative_x = clamped_x - self.virtual_left
            percentage = relative_x / self.virtual_width
        
        new_value = self.min_val + percentage * (self.max_val - self.min_val)
        self.set_value(new_value)
  
    def update_value_incremental(self, x_pos):
        if self.last_touch_x is None:
            self.last_touch_x = x_pos
            return
            
        delta_x = x_pos - self.last_touch_x
        
        if abs(delta_x) < 2:
            return
            
        movement_factor = abs(delta_x) / 50.0
        value_change = self.step_size * movement_factor * (1 if delta_x > 0 else -1)
        
        new_value = self.value + value_change
        self.last_touch_x = x_pos
        self.set_value(new_value)
  
    def set_target_from_pos(self, x_pos):
        clamped_x = max(self.rect.x, min(x_pos, self.rect.x + self.rect.width))
        
        if clamped_x <= self.virtual_left:
            percentage = 0.0
        elif clamped_x >= self.virtual_right:
            percentage = 1.0
        else:
            relative_x = clamped_x - self.virtual_left
            percentage = relative_x / self.virtual_width
        
        self.target_value = self.min_val + percentage * (self.max_val - self.min_val)
  
    def update_value_smooth(self):
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
  
    def set_value(self, new_value):
        new_value = max(self.min_val, min(new_value, self.max_val))
        
        if abs(new_value - self.value) > 0.01:
            self.value = new_value
            
            percentage = (self.value - self.min_val) / (self.max_val - self.min_val)
            self.fg_rect.width = int(self.rect.rect.width * percentage)
            self.fg_rect.height = self.rect.rect.height
            
            if self.on_change:
                self.on_change(self.value)
  
    def loop(self):
        """Vereinfachte Loop ohne Momentum"""
        if self.mode == FaderMode.SMOOTH and (self.is_active or self.value != self.target_value):
            self.update_value_smooth()
  
    def draw(self, surface):
        surface.blit(self.bg_pattern, self.rect.rect)
        pygame.draw.rect(surface, DEFAULT_COLOR, self.fg_rect)
        
        if self.text:
            font = pygame.font.SysFont(DEFAULT_FONT, 11)
            text_surface = font.render(self.text, True, DEFAULT_COLOR)
            text_rect = text_surface.get_rect(center=(self.rect.rect.centerx, self.rect.rect.bottom + 15))
            surface.blit(text_surface, text_rect)
            
        value_font = pygame.font.Font(DEFAULT_DIGIT, 80)
        value_text = value_font.render(f"{int(self.value)}", True, "black")
        value_rect = value_text.get_rect(midtop=(50, self.rect.rect.top + 9))
        surface.blit(value_text, value_rect)

class Touchpad:
    def __init__(self, config):
        self.config = config
        self.device_path = config.get("device", self.autodetect_touch())
        self.width = config.get("width", 800)
        self.height = config.get("height", 480)
        
        self.device = None
        self.running = False
        self.ui_elements = []
        
        # OPTIMIERT: Framerate-Begrenzung
        self.target_fps = 30
        self.frame_time = 1.0 / self.target_fps
        self.last_frame_time = 0
        
        self.touches = {}
        self.slot_states = defaultdict(dict)
        self.current_slot = 0
        
        self.touch_manager = TouchManager()
        
        self.touch_calibration = {
            'raw_x_min': 5,
            'raw_x_max': 792, 
            'raw_y_min': 10,
            'raw_y_max': 468,
            'display_width': self.width,
            'display_height': self.height
        }
        
        self.setup()
        
    def autodetect_touch(self):
        try:
            for device_path in list_devices():
                device = InputDevice(device_path)
                if self.is_multitouch_device(device):
                    return device.path
                elif self.is_touch_device(device):
                    return device.path
        except Exception as e:
            print(f"⚠️  Auto-detect error: {e}")
        return "/dev/input/event5"
    
    def is_multitouch_device(self, device):
        try:
            caps = device.capabilities()
            if ecodes.EV_ABS in caps:
                abs_caps = caps[ecodes.EV_ABS]
                abs_codes = [code for code, abs_info in abs_caps]
                
                has_mt_slot = 47 in abs_codes
                has_mt_tracking_id = 57 in abs_codes
                
                return has_mt_slot and has_mt_tracking_id
                
        except Exception:
            pass
        return False

    def is_touch_device(self, device):
        try:
            caps = device.capabilities()
            
            abs_codes = []
            if ecodes.EV_ABS in caps:
                abs_codes = [code for code, abs_info in caps[ecodes.EV_ABS]]
            
            key_codes = []
            if ecodes.EV_KEY in caps:
                key_codes = caps[ecodes.EV_KEY]
            
            has_abs_x = 0 in abs_codes
            has_abs_y = 1 in abs_codes
            has_btn_touch = 330 in key_codes
            has_mt_position_x = 53 in abs_codes
            has_mt_position_y = 54 in abs_codes
            
            return (has_abs_x and has_abs_y and has_btn_touch) or (has_mt_position_x and has_mt_position_y)
            
        except Exception:
            return False

    def setup(self):
        try:
            import os
            os.environ['DISPLAY'] = ':0'
            os.environ['SDL_VIDEO_FULLSCREEN_DISPLAY'] = '0'
            
            self.device = InputDevice(self.device_path)
            
            if self.is_multitouch_device(self.device):
                self.multitouch = True
                self.ignore_singletouch = True
            else:
                self.multitouch = False
                self.ignore_singletouch = False
                
            self.running = True
            
            pygame.init()
            self.surface = pygame.display.set_mode(
                (self.width, self.height), 
                pygame.FULLSCREEN | pygame.NOFRAME
            )
            pygame.mouse.set_visible(False)
            
        except Exception as e:
            print(f"❌ Setup failed: {e}")
            self.running = False
    
    def listen(self):
        if not self.device or not self.running:
            return
            
        try:
            r, w, x = select([self.device], [], [], 0.001)
            if r:
                for event in self.device.read():
                    self.process_event(event)
                    
                self.update_touch_states()
                    
        except Exception:
            pass
    
    def process_event(self, event):
        if self.multitouch:
            self.process_multitouch_event(event)
        else:
            self.process_singletouch_event(event)

    def process_multitouch_event(self, event):
        if event.type == ecodes.EV_ABS and event.code == 47:
            self.current_slot = event.value
            
        elif event.type == ecodes.EV_ABS and event.code == 57:
            slot = self.current_slot
            if event.value == -1:
                if slot in self.slot_states and 'tracking_id' in self.slot_states[slot]:
                    tracking_id = self.slot_states[slot]['tracking_id']
                    self.handle_touch_up(slot, tracking_id)
                    self.slot_states[slot].clear()
            else:
                self.slot_states[slot]['tracking_id'] = event.value
                self.slot_states[slot]['pressed'] = True
                self.handle_touch_down(slot, event.value)
                
        elif event.type == ecodes.EV_ABS:
            slot = self.current_slot
            if event.code == 53:
                self.slot_states[slot]['x'] = event.value
            elif event.code == 54:
                self.slot_states[slot]['y'] = event.value
                
        elif event.type == ecodes.EV_SYN and event.code == ecodes.SYN_REPORT:
            self.update_multitouch_states()

    def update_multitouch_states(self):
        for slot, state in self.slot_states.items():
            if state.get('pressed') and 'x' in state and 'y' in state:
                tracking_id = state.get('tracking_id', slot)
                x, y = state['x'], state['y']
                
                if not state.get('position_received', False):
                    state['position_received'] = True
                    if not state.get('down_sent', False):
                        state['down_sent'] = True
                        self.handle_touch_down(slot, tracking_id)
                else:
                    self.handle_touch_move(slot, tracking_id, x, y)
    
    def process_singletouch_event(self, event):
        if hasattr(self, 'ignore_singletouch') and self.ignore_singletouch:
            return
            
        if event.type == ecodes.EV_ABS:
            if event.code == ecodes.ABS_X:
                self.slot_states[0]['x'] = event.value
            elif event.code == ecodes.ABS_Y:
                self.slot_states[0]['y'] = event.value
                    
        elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH:
            if event.value == 1:
                self.slot_states[0]['pressed'] = True
                self.slot_states[0]['tracking_id'] = 0
                self.handle_touch_down(0, 0)
            else:
                if 0 in self.slot_states:
                    tracking_id = self.slot_states[0].get('tracking_id', 0)
                    self.handle_touch_up(0, tracking_id)
                    self.slot_states[0].clear()
    
    def update_touch_states(self):
        if not hasattr(self, 'multitouch_type_b') or not self.multitouch_type_b:
            for slot, state in self.slot_states.items():
                if state.get('pressed') and 'x' in state and 'y' in state:
                    tracking_id = state.get('tracking_id', slot)
                    x, y = state['x'], state['y']
                    self.handle_touch_move(slot, tracking_id, x, y)
    
    def calibrate_touch_coordinates(self, raw_x, raw_y):
        raw_x_range = self.touch_calibration['raw_x_max'] - self.touch_calibration['raw_x_min']
        if raw_x_range > 0:
            x_ratio = (raw_x - self.touch_calibration['raw_x_min']) / raw_x_range
            display_x = int(x_ratio * self.touch_calibration['display_width'])
        else:
            display_x = raw_x
            
        raw_y_range = self.touch_calibration['raw_y_max'] - self.touch_calibration['raw_y_min']
        if raw_y_range > 0:
            y_ratio = (raw_y - self.touch_calibration['raw_y_min']) / raw_y_range
            display_y = int(y_ratio * self.touch_calibration['display_height'])
        else:
            display_y = raw_y
            
        display_x = max(0, min(display_x, self.width - 1))
        display_y = max(0, min(display_y, self.height - 1))
        
        return display_x, display_y
    
    def handle_touch_down(self, slot, tracking_id):
        if slot in self.slot_states and 'x' in self.slot_states[slot] and 'y' in self.slot_states[slot]:
            raw_x, raw_y = self.slot_states[slot]['x'], self.slot_states[slot]['y']
            x, y = self.calibrate_touch_coordinates(raw_x, raw_y)
            
            finger_id = tracking_id
            
            touch_data = {
                'finger_id': finger_id,
                'state': TouchState.TOUCH_DOWN,
                'current_x': x,
                'current_y': y,
                'start_x': x,
                'start_y': y,
                'slot': slot,
                'raw_x': raw_x,
                'raw_y': raw_y
            }
            
            self.send_to_ui(touch_data, x, y)

    def handle_touch_up(self, slot, tracking_id):
        if slot in self.slot_states and 'x' in self.slot_states[slot] and 'y' in self.slot_states[slot]:
            raw_x, raw_y = self.slot_states[slot]['x'], self.slot_states[slot]['y']
            x, y = self.calibrate_touch_coordinates(raw_x, raw_y)
            
            finger_id = tracking_id
            
            touch_data = {
                'finger_id': finger_id,
                'state': TouchState.TOUCH_UP,
                'current_x': x,
                'current_y': y,
                'slot': slot,
                'raw_x': raw_x,
                'raw_y': raw_y
            }
            
            self.send_to_ui(touch_data, x, y)

    def handle_touch_move(self, slot, tracking_id, raw_x, raw_y):
        x, y = self.calibrate_touch_coordinates(raw_x, raw_y)
        
        finger_id = tracking_id
        
        touch_data = {
            'finger_id': finger_id,
            'state': TouchState.TOUCH_DRAG,
            'current_x': x,
            'current_y': y,
            'slot': slot,
            'raw_x': raw_x,
            'raw_y': raw_y
        }
        
        self.send_to_ui(touch_data, x, y)
    
    def send_to_ui(self, touch_data, x, y):
        event = TouchEvent(
            finger_id=touch_data['finger_id'],
            x=x, 
            y=y,
            state=touch_data['state'],
            raw_x=touch_data.get('raw_x'),
            raw_y=touch_data.get('raw_y')
        )
        
        element_uid = self.touch_manager.process_touch_event(event)
        
        if element_uid and event.target:
            for element in self.ui_elements:
                if hasattr(element, 'uid') and element.uid == event.target:
                    element.handle_touch_event(event)
                    break
    
    def loop(self):
        for element in self.ui_elements:
            if hasattr(element, 'loop'):
                element.loop()
    
    def render(self):
        current_time = time.time()
        if current_time - self.last_frame_time < self.frame_time:
            return
            
        self.last_frame_time = current_time
        
        if self.running:
            self.surface.fill((0, 0, 0))
            for element in self.ui_elements:
                element.draw(self.surface)
            pygame.display.flip()
    
    def cleanup(self):
        if self.device:
            self.device.close()
        pygame.quit()
    
    def create_test_ui(self):
        button1 = Button(0, 0, 190, 100, "BTN 1")
        button1.on_click = lambda: None
        button1.on_release = lambda: None
        button1.register_touch(self.touch_manager)
        self.ui_elements.append(button1)
        
        button2 = Button(203, 0, 190, 100, "BTN 2") 
        button2.on_click = lambda: None
        button2.on_release = lambda: None
        button2.register_touch(self.touch_manager)
        self.ui_elements.append(button2)
        
        button3 = Button(406, 0, 190, 100, "BTN 3") 
        button3.on_click = lambda: None
        button3.on_release = lambda: None
        button3.register_touch(self.touch_manager)
        self.ui_elements.append(button3)
        
        button4 = Button(609, 0, 191, 100, "BTN 4") 
        button4.on_click = lambda: None
        button4.on_release = lambda: None
        button4.register_touch(self.touch_manager)
        self.ui_elements.append(button4)
        
        # Standardmäßig INCREMENTAL Fader (CPU-freundlich)
        fader1 = Fader(0, 130, 800, 100, "VOL", mode=FaderMode.SMOOTH, overflow=100,smooth_speed=0.1)
        fader1.on_change = lambda v: None
        fader1.register_touch(self.touch_manager)
        self.ui_elements.append(fader1)
        
        fader2 = Fader(0, 260, 800, 100, "PAN", mode=FaderMode.SMOOTH, overflow=100,smooth_speed=0.1)
        fader2.on_change = lambda v: None
        fader2.register_touch(self.touch_manager)
        self.ui_elements.append(fader2)

if __name__ == "__main__":
    config = {
        "device": "/dev/input/event5",
        "width": 800,
        "height": 480
    }
    
    touchpad = Touchpad(config)
    touchpad.create_test_ui()
    
    try:
        while touchpad.running:
            touchpad.listen()
            touchpad.loop()
            touchpad.render()
    except KeyboardInterrupt:
        print("Shutting down...")
    finally:
        touchpad.cleanup()