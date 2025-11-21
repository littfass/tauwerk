<div align="center">
  <img src="tauwerk.svg" alt="Tauwerk Logo" width="300">
  
  # Tauwerk MIDI Sequencer
</div>

Low-latency MIDI sequencer for Raspberry Pi 5 with hardware control interface.

## Features

- **C++ Performance Layer**
  - GPIO driver with 1kHz polling rate
  - DRM/KMS + OpenGL ES 2.0 touch UI
  - Shared memory IPC for minimal latency
  
- **Python Application Layer**
  - Hardware abstraction via INI configuration
  - OLED display management (I2C multiplexer)
  - MIDI integration

- **Hardware Support**
  - 7" DSI touchscreen with GPU-accelerated UI
  - Multiple rotary encoders via GPIO
  - Multiple OLED displays via I2C
  - MIDI I/O via Blokaslabs Pimidi

## Quick Start

```bash
# Build C++ components
make

# Start all services
./start.sh

# Stop all services
./stop.sh
```

## Architecture

```
┌─────────────────┐     Shared Memory      ┌──────────────────┐
│  C++ GPIO       │────────────────────────▶│  Python App      │
│  Driver (1kHz)  │     /tauwerk_gpio      │  (main.py)       │
└─────────────────┘                         └──────────────────┘
                                                      │
┌─────────────────┐     Shared Memory               │
│  C++ Touch UI   │────────────────────────────────│
│  (OpenGL ES)    │     /tauwerk_ui_commands        │
└─────────────────┘                                  │
                                                      ▼
                                            ┌──────────────────┐
                                            │  MIDI Engine     │
                                            │  (planned)       │
                                            └──────────────────┘
```

## Hardware Configuration

Edit `config/hardware.ini` to configure GPIO pins and I2C addresses:

```ini
[controllers]
a.encoder.select = 4,12
a.buttons.push = 16
a.buttons.back = 20

[displays]
out1.channel = 0x01
out1.multiplexer = 0x70
out1.address = 0x3C
```

## Development

- **Language**: All comments in German
- **Indentation**: 2 spaces
- **Philosophy**: "Make it work, make it fast, make it readable - in that order!"

## System Requirements

- Raspberry Pi 5 (8GB RAM)
- Debian 13 (trixie)
- Kernel 6.12.47+rpt-rpi-2712

## License

MIT
