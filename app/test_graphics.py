#!/usr/bin/env python3
import mmap
import struct
import time
import os

def send_graphics_command():
    try:
        # Shared Memory öffnen
        shm_fd = os.open('/dev/shm/tauwerk_graphics', os.O_RDWR)
        
        # Command Buffer mappen
        buffer_size = 256
        command_size = 80  # sizeof(GraphicsCommand) - 6*int + 64*char + 1*int = 6*4 + 64 + 4 = 24 + 64 + 4 = 92? 
        # Korrektur: Wir testen verschiedene Größen
        command_size = 92  # Versuchen wir 92 Bytes
        
        total_size = buffer_size * command_size + 12  # + control data
        
        shm = mmap.mmap(shm_fd, total_size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        
        # Control data lesen (BEVOR wir schreiben)
        control_offset = buffer_size * command_size
        write_index = struct.unpack('i', shm[control_offset:control_offset+4])[0]
        read_index = struct.unpack('i', shm[control_offset+4:control_offset+8])[0]
        magic = struct.unpack('i', shm[control_offset+8:control_offset+12])[0]
        
        print(f"📊 BEFORE: write={write_index}, read={read_index}, magic={hex(magic)}")
        print(f"📏 Command size: {command_size} bytes")
        
        # TEST 1: Roten Bildschirm
        print("🎨 Drawing RED screen...")
        # 6 ints (4 bytes each) + 64 chars + 1 int = 24 + 64 + 4 = 92 bytes
        cmd_bytes = struct.pack('iiiiii64si', 0, 0, 0, 0, 0, 0xFF0000, b"\0"*64, 0)
        
        print(f"📦 Command bytes length: {len(cmd_bytes)}")
        
        # Command schreiben
        cmd_offset = write_index * command_size
        shm[cmd_offset:cmd_offset+len(cmd_bytes)] = cmd_bytes
        
        # Write index updaten UND ZURÜCK SCHREIBEN
        new_write = (write_index + 1) % buffer_size
        shm[control_offset:control_offset+4] = struct.pack('i', new_write)
        print(f"📝 Wrote CLEAR command at index {write_index}, new_write={new_write}")
        
        # SOFORT LESEN ZUM VERIFIZIEREN
        verify_write = struct.unpack('i', shm[control_offset:control_offset+4])[0]
        print(f"✅ VERIFY: write_index is now {verify_write}")
        
        time.sleep(2)
        
        # TEST 2: Grünes Rechteck
        print("🎨 Drawing GREEN rectangle...")
        cmd_bytes = struct.pack('iiiiii64si', 1, 100, 100, 200, 150, 0x00FF00, b"\0"*64, 0)
        
        cmd_offset = new_write * command_size
        shm[cmd_offset:cmd_offset+len(cmd_bytes)] = cmd_bytes
        
        new_write = (new_write + 1) % buffer_size
        shm[control_offset:control_offset+4] = struct.pack('i', new_write)
        print(f"📝 Wrote RECT command at index {new_write-1}, new_write={new_write}")
        
        time.sleep(2)
        
        # TEST 3: Weißen Text
        print("🎨 Drawing WHITE text...")
        text_bytes = b"TAUWERK" + b"\0" * (64 - 7)  # 64 bytes total
        cmd_bytes = struct.pack('iiiiii64si', 2, 50, 50, 0, 0, 0xFFFFFF, text_bytes, 0)
        
        cmd_offset = new_write * command_size
        shm[cmd_offset:cmd_offset+len(cmd_bytes)] = cmd_bytes
        
        new_write = (new_write + 1) % buffer_size
        shm[control_offset:control_offset+4] = struct.pack('i', new_write)
        print(f"📝 Wrote TEXT command at index {new_write-1}, new_write={new_write}")
        
        # FINAL VERIFICATION
        final_write = struct.unpack('i', shm[control_offset:control_offset+4])[0]
        final_read = struct.unpack('i', shm[control_offset+4:control_offset+8])[0]
        print(f"📊 FINAL: write={final_write}, read={final_read}")
        
        print("✅ Test commands sent! Check C++ driver output...")
        
        time.sleep(5)
        
        shm.close()
        os.close(shm_fd)
        
    except Exception as e:
        print(f"❌ Test failed: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    send_graphics_command()