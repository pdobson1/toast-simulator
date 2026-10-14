#!/usr/bin/env python3
"""Runs the ROM headless in the Genesis Plus GX libretro core, for testing.

  python3 tools/run_rom.py CORE.so toast.bin SCRIPT

SCRIPT is a list of steps separated by spaces:
  wait:N         run N frames
  press:BTN:N    hold a button (a, b, c, start, up, down, left, right) for N frames
  shot:FILE      save the current frame as a PNG
  until:SYM:N    run until the 32-bit variable SYM reaches N (at most 20 seconds)
  wav:FILE       save all the sound so far as a WAV file
  poke:SYM:OFF:HEX  write bytes into a variable in work RAM, at a byte offset (for setting up tests)
"""
import ctypes as C, sys
from PIL import Image

core_path, rom_path, script = sys.argv[1], sys.argv[2], sys.argv[3:]
core = C.CDLL(core_path)

frame = {'img': None}
held = set()
# libretro joypad ids, and the Mega Drive buttons Genesis Plus GX maps them to
IDS = {'b': 0, 'start': 3, 'up': 4, 'down': 5, 'left': 6, 'right': 7, 'c': 8, 'a': 1}

ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
STATE = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

sysdir = C.c_char_p(b'/tmp')

@ENV
def env(cmd, data):
    if cmd in (9, 31):  # system and save directories
        C.cast(data, C.POINTER(C.c_char_p))[0] = sysdir.value
        return True
    if cmd == 10:  # pixel format: accept what the core asks for
        return True
    return False

@VIDEO
def video(data, w, h, pitch):
    if not data: return
    buf = C.string_at(data, pitch * h)
    img = Image.frombuffer('RGB', (w, h), buf, 'raw', 'BGR;16', pitch, 1)
    frame['img'] = img

@AUDIO
def audio(l, r): pass

sound = bytearray()

@AUDIO_BATCH
def audio_batch(data, n):
    sound.extend(C.string_at(data, n * 4))
    return n

@POLL
def poll(): pass

@STATE
def state(port, device, index, id_):
    return 1 if port == 0 and device == 1 and any(IDS[b] == id_ for b in held) else 0

callbacks = [env, video, audio, audio_batch, poll, state]  # keep them alive
core.retro_set_environment(env)
core.retro_set_video_refresh(video)
core.retro_set_audio_sample(audio)
core.retro_set_audio_sample_batch(audio_batch)
core.retro_set_input_poll(poll)
core.retro_set_input_state(state)
core.retro_init()

class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p), ('size', C.c_size_t), ('meta', C.c_char_p)]

rom = open(rom_path, 'rb').read()
rom_buf = C.create_string_buffer(rom, len(rom))
info = GameInfo(rom_path.encode(), C.cast(rom_buf, C.c_void_p), len(rom), None)
core.retro_load_game.restype = C.c_bool
assert core.retro_load_game(C.byref(info)), 'the core refused the ROM'

def run(n):
    for _ in range(n): core.retro_run()

import subprocess
syms = {}
for line in subprocess.run(['m68k-linux-gnu-nm', 'build/toast.elf'], capture_output=True, text=True).stdout.splitlines():
    a, _, n = line.split()
    syms[n] = int(a, 16)
core.retro_get_memory_data.restype = C.c_void_p
core.retro_get_memory_size.restype = C.c_size_t

def peek(name, size):
    ram = C.string_at(core.retro_get_memory_data(2), core.retro_get_memory_size(2))
    off = syms[name] - 0xFF0000
    return bytes(ram[(off + i) ^ 1] for i in range(size))  # Genesis Plus GX keeps RAM byte-swapped in words

for step in script:
    parts = step.split(':')
    if parts[0] == 'wait': run(int(parts[1]))
    elif parts[0] == 'press':
        held.add(parts[1]); run(int(parts[2])); held.discard(parts[1])
    elif parts[0] == 'shot':
        img = frame['img']
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(parts[1])
        print('saved', parts[1], img.size)
    elif parts[0] == 'until':
        for _ in range(1200):
            if int.from_bytes(peek(parts[1], 4), 'big', signed=True) >= int(parts[2]): break
            run(1)
    elif parts[0] == 'poke':
        base = C.cast(core.retro_get_memory_data(2), C.POINTER(C.c_ubyte))
        off = syms[parts[1]] - 0xFF0000 + int(parts[2])
        for i, byte in enumerate(bytes.fromhex(parts[3])): base[(off + i) ^ 1] = byte
    elif parts[0] == 'wav':
        import wave
        with wave.open(parts[1], 'wb') as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(bytes(sound))
        print('saved', parts[1], len(sound) // 4, 'samples')

for step in script:
    if step.startswith('peek:'):
        _, n, size = step.split(':')
        print(n, peek(n, int(size)).hex())
