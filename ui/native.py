"""UI-only ctypes bridge. Calibration, pixels, rotation and export live in C."""
import ctypes
import ctypes.util
import json
import os
import shutil
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('F2400_CLI') or (str(ROOT / 'build' / 'f2400') if (ROOT / 'build' / 'f2400').exists() else shutil.which('f2400') or 'f2400'))
_library = os.environ.get('F2400_LIBRARY')
if not _library:
    local = ROOT / 'build' / 'libf2400.so'
    _library = str(local) if local.exists() else ctypes.util.find_library('f2400')
if not _library:
    raise RuntimeError('请先构建 C 核心：cmake -S . -B build && cmake --build build')
lib = ctypes.CDLL(_library)
lib.f2400_error.restype = ctypes.c_char_p
lib.f2400_profile_info.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
lib.f2400_image_load.argtypes = [ctypes.c_char_p]
lib.f2400_image_load.restype = ctypes.c_void_p
lib.f2400_image_free.argtypes = [ctypes.c_void_p]
lib.f2400_image_rotate.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.f2400_image_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.f2400_image_preview.argtypes = [ctypes.c_void_p]
lib.f2400_image_preview.restype = ctypes.c_void_p
for _name in ('width', 'height', 'channels', 'depth'):
    _fn = getattr(lib, 'f2400_image_' + _name)
    _fn.argtypes = [ctypes.c_void_p]
    _fn.restype = ctypes.c_uint

def checked(status):
    if status < 0:
        raise ValueError(lib.f2400_error().decode('utf-8', errors='replace'))

def load_profile(path):
    output = ctypes.create_string_buffer(65536)
    checked(lib.f2400_profile_info(os.fsencode(path), output, len(output)))
    metadata = json.loads(output.value)
    return None, metadata['positive_exposures'], metadata

class PreviewDocument:
    """Native image owns full-depth data even after temporary files disappear."""
    def __init__(self, handle):
        self.handle = handle
        self.turns = 0
        self.refresh()

    @classmethod
    def load(cls, path):
        handle = lib.f2400_image_load(os.fsencode(path))
        if not handle:
            checked(-1)
        return cls(handle)

    def refresh(self):
        w = lib.f2400_image_width(self.handle)
        h = lib.f2400_image_height(self.handle)
        c = lib.f2400_image_channels(self.handle)
        data = ctypes.string_at(lib.f2400_image_preview(self.handle), w * h * c)
        self.image = Image.frombytes('RGB' if c == 3 else 'L', (w, h), data).convert('RGB')
        self.high_depth = True if lib.f2400_image_depth(self.handle) == 16 else None

    def rotate(self, clockwise=True):
        checked(lib.f2400_image_rotate(self.handle, int(clockwise)))
        self.turns = (self.turns + (1 if clockwise else -1)) % 4
        self.refresh()

    def save(self, path):
        checked(lib.f2400_image_save(self.handle, os.fsencode(path)))

    def close(self):
        if self.handle:
            lib.f2400_image_free(self.handle)
            self.handle = None

    def __del__(self):
        self.close()
