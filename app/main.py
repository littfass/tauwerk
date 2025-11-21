#!/usr/bin/env python3
import time
import signal
import configparser

from hardware import Hardware, Event
from touchpad import Touchpad
from display import Icon, Font, EyeIcon
from ui import TauwerkUIBridge, UIEventType  # NEU

class Tauwerk:
    def __init__(self):
        self.running = True
        self.name = "TAUWERK"
        self.modes = ["MAIN","TRACK","CREATURE"]
        self.mode = 0
        self.main_value = 0
        self.track_value = 0
        self.creature_value = 0
        self.creature_size = 50
        self.eye_icon = EyeIcon(min_radius=self.creature_size/2, max_radius=self.creature_size/2)
        self.icon = Icon.SAMPLE
        self.hardware = Hardware()
        
        # NEU: C++ UI Bridge
        self.ui_bridge = TauwerkUIBridge()
        self.setup_cpp_ui()
        
        self.setup()
        self.dispatch()

    def setup_cpp_ui(self):
        """Setup C++ UI mit Tauwerk Elementen"""
        if not self.ui_bridge.connect():
            print("⚠️  C++ UI nicht verfügbar - fallback zu OLEDs")
            return
            
        # Erstelle Haupt-UI in C++
        self.ui_bridge.create_button(1, 0, 0, 195, 120, "PLAY", 0xCCEEEC)
        self.ui_bridge.create_button(2, 205, 0, 195, 120, "STOP", 0xCCEEEC)
        self.ui_bridge.create_button(5, 410, 0, 195, 120, "REC", 0xCCEEEC)
        self.ui_bridge.create_button(6, 615, 0, 185, 120, "EDIT", 0xCCEEEC)
        self.ui_bridge.create_fader(3, 0, 130, 800, 120, "VOLUME", 50, 0xCCEEEC, "smooth",0.1)
        self.ui_bridge.create_fader(4, 0, 260, 800, 120, "BPM", 120, 0xCCEEEC)
        self.ui_bridge.create_label(7, 0, 390, "TAUWERK", 0xCCEEEC)
        
        print("✅ C++ UI initialized")

    def handle_ui_events(self):
      """Verarbeite Events von C++ UI - nur bei echten Änderungen"""
      events = self.ui_bridge.get_events()
      for event in events:
          if event.type == UIEventType.BUTTON_DOWN.value:
              # Buttons sofort verarbeiten
              if event.id == 1:  # PLAY
                  print("▶️ PLAY pressed")
              elif event.id == 2:  # STOP
                  print("⏹️ STOP pressed")
                  
          elif event.type == UIEventType.BUTTON_UP.value:
              # Buttons sofort verarbeiten  
              if event.id == 1:  # PLAY
                  print("⏸️ PLAY released")
                  
          elif event.type == UIEventType.FADER_CHANGE.value:
              # Fader Werte direkt zuweisen (kein print im hot path!)
              if event.id == 3:  # VOLUME
                  self.main_value = event.value
              elif event.id == 4:  # BPM
                  self.track_value = event.value

    def setup(self):
        signal.signal(signal.SIGTERM, self._handle_signal)
        signal.signal(signal.SIGINT, self._handle_signal)
        
        self.hardware.setup(self)
        self.initialize_displays()

    def initialize_displays(self):
        print("🖥️  Initializing displays...")
        self.hardware.write_display("out1", self.template_main())
        self.hardware.write_display("out2", self.template_track())
        for display in self.hardware.displays.values():
            display.draw()
        print("✅ Displays initialized")

    def _handle_signal(self, signum, frame):
        print(f"📡 Signal {signum} empfangen, beende Anwendung...")
        self.running = False

    def listen(self, event):
      self.dispatch(event)

    @property
    def label(self):
      return self.modes[self.mode]

    def template_main(self):
        if self.modes[self.mode] == "CREATURE":
            self.eye_icon.update(value=self.creature_value, size=self.creature_size)
            return [{
                "uid": "cpu_eye",
                "element": self.eye_icon,
                "x": 40,
                "y": 0,
                "value": self.creature_value,
                "size": self.creature_size,
                "dither": True
            }]
        elif self.modes[self.mode] == "MAIN":
            return [
                {"icon":self.icon, "text":self.label, "x": 0, "y": 0, "space": 4, "fill":"white","font":Font.get("small"), "align":"left", "valign":"top"},
                {"progressbar":{"x": 0, "y": 0, "value": self.main_value, "min": 0, "max": 127, "width": 16, "height":6, "fill":"white", "background": "black", "align":"right"}},
                {"y": 22, "text": self.main_value, "fill": "white", "font": Font.get("digit"), "align": "center"}
            ]

    def template_track(self):
        return [
            {"icon":self.icon, "text":self.label, "x":0, "y":0, "space": 4, "fill":"white","font":Font.get("small"), "align":"left", "valign":"top"},
            {"progressbar":{"x": 0, "y": 0, "value": self.track_value, "min": 0, "max": 127, "width": 16, "height": 6, "fill":"white", "background": "black", "align":"right"}},
            {"x": 0, "y": 22, "text": self.track_value, "fill": "white", "font": Font.get("digit"), "align": "center"}
        ]

    def onbutton(self, event):
        if event.BUTTON:
            if event.name == "back":
                if event.PRESS:
                    self.mode = (self.mode + 1) % len(self.modes)
                    if self.label == "MAIN":
                        self.icon = Icon.SAMPLE
                    elif self.label == "TRACK":
                        self.icon = Icon.SAMPLE
                    elif self.label == "CREATURE":
                        self.icon = Icon.EYE
                    else:
                        self.icon = Icon.SAMPLE

            if event.name == "confirm":
                if event.PRESS:
                    if self.label == "MAIN":
                        self.main_value = 0 if self.main_value > 0 else 127
                    elif self.label == "TRACK":
                        self.track_value = 0 if self.track_value > 0 else 127
                    elif self.label == "CREATURE":
                        self.creature_value = 100 if self.creature_value == 0 else 0

    def onencoder(self, event=None):
        if event.ENCODER:
            if self.label == "MAIN":
                self.main_value += event.direction
            elif self.label == "TRACK":
                self.track_value += event.direction
            elif self.label == "CREATURE":
                self.creature_value += event.direction * 10
    
    def dispatch(self, event=None):
        if event:
            if event.ENCODER:
                self.onencoder(event)
            elif event.BUTTON:
                self.onbutton(event)

        self.hardware.write_display("out1", self.template_main())
        self.hardware.write_display("out2", self.template_track())

    def close(self):
        if hasattr(self, 'ui_bridge'):
            self.ui_bridge.close()
        self.hardware.close()
        print(f"🛑 Closing {self.name}")

    def run(self):
        print(f"🚀 Starting {self.name}")
        try:
            while self.running:
                self.hardware.run()
                self.handle_ui_events()  # NEU: C++ UI Events verarbeiten
                time.sleep(0.001)  # 100 FPS
                        
        except KeyboardInterrupt:
            self.close()

if __name__ == "__main__":
    app = Tauwerk()
    app.run()