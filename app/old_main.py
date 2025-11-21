#!/usr/bin/env python3
"""
Tauwerk - Hauptanwendung (PI5 mit gpiod)
"""

import os
# 🎵 AUDIO KOMPLETT DEAKTIVIEREN
os.environ['SDL_AUDIODRIVER'] = 'dummy'
os.environ['PYGAME_DISABLE_AUDIO'] = '1' 
os.environ['AUDIODRIVER'] = 'null'
os.environ['ALSA_CARD'] = '0'  # Erste verfügbare Karte

# 🎵 PYGAME IMPORT MIT AUDIO-DISABLE
import pygame
pygame.mixer.quit()
pygame.mixer.init(frequency=22050, size=-16, channels=2, buffer=0)

import time
import signal
import logging
from tauwerk.hardware.device import Hardware, Button, Controller, Display
from tauwerk.hardware.touchpad import Touchpad
from tauwerk.display.layout import Icon, Font, EyeIcon

# Logging konfigurieren
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Deine bestehenden Klassen unverändert übernehmen
class Foo:
    def __init__(self):
        self.name = "Foo"

    def draw(self, draw, item, x, y):
        draw.text((0, 10), f"{self.name}", fill="white", font=Font.get("small"))

class Bar:
    def __init__(self):
        self.name = "Bar"

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
        self.foo = Foo()
        self.cpu = Bar()
        self.hardware = Hardware()
        self.touchpad = None  # Wird in setup() initialisiert
        self.setup()
        self.dispatch()

    def setup(self):
        signal.signal(signal.SIGTERM, self._handle_signal)
        signal.signal(signal.SIGINT, self._handle_signal)
        
        self.hardware.setup(self)
        self.initialize_displays()
        
        touchpad_config = self.hardware.config.get("touchpads", {}).get("TOUCHPAD1", {})
        if touchpad_config:
            self.touchpad = Touchpad(touchpad_config)
            self.touchpad.create_test_ui()  # ❌ Erstmal deaktivieren
            #self.touchpad.create_debug_ui()   # ✅ Debug UI verwenden
            logger.info("✅ Touchpad initialized with DEBUG UI")
        else:
            logger.warning("⚠️ No touchpad configuration found")

    def initialize_displays(self):
        """Initialisiert die Displays mit Startwerten"""
        logger.info("🖥️  Initializing displays...")
        
        # Erzwinge erste Anzeige
        self.hardware.write_display("OUT1", self.template_main())
        self.hardware.write_display("OUT2", self.template_track())
        
        # Zeichne sofort
        for display in self.hardware.displays.values():
            display.draw()
        
        logger.info("✅ Displays initialized")

    def _handle_signal(self, signum, frame):
        logger.info(f"📡 Signal {signum} empfangen, beende Anwendung...")
        self.running = False

    def listen(self, event):
        logger.debug(f"Event received: {event}")
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
                {"icon":self.icon, "text":self.label, "x":0, "y":0, "space": 4, "fill":"white","font":Font.get("small"), "align":"left", "valign":"top"},
                {"progressbar":{"x": 0, "y":0, "value": self.main_value, "min":0, "max":127, "width":16, "height":6,  "fill":"white", "background": "black", "align":"right"}},
                {"y":22, "text":self.main_value, "fill":"white", "font":Font.get("digit"), "align":"center"},
                {"y":20, "element":self.foo}
            ]

    def template_track(self):
        return [
            {"icon":self.icon, "text":self.label, "x":0, "y":0, "space": 4, "fill":"white","font":Font.get("small"), "align":"left", "valign":"top"},
            {"progressbar":{"x": 0, "y":0, "value": self.track_value, "min":0, "max":127, "width":16, "height":6,  "fill":"white", "background": "black", "align":"right"}},
            {"text":self.track_value, "x": 0, "y":22, "fill":"white", "font":Font.get("digit"), "align":"center"}
        ]

    def onbutton(self, event):
        if event.BUTTON:
            if event.name == "back":
                if event.PRESS:
                    logger.info(f"MODES: {self.modes}")
                    self.mode += 1
                    if self.mode > len(self.modes) - 1:
                        self.mode = 0
                    logger.info(f"🎯 Mode changed to: {self.modes[self.mode]}")
                    #self.mode = (self.mode + 1) % len(self.modes)
                    if self.modes[self.mode] == "MAIN":
                        self.icon = Icon.SAMPLE
                    elif self.modes[self.mode] == "TRACK":
                        self.icon = Icon.STEP
                    elif self.modes[self.mode] == "CREATURE":
                        self.icon = Icon.EYE
                    else:
                        self.icon = Icon.STEP

            if event.name == "confirm":
                if event.PRESS:
                    self.icon = Icon.OK
                    if self.modes[self.mode] == "MAIN":
                        self.main_value = 0 if self.main_value > 0 else 127
                    elif self.modes[self.mode] == "TRACK":
                        self.track_value = 0 if self.track_value > 0 else 127
                    elif self.modes[self.mode] == "CREATURE":
                        self.creature_value = 100 if self.creature_value == 0 else 0
                elif event.RELEASE:
                    if self.modes[self.mode] == "MAIN":
                        self.icon = Icon.SAMPLE
                    elif self.modes[self.mode] == "TRACK":
                        self.icon = Icon.STEP
                    elif self.modes[self.mode] == "CREATURE":
                        self.icon = Icon.EYE
                    else:
                        self.icon = Icon.STEP

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

        self.hardware.write_display("OUT1", self.template_main())
        self.hardware.write_display("OUT2", self.template_track())

    def close(self):
        if self.touchpad:
            self.touchpad.cleanup()
        self.hardware.close()
        logger.info(f"🛑 Closing {self.name}")

    def run(self):
        logger.info(f"🚀 Starting {self.name}")
        try:
            while self.running:
                #now = time.time()

                if self.touchpad:
                    self.touchpad.listen()
                    self.touchpad.loop()
                    self.touchpad.render()

                self.hardware.run()
                time.sleep(0.01)
                        
        except KeyboardInterrupt:
            self.close()
        

def main():
    app = Tauwerk()
    app.run()

if __name__ == "__main__":
    main()