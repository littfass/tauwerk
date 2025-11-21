import zmq
import json
import threading
import logging
from typing import Callable, Optional

logger = logging.getLogger(__name__)

class MidiBridge:
    def __init__(self):
        self.context = zmq.Context()
        self.socket = None
        self.receiver_thread = None
        self.running = False
        self.callbacks = []
        
    def connect(self, address: str = "ipc:///tmp/tauwerk_midi"):
        """Connect to the C++ MIDI engine"""
        try:
            self.socket = self.context.socket(zmq.PAIR)
            self.socket.connect(address)
            self.running = True
            logger.info("✅ Connected to MIDI engine")
            return True
        except Exception as e:
            logger.error(f"❌ Failed to connect to MIDI engine: {e}")
            return False
        
    def disconnect(self):
        """Disconnect from the MIDI engine"""
        self.running = False
        if self.receiver_thread:
            self.receiver_thread.join()
        if self.socket:
            self.socket.close()
        logger.info("🔌 Disconnected from MIDI engine")
        
    def send_cc(self, channel: int, controller: int, value: int):
        """Send MIDI Control Change message"""
        message = {
            "type": "cc",
            "channel": max(0, min(15, channel)),
            "controller": max(0, min(127, controller)), 
            "value": max(0, min(127, value))
        }
        self._send_message(message)
        logger.debug(f"🎛️  CC ch:{channel} ctrl:{controller} val:{value}")
        
    def send_note_on(self, channel: int, note: int, velocity: int = 100):
        """Send MIDI Note On message"""
        message = {
            "type": "note",
            "channel": max(0, min(15, channel)), 
            "note": max(0, min(127, note)),
            "velocity": max(0, min(127, velocity))
        }
        self._send_message(message)
        logger.debug(f"🎵 Note On ch:{channel} note:{note} vel:{velocity}")
        
    def send_note_off(self, channel: int, note: int):
        """Send MIDI Note Off message"""
        self.send_note_on(channel, note, 0)
        logger.debug(f"🔇 Note Off ch:{channel} note:{note}")
        
    def set_bpm(self, bpm: float):
        """Set BPM"""
        message = {
            "type": "bpm",
            "bpm": max(20.0, min(300.0, bpm))
        }
        self._send_message(message)
        logger.info(f"🎶 BPM set to {bpm}")
        
    def set_clock_mode(self, mode: int):
        """Set clock mode: 0=internal, 1=master, 2=slave"""
        message = {
            "type": "clock_mode", 
            "mode": max(0, min(2, mode))
        }
        self._send_message(message)
        logger.info(f"⏱️  Clock mode set to {mode}")
        
    def start_clock(self):
        """Start MIDI clock"""
        message = {"type": "clock_start"}
        self._send_message(message)
        logger.info("▶️  Clock started")
        
    def stop_clock(self):
        """Stop MIDI clock""" 
        message = {"type": "clock_stop"}
        self._send_message(message)
        logger.info("⏹️  Clock stopped")
        
    def add_callback(self, callback: Callable[[dict], None]):
        """Add callback for received messages"""
        self.callbacks.append(callback)
        logger.debug(f"📨 Added MIDI callback (total: {len(self.callbacks)})")
        
    def start_receiver(self):
        """Start receiving thread"""
        def receiver_loop():
            while self.running:
                try:
                    message_str = self.socket.recv_string(zmq.NOBLOCK)
                    message = json.loads(message_str)
                    
                    for callback in self.callbacks:
                        try:
                            callback(message)
                        except Exception as e:
                            logger.error(f"Callback error: {e}")
                            
                except zmq.Again:
                    pass
                except Exception as e:
                    logger.error(f"Receiver error: {e}")
                    
        self.receiver_thread = threading.Thread(target=receiver_loop, daemon=True)
        self.receiver_thread.start()
        logger.info("📡 Started MIDI message receiver")
        
    def _send_message(self, message: dict):
        """Internal message sender"""
        if self.socket:
            try:
                self.socket.send_string(json.dumps(message))
            except Exception as e:
                logger.error(f"Send error: {e}")
        else:
            logger.warning("⚠️  Not connected to MIDI engine")