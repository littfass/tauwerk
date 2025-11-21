import mmap
import struct
import os
import json
import time
from collections import deque
from luma.core.interface.serial import i2c
from luma.oled.device import sh1106
import smbus2 as smbus
import configparser
from display import Layout

class Hardware:
    def __init__(self):
        self.config = self.load_config()
        self.bridge = None
        self.controllers = []  # Liste aller Controller
        self.displays = {}     # Dictionary aller Displays
    
    def load_config(self):
      config = configparser.ConfigParser()
      config.read('/home/tauwerk/config/hardware.ini')
      return config
        
    def setup(self, app_instance):
      """Komplette Hardware-Setup aus INI Config - GENERISCH"""
      print("🔧 Setting up hardware from INI config...")
      
      # Shared Memory Bridge
      self.bridge = GPIOBridge()
      
      # ✅ GENERISCHE CONTROLLER SETUP
      if 'controllers' in self.config:
          encoder_configs = {}
          button_configs = {}
          
          # Sammle alle Configs gruppiert nach Controller
          for key, value in self.config['controllers'].items():
              parts = key.split('.')
              if len(parts) >= 3:
                  ctrl_name = parts[0]  # z.B. "A", "B", etc.
                  ctrl_type = parts[1]  # "encoder" oder "buttons"
                  ctrl_component = parts[2]  # "select", "push", etc.
                  
                  if ctrl_type == "encoder":
                      if ctrl_name not in encoder_configs:
                          encoder_configs[ctrl_name] = {}
                      encoder_configs[ctrl_name][ctrl_component] = value
                      
                  elif ctrl_type == "buttons":
                      if ctrl_name not in button_configs:
                          button_configs[ctrl_name] = {}
                      button_configs[ctrl_name][ctrl_component] = value
          
          # ✅ ENCODER Setup für alle Controller
          for ctrl_name, encoder_data in encoder_configs.items():
              if 'select' in encoder_data:
                  pins_str = encoder_data['select']
                  pins = [int(pin.strip()) for pin in pins_str.split(',')]
                  print(f"  🎛️  Encoder {ctrl_name}: pins={pins}")
                  encoder = Encoder(
                      id=f"{ctrl_name}_encoder", 
                      pins=pins, 
                      bridge=self.bridge,
                      listeners=[app_instance]
                  )
                  self.controllers.append(encoder)
          
          # ✅ BUTTONS Setup für alle Controller  
          for ctrl_name, buttons_data in button_configs.items():
              if buttons_data:
                  print(f"  🔘 Buttons {ctrl_name}: {buttons_data}")
                  # Konvertiere String-Werte zu Integers
                  button_pins = {name: int(pin) for name, pin in buttons_data.items()}
                  button = Button(
                      id=f"{ctrl_name}_buttons", 
                      pins=button_pins, 
                      bridge=self.bridge,
                      listeners=[app_instance]
                  )
                  self.controllers.append(button)
      
      # ✅ GENERISCHE DISPLAY SETUP
      if 'displays' in self.config:
          display_configs = {}
          
          # Sammle alle Display-Konfigurationen
          for key, value in self.config['displays'].items():
              if '.' in key:
                  display_id, property_name = key.split('.', 1)  # "OUT1.channel" → "OUT1", "channel"
                  if display_id not in display_configs:
                      display_configs[display_id] = {}
                  display_configs[display_id][property_name] = value
          
          # Erstelle Displays für alle gefundenen Konfigurationen
          for display_id, config in display_configs.items():
              try:
                  # Mit Default-Werten falls nicht in INI
                  channel = int(config.get('channel', '0x01'), 16)
                  port = int(config.get('port', '1'))
                  multiplexer_address = int(config.get('multiplexer', '0x70'), 16)
                  display_address = int(config.get('address', '0x3C'), 16)
                  
                  print(f"  📺 Display {display_id}: channel={hex(channel)}, port={port}")
                  
                  display = Display(
                      id=display_id,
                      channel=channel,
                      port=port,
                      multiplexer_address=multiplexer_address,
                      display_address=display_address
                  )
                  self.displays[display_id] = display
                  
              except Exception as e:
                  print(f"  ❌ Fehler beim Display {display_id}: {e}")
    
    def run(self):
        """Pollt alle Hardware-Komponenten"""
        if self.bridge:
            events = self.bridge.read_events()
            if events:
                #print(f"📨 Processing {len(events)} events through {len(self.controllers)} controllers")
                for controller in self.controllers:
                    controller.process_events(events)

        for display in self.displays.values():
            display.draw()
    
    def close(self):
        """Schließt alle Hardware-Komponenten"""
        if self.bridge and self.bridge.mmap:
            self.bridge.mmap.close()

        for display in self.displays.values():
            try:
                print(f"Closing display {display.id}")
                display.select()
                display.device.clear()
                display.device.command(0xAE)  # Display ausschalten
                display.device.contrast(0)
            except Exception as e:
                print(f"Error closing display {display.id}: {e}")

    def write_display(self, display_id, output):
      if display_id in self.displays:
          self.displays[display_id].send(output)
      else:
          print(f"❌ Display {display_id} not found in {list(self.displays.keys())}")

    def merge_display(self, display_id, output):
        """Overlay auf ein bestimmtes Display"""
        if display_id in self.displays:
            self.displays[display_id].overlay(output)

class GPIOBridge:
    def __init__(self, shm_path="/tauwerk_gpio"):
        self.shm_path = f"/dev/shm{shm_path}"
        self.buffer_size = 256
        self.last_read_index = 0
        self.setup_shared_memory()
        
    def setup_shared_memory(self):
        try:
            # Shared Memory öffnen
            self.shm_fd = os.open(self.shm_path, os.O_RDWR)
            # Puffer: 256 Events * 16 Bytes + 16 Bytes für Metadaten
            self.mmap = mmap.mmap(self.shm_fd, 256 * 16 + 16, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
            
            print("✅ Connected to GPIO shared memory")
                
        except Exception as e:
            print(f"❌ Failed to connect to shared memory: {e}")
            self.mmap = None
    
    def get_write_index(self):
        """Liest den aktuellen Write Index vom C++ Treiber"""
        if not self.mmap:
            return 0
            
        try:
            # Write Index ist am Ende des Event-Buffers
            offset = 256 * 16  # 256 Events * 16 Bytes
            write_index_data = self.mmap[offset:offset + 4]
            return struct.unpack('I', write_index_data)[0]  # unsigned int
        except Exception as e:
            print(f"Error reading write index: {e}")
            return 0
    
    def read_events(self):
        if not self.mmap:
            return []
            
        events = []
        try:
            current_write_index = self.get_write_index()
            #print(f"Read index: {self.last_read_index}, Write index: {current_write_index}")
            
            # Nur neue Events lesen (Ringpuffer)
            while self.last_read_index != current_write_index:
                offset = self.last_read_index * 16  # 4 ints * 4 bytes
                event_data = self.mmap[offset:offset + 16]
                
                if len(event_data) == 16:
                    # Event-Struktur: type, pin, value, timestamp (je 4 Bytes)
                    event_type, pin, value, timestamp = struct.unpack('iiii', event_data)
                    
                    #print(f"Raw event - type:{event_type}, pin:{pin}, value:{value}, timestamp:{timestamp}")
                    
                    # Event validieren
                    if event_type in [0, 1] and pin >= 0:
                        event = {
                            'type': 'encoder' if event_type == 0 else 'button',
                            'pin': pin,
                            'value': value,
                            'timestamp': timestamp
                        }
                        events.append(event)
                        #print(f"✅ Valid event: {event}")
                
                # Nächsten Index (Ringpuffer)
                self.last_read_index = (self.last_read_index + 1) % 256
                        
        except Exception as e:
            print(f"Error reading events: {e}")
            
        return events

class Event:
    """Vereinfachte Event-Klasse - kompatibel mit deiner main.py"""
    def __init__(self, id=None, pin=None, name=None, type=None, value=None, direction=None, state=None, tick=None):
        self.id = id
        self.pin = pin
        self.name = name
        self.type = type
        self.direction = direction
        self.value = value
        self.state = state
        self.tick = tick
        self.ENCODER = self.type == "encoder"
        self.BUTTON = self.type == "button"
        self.LEFT = (direction == -1) if direction is not None else None
        self.RIGHT = (direction == 1) if direction is not None else None
        self.PRESS = (state == "PRESSED") if state else None
        self.RELEASE = (state == "RELEASED") if state else None

    def parent(self, name):
        return self.name == name

    def __str__(self):
        return f"Event(id={self.id}, type={self.type}, dir={self.direction}, state={self.state})"

class Controller:
    """Basisklasse für alle Controller"""
    controllers = {}
    
    def __init__(self, id, bridge, listeners=None):
        self.id = id
        self.bridge = bridge
        self.listeners = listeners or []
        self.last_events = deque(maxlen=10)  # Für Debugging
        
        Controller.controllers[id] = self
        print(f"Registered {self.__class__.__name__}: {id}")
    
    def add_listener(self, listener):
        if listener not in self.listeners:
            self.listeners.append(listener)
    
    def process_events(self, events):
        """Muss von Subclasses implementiert werden"""
        pass
    
    def notify_listeners(self, event):
      """Benachrichtigt alle Listener"""
      for i, listener in enumerate(self.listeners):
          if listener is None:
              print(f"❌ Listener {i} is None - skipping")
              continue
              
          if hasattr(listener, 'listen'):
              try:
                  #print(f"Notifying listener {i}: {type(listener).__name__}")
                  listener.listen(event)
              except Exception as e:
                  print(f"Error notifying listener {listener}: {e}")
                  print(f"Listener type: {type(listener)}")
          else:
              print(f"❌ Listener {i} has no 'listen' method: {type(listener)}")
    
    @classmethod
    def run(cls, now=None):
        """Pollt alle Controller"""
        for controller in cls.controllers.values():
            if hasattr(controller, 'bridge'):
                events = controller.bridge.read_events()
                if events:
                    controller.process_events(events)
    
    @classmethod
    def close(cls):
        """Schließt alle Controller"""
        for controller in cls.controllers.values():
            if hasattr(controller, 'bridge') and controller.bridge.mmap:
                controller.bridge.mmap.close()
        cls.controllers.clear()

class Encoder(Controller):
    def __init__(self, id, bridge, pins, listeners=None):
        super().__init__(id, bridge, listeners)
        self.pins = pins  # [pin_a, pin_b]
        self.value = 0
        self.pin_to_name = {pins[0]: "a", pins[1]: "b"}
    
    def process_events(self, events):
        for event_data in events:
            if event_data['type'] == 'encoder' and event_data['pin'] in self.pins:
                # C++ Treiber liefert bereits die direction
                direction = event_data['value']
                self.value += direction
                
                # Event für Listener erstellen
                event = Event(
                    id=self.id,
                    pin=event_data['pin'],
                    name=self.pin_to_name.get(event_data['pin']),
                    type="encoder",
                    direction=direction,
                    value=self.value,
                    tick=event_data['timestamp']
                )
                
                self.last_events.append(event)
                self.notify_listeners(event)
                #print(f"Encoder {self.id}: value={self.value} direction={direction}")

    @staticmethod
    def register(id, pins=None, bridge=None, listeners=None):
        Encoder.controllers[id] = Encoder(id, bridge, pins, listeners)

class Button(Controller):
    def __init__(self, id, bridge, pins, listeners=None):
        super().__init__(id, bridge, listeners)
        self.pins = pins  # {"push": 16, "back": 20, "confirm": 21}
        self.states = {pin: "RELEASED" for pin in pins.values()}
        self.name_to_pin = {name: pin for name, pin in pins.items()}
        self.pin_to_name = {pin: name for name, pin in pins.items()}
        print(f"🔘 Button {id} configured - pins: {self.pins}, pin_to_name: {self.pin_to_name}")
    
    def process_events(self, events):
        #print(f"🔘 Button {self.id} processing {len(events)} events")
        
        for event_data in events:
            #print(f"🔘 Processing event: pin={event_data['pin']}, value={event_data['value']}")
            
            # Prüfe ob dieser Pin zu unseren Buttons gehört
            if event_data['type'] == 'button' and event_data['pin'] in self.pins.values():
                pin = event_data['pin']
                button_name = self.pin_to_name.get(pin, "unknown")
                new_state = "PRESSED" if event_data['value'] == 1 else "RELEASED"
                
                #print(f"🔘 Button match - pin:{pin} -> '{button_name}', new_state:{new_state}")
                
                # State-Änderung?
                old_state = self.states.get(pin)
                if old_state != new_state:
                    #print(f"🔘 ✅ State change: {button_name} {old_state} -> {new_state}")
                    self.states[pin] = new_state
                    
                    # Event für Listener erstellen
                    event = Event(
                        id=self.id,
                        pin=pin,
                        name=button_name,  # Verwende den echten Namen ("back", "confirm", etc.)
                        type="button", 
                        state=new_state,
                        tick=event_data['timestamp']
                    )
                    
                    self.last_events.append(event)
                    self.notify_listeners(event)
                    #print(f"🔘 ✅ Notified listeners for {button_name} {new_state}")
                #else:
                #    print(f"🔘 ⏭️  No state change for {button_name} (still {old_state})")
            #else:
            #    print(f"🔘 ❌ Event ignored - pin {event_data['pin']} not in our pins {list(self.pins.values())}")

    @staticmethod
    def register(id, pins=None, bridge=None, listeners=None):
        Button.controllers[id] = Button(id, bridge, pins, listeners)

class Display:
    last_channel = None
    devices = {}

    def __init__(self, id="display", channel=0x01, port=1, multiplexer_address=0x70, display_address=0x3C):
        self.id = id
        self.ready = False
        self.channel = channel
        self.port = port
        self.multiplexer_address = multiplexer_address
        self.display_address = display_address
        self.output = Layout(id)   
        self.setup()

    def setup(self):
        try:
            # I2C Bus initialisieren
            if self.port not in Display.devices:
                print(f"Initializing I2C bus on port {self.port}")
                Display.devices[self.port] = smbus.SMBus(self.port)
            
            # Multiplexer channel selektieren
            self.select()
            
            # Display initialisieren
            print(f"Initializing OLED display at address {hex(self.display_address)}")
            self.serial = i2c(port=self.port, address=self.display_address)
            self.device = sh1106(self.serial, width=128, height=64)
            self.device.contrast(100)
            self.device.clear()
            self.ready = True
            
            print(f"✅ Display {self.id} initialized successfully")
            
        except Exception as e:
            print(f"❌ Failed to initialize display {self.id}: {e}")
            self.ready = False

    @property
    def bus(self):
        return Display.devices.get(self.port)

    def select(self):
        if Display.last_channel != self.channel:
            try:
                #print(f"Selecting channel {hex(self.channel)} on multiplexer")
                self.bus.write_byte(self.multiplexer_address, self.channel)
                Display.last_channel = self.channel
                time.sleep(0.01)  # Kurze Pause für Multiplexer
            except Exception as e:
                print(f"Error selecting channel {hex(self.channel)}: {e}")

    def send(self, items):
        if items and self.ready:
            self.output.update(items)

    def overlay(self, items):
        if items and self.ready:
            self.output.overlay(items)
 
    def draw(self):
      """Zeichnet nur wenn nötig - mit Debug-Logs"""
      if self.ready:
          #print(f"🔍 Display {self.id}: has_changed={self.output.has_changed}, items={len(self.output.items)}")
          
          if self.output.has_changed:
              try:
                  #print(f"🎨 Drawing to display {self.id}...")
                  self.select()
                  self.output.draw(self.device)
                  #print(f"✅ Display {self.id} drawn successfully")
              except Exception as e:
                  print(f"❌ Error drawing to display {self.id}: {e}")
          #else:
          #    print(f"⏭️  Display {self.id} skipped - no changes")
      else:
          print(f"🚫 Display {self.id} not ready")