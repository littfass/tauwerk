#!/usr/bin/env python3
"""
Tauwerk UI Bridge - Shared Memory Kommunikation zwischen Python und C++
"""

import mmap
import struct
import time
from typing import List, Optional
from ctypes import Structure, c_int, c_char, c_bool, sizeof
from enum import Enum

class UIElementType(Enum):
    BUTTON = 0
    FADER = 1
    LABEL = 2
    TOGGLE = 3

class UIEventType(Enum):
    BUTTON_DOWN = 0
    BUTTON_UP = 1
    FADER_CHANGE = 2
    TOUCH_MOVE = 3

class PythonCommand(Structure):
    _fields_ = [
        ('type', c_int),        # 0=CREATE, 1=UPDATE, 2=DELETE, 3=SHOW, 4=HIDE
        ('id', c_int),
        ('element_type', c_int), # BUTTON, FADER, etc.
        ('x', c_int),
        ('y', c_int), 
        ('width', c_int),
        ('height', c_int),
        ('value', c_int),
        ('visible', c_bool),
        ('text', c_char * 64),
        ('color', c_int)
    ]

class UIEvent(Structure):
    _fields_ = [
        ('type', c_int),    # 0=BUTTON_DOWN, 1=BUTTON_UP, 2=FADER_CHANGE
        ('id', c_int),
        ('value', c_int),
        ('timestamp', c_int)
    ]

class TauwerkUIBridge:
    def __init__(self):
        self.command_shm = None
        self.event_shm = None
        self.command_buffer = None
        self.event_buffer = None
        self.buffer_size = 256
        self.command_write_index = 0
        self.event_read_index = 0
        
    def connect(self):
        """Verbinde mit C++ UI Treiber"""
        try:
            # Command Buffer (Python → C++)
            cmd_shm_fd = open("/dev/shm/tauwerk_ui_commands", "r+b")
            self.command_shm = mmap.mmap(cmd_shm_fd.fileno(), 0)
            
            # Event Buffer (C++ → Python)  
            event_shm_fd = open("/dev/shm/tauwerk_ui_events", "r+b") 
            self.event_shm = mmap.mmap(event_shm_fd.fileno(), 0)
            
            # Buffer als Arrays von Structures mappen
            self.command_buffer = (PythonCommand * self.buffer_size).from_buffer(self.command_shm)
            self.event_buffer = (UIEvent * self.buffer_size).from_buffer(self.event_shm)
            
            print("✅ Connected to C++ UI Driver")
            return True
            
        except Exception as e:
            print(f"❌ UI Bridge connection failed: {e}")
            return False
    
    def create_button(self, element_id: int, x: int, y: int, width: int, height: int, 
                     text: str = "", color: int = 0x5A825A):
        """Erstelle Button in C++ UI"""
        cmd = PythonCommand()
        cmd.type = 0  # CREATE
        cmd.id = element_id
        cmd.element_type = UIElementType.BUTTON.value
        cmd.x = x
        cmd.y = y
        cmd.width = width
        cmd.height = height
        cmd.value = 0
        cmd.text = text.encode('utf-8')
        cmd.color = color
        cmd.visible = True
        
        self._send_command(cmd)
        print(f"📝 Created button: {text} (ID:{element_id})")
    
    def create_fader(self, element_id: int, x: int, y: int, width: int, height: int,
                    text: str = "", value: int = 50, color: int = 0x5A825A,
                    mode: str = "smooth", smooth_speed: float = 0.1):
      """Erstelle Fader in C++ UI mit Smooth Mode Support"""
      cmd = PythonCommand()
      cmd.type = 0  # CREATE
      cmd.id = element_id
      cmd.element_type = UIElementType.FADER.value
      cmd.x = x
      cmd.y = y
      cmd.width = width
      cmd.height = height
      cmd.value = value
      cmd.visible = True
      
      # ✅ SMART: Text-Feld für Mode + Speed nutzen
      # Format: "text|mode|speed" 
      config_text = f"{text}|{mode}|{smooth_speed}"
      cmd.text = config_text.encode('utf-8')[:63]  # Buffer limit
      
      cmd.color = color
      
      self._send_command(cmd)
      print(f"📝 Created fader: {text} (ID:{element_id}, Mode:{mode})")
    
    def create_label(self, element_id: int, x: int, y: int, text: str = "", 
                    color: int = 0xFFFFFF):
        """Erstelle Label in C++ UI"""
        cmd = PythonCommand()
        cmd.type = 0  # CREATE
        cmd.id = element_id
        cmd.element_type = UIElementType.LABEL.value
        cmd.x = x
        cmd.y = y
        cmd.width = len(text) * 8  # Geschätzte Breite
        cmd.height = 16
        cmd.value = 0
        cmd.text = text.encode('utf-8')
        cmd.color = color
        cmd.visible = True
        
        self._send_command(cmd)
        print(f"📝 Created label: {text} (ID:{element_id})")
    
    def update_element(self, element_id: int, value: Optional[int] = None, 
                      text: Optional[str] = None, visible: Optional[bool] = None):
        """Update UI Element in C++"""
        cmd = PythonCommand()
        cmd.type = 1  # UPDATE
        cmd.id = element_id
        
        if value is not None:
            cmd.value = value
        if text is not None:
            cmd.text = text.encode('utf-8')
        if visible is not None:
            cmd.visible = visible
            
        self._send_command(cmd)
        print(f"📝 Updated element {element_id}: value={value}, text={text}")
    
    def delete_element(self, element_id: int):
        """Lösche UI Element"""
        cmd = PythonCommand()
        cmd.type = 2  # DELETE
        cmd.id = element_id
        self._send_command(cmd)
        print(f"🗑️ Deleted element: {element_id}")
    
    def hide_element(self, element_id: int):
        """Verstecke UI Element"""
        cmd = PythonCommand()
        cmd.type = 4  # HIDE
        cmd.id = element_id
        self._send_command(cmd)
        print(f"👻 Hidden element: {element_id}")
    
    def show_element(self, element_id: int):
        """Zeige UI Element"""
        cmd = PythonCommand()
        cmd.type = 3  # SHOW  
        cmd.id = element_id
        self._send_command(cmd)
        print(f"👀 Shown element: {element_id}")
    
    def _send_command(self, command: PythonCommand):
        """Sende Command an C++ Treiber"""
        if not self.command_buffer:
            return
            
        # Schreibe Command in Buffer
        self.command_buffer[self.command_write_index] = command
        
        # Update write index in Shared Memory
        self.command_write_index = (self.command_write_index + 1) % self.buffer_size
        
        # Größe der PythonCommand Structure berechnen
        command_size = sizeof(PythonCommand)
        control_offset = self.buffer_size * command_size
        
        self.command_shm[control_offset:control_offset+4] = struct.pack('I', self.command_write_index)
    
    def get_events(self) -> List[UIEvent]:
        """Lese Events von C++ Treiber"""
        events = []
        if not self.event_buffer:
            return events
            
        # Lese control data
        event_size = sizeof(UIEvent)
        control_offset = self.buffer_size * event_size
        write_idx = struct.unpack('I', self.event_shm[control_offset:control_offset+4])[0]
        
        while self.event_read_index != write_idx:
            event = self.event_buffer[self.event_read_index]
            events.append(event)
            self.event_read_index = (self.event_read_index + 1) % self.buffer_size
        
        # Update read index in Shared Memory
        self.event_shm[control_offset+4:control_offset+8] = struct.pack('I', self.event_read_index)
        
        return events
    
    def close(self):
        """Schließe Verbindung"""
        if self.command_shm:
            self.command_shm.close()
        if self.event_shm:
            self.event_shm.close()
        print("🔌 UI Bridge disconnected")

# Test der Bridge
if __name__ == "__main__":
    bridge = TauwerkUIBridge()
    
    if bridge.connect():
        # Erstelle Test-UI
        bridge.create_button(1, 50, 50, 100, 60, "PLAY", 0x5A825A)
        bridge.create_button(2, 170, 50, 100, 60, "STOP", 0x825A5A)
        bridge.create_fader(3, 50, 150, 700, 40, "VOLUME", 75, 0x5A825A)
        bridge.create_fader(4, 50, 220, 700, 40, "BPM", 120, 0x5A5A82)
        bridge.create_label(5, 320, 10, "TAUWERK", 0xFFFFFF)
        
        print("🚀 Test UI sent to C++ - checking for events...")
        print("Press Ctrl+C to stop")
        
        try:
            while True:
                events = bridge.get_events()
                for event in events:
                    event_type = UIEventType(event.type)
                    print(f"🎯 UI Event: {event_type.name} (ID:{event.id}, Value:{event.value})")
                
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
        
        bridge.close()