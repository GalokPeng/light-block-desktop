"""Run the native clipboard regression in a private window station."""
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(pixels, 1)) + chunk(b"IEND", b""))


def main():
    build = ROOT / "build"
    toolchain = Path(os.environ.get("LIGHTBLOCK_COMPILER_ROOT", build / "native-toolchain/Compiler"))
    compiler = toolchain / "bin/clang++.exe"
    objects = [build / (source.stem + ".o") for source in sorted((ROOT / "src").glob("*.cpp"))
               if source.name != "clipboard.cpp"]
    libraries = ("user32 gdi32 shell32 comctl32 shlwapi dwmapi ole32 oleaut32 windowscodecs uuid "
                 "mfuuid propsys msimg32 comdlg32 psapi advapi32 winhttp bcrypt runtimeobject gdiplus").split()
    with tempfile.TemporaryDirectory(prefix="clipboard-io-", dir=build) as temp:
        root = Path(temp)
        probe = root / "clipboard-io.exe"
        subprocess.run([compiler, "--target=x86_64-w64-mingw32", "-DUNICODE", "-D_UNICODE", "-DWINVER=0x0A00",
                        "-D_WIN32_WINNT=0x0A00", "-std=c++23", "-Os", "-static", "-municode", "-I", build,
                        ROOT / "native/tests/clipboard_io.cpp", *objects, *["-l" + lib for lib in libraries],
                        "-o", probe], check=True)
        png(root / "transparent.png", 2, 1, bytes([0, 250, 150, 50, 127, 0, 0, 0, 0]))
        png(root / "long.png", 1024, 20000, bytes((1024 * 4 + 1) * 20000))
        rng = random.Random(17)
        png(root / "large.png", 1200, 1200, b"".join(b"\0" + rng.randbytes(4800) for _ in range(1200)))
        assert (root / "large.png").stat().st_size > 5 * 1024**2
        # A valid very tall solid-color image stays small on disk.
        png(root / "oversize.png", 512, 65537, bytes((512 * 4 + 1) * 65537))
        png(root / "oversize-bytes.png", 2100, 2048, b"".join(b"\0" + rng.randbytes(8400) for _ in range(2048)))
        assert (root / "oversize-bytes.png").stat().st_size > 16 * 1024**2
        subprocess.run([probe, root], check=True, timeout=45)
        print("PASS isolated clipboard history, PNG editing, cross-process paste, busy retry and size limits.")


if __name__ == "__main__":
    main()
