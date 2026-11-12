"""SLF archive reader/writer (Sir-Tech library format used by JA2 / Wizardry 8).

Header (532 bytes): libName[256], libPath[256], numEntries i32, numUsed i32,
                    sort u16 (0xffff), version u16 (0x0200), containsSubdirs i32
Entries (280 bytes each) are stored at the END of the file:
  name[256], offset u32, length u32, state u8, reserved u8, pad[2], FILETIME[8], reserved u16, pad[2]
"""
import struct, sys

ENTRY = 280
BS = chr(92)
FILETIME = bytes.fromhex('00f9d876d681c101')  # same timestamp as the official PATCH.001


def read_slf(path):
    f = open(path, 'rb')
    h = f.read(532)
    lib = h[:256].split(b'\0')[0]
    lp = h[256:512].split(b'\0')[0]
    n, used, sort, ver, sub = struct.unpack('<iiHHi', h[512:528])
    f.seek(0, 2)
    size = f.tell()
    f.seek(size - n * ENTRY)
    ents = []
    for i in range(n):
        e = f.read(ENTRY)
        name = e[:256].split(b'\0')[0].decode('latin1')
        off, ln, state = struct.unpack('<IIB', e[256:265])
        ents.append((name, off, ln, state))
    return dict(lib=lib, path=lp, n=n, used=used, ver=ver, sub=sub, size=size), ents


def read_file(path, name):
    """Return bytes of entry `name` (case-insensitive, backslash separated) or None."""
    h, es = read_slf(path)
    for n, o, l, s in es:
        if n.upper() == name.upper():
            f = open(path, 'rb')
            f.seek(o)
            return f.read(l)
    return None


def write_slf(path, libname, libpath, files):
    """files: dict {name: bytes}. Entries are sorted case-insensitively (lower case), like DATA.SLF:
    the exe looks names up with bsearch + _stricmp (0x4131b0 / 0x413360)."""
    names = sorted(files, key=lambda s: s.lower())
    out = bytearray()
    out += libname.encode('latin1').ljust(256, b'\0')
    out += libpath.encode('latin1').ljust(256, b'\0')
    out += struct.pack('<iiHHii', len(names), len(names), 0xffff, 0x200, 1, 0)
    assert len(out) == 532
    table = bytearray()
    for n in names:
        data = files[n]
        off = len(out)
        out += data
        e = n.upper().encode('latin1').ljust(256, b'\0')
        e += struct.pack('<IIBB2x', off, len(data), 0, 0) + FILETIME + b'\0\0\0\0'
        assert len(e) == ENTRY
        table += e
    out += table
    open(path, 'wb').write(out)


if __name__ == '__main__':
    hdr, ents = read_slf(sys.argv[1])
    print(hdr)
    for e in ents:
        print(e)
