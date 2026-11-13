# A minimal FAT16 superfloppy image: create one holding files in the root directory, list and read files back.
# Used as the emulated flashcart's card (DeSmuME CompactFlash, file mode) so saves persist between emulator runs.
import struct, math
SECT = 512

def lfn_checksum(short11):
    s = 0
    for ch in short11: s = (((s & 1) << 7) + (s >> 1) + ch) & 0xFF
    return s

class Fat16:
    def __init__(self, path):
        self.path = path
        self.img = bytearray(open(path, 'rb').read())
        b = self.img
        self.bps, self.spc, self.reserved, self.nfats, self.root_entries = struct.unpack_from('<HBHBH', b, 0x0B)
        self.fatsz = struct.unpack_from('<H', b, 0x16)[0]
        self.fat_off = self.reserved * SECT
        self.root_off = (self.reserved + self.nfats * self.fatsz) * SECT
        self.data_off = self.root_off + self.root_entries * 32
    def save(self): open(self.path, 'wb').write(self.img)
    def fat_get(self, c): return struct.unpack_from('<H', self.img, self.fat_off + 2 * c)[0]
    def fat_set(self, c, v):
        for k in range(self.nfats): struct.pack_into('<H', self.img, self.fat_off + k * self.fatsz * SECT + 2 * c, v)
    def clus_off(self, c): return self.data_off + (c - 2) * self.spc * SECT
    def entries(self, include_deleted=False):
        out = []; lfn = []
        for i in range(self.root_entries):
            e = bytes(self.img[self.root_off + 32 * i: self.root_off + 32 * i + 32])
            if e[0] == 0: break
            if e[0] == 0xE5:
                if include_deleted: out.append(dict(index=i, deleted=True, raw=e, attr=e[11]))
                lfn = []; continue
            if e[11] == 0x0F:
                part = e[1:11] + e[14:26] + e[28:32]
                lfn.insert(0, part.decode('utf-16-le')); continue
            name = e[0:8].decode('latin1').rstrip(); ext = e[8:11].decode('latin1').rstrip()
            short = name + ('.' + ext if ext else '')
            long = ''.join(lfn).split('\0')[0] if lfn else None
            lfn = []
            clus = struct.unpack_from('<H', e, 26)[0]; size = struct.unpack_from('<I', e, 28)[0]
            out.append(dict(index=i, short=short, long=long, name=long or short, attr=e[11], cluster=clus, size=size, deleted=False))
        return out
    def chain(self, c):
        out = []
        while 2 <= c < 0xFFF0 and len(out) < 70000:
            out.append(c); c = self.fat_get(c)
        return out
    def read(self, name):
        for e in self.entries():
            if e['name'].lower() == name.lower() or e['short'].lower() == name.lower():
                data = b''.join(bytes(self.img[self.clus_off(c): self.clus_off(c) + self.spc * SECT]) for c in self.chain(e['cluster']))
                return data[:e['size']]
        return None
    def file_offset(self, name):
        for e in self.entries():
            if e['name'].lower() == name.lower() or e['short'].lower() == name.lower():
                return self.clus_off(e['cluster']) if e['cluster'] >= 2 else None
        return None
    def free_cluster(self, start=2):
        n = (len(self.img) - self.data_off) // (self.spc * SECT)
        for c in range(start, n + 2):
            if self.fat_get(c) == 0: return c
        raise RuntimeError('full')
    def add(self, name, data):
        """Add a file (8.3 short entry, plus a long-name entry when the name is not plain 8.3 upper case)."""
        base, _, ext = name.rpartition('.') if '.' in name else (name, '', '')
        plain = name.upper() == name and len(base) <= 8 and len(ext) <= 3 and '.' not in base
        if len(base) <= 8 and len(ext) <= 3 and '.' not in base:
            short11 = (base.upper().ljust(8) + ext.upper().ljust(3)).encode('latin1')
        else:
            short11 = ((base.replace('.', '').upper()[:6] + '~1').ljust(8) + ext.upper()[:3].ljust(3)).encode('latin1')
        nclus = max(1, math.ceil(len(data) / (self.spc * SECT))) if data else 0
        first = 0; prev = 0; c = 2
        for k in range(nclus):
            c = self.free_cluster(c)
            if prev: self.fat_set(prev, c)
            else: first = c
            self.fat_set(c, 0xFFFF)
            off = self.clus_off(c); chunk = data[k * self.spc * SECT:(k + 1) * self.spc * SECT]
            self.img[off:off + len(chunk)] = chunk
            prev = c
        ents = []
        if not plain:
            u = name.encode('utf-16-le') + b'\0\0'
            while len(u) % 26: u += b'\xff\xff'
            parts = [u[i:i + 26] for i in range(0, len(u), 26)]
            cs = lfn_checksum(short11)
            for i in reversed(range(len(parts))):
                p = parts[i]; seq = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
                ents.append(bytes([seq]) + p[0:10] + bytes([0x0F, 0, cs]) + p[10:22] + b'\0\0' + p[22:26])
        ents.append(short11 + bytes([0x20, 0, 0]) + struct.pack('<HHHHHHHI', 0, 0x5B6C, 0x5B6C, 0, 0, 0x5B6C, first, len(data)))
        # first run of free slots
        i = 0
        while i < self.root_entries and self.img[self.root_off + 32 * i] not in (0,): i += 1
        for k, en in enumerate(ents): self.img[self.root_off + 32 * (i + k): self.root_off + 32 * (i + k) + 32] = en
    def remove(self, name):
        lfn_idx = []
        for i in range(self.root_entries):
            e = self.img[self.root_off + 32 * i: self.root_off + 32 * i + 32]
            if e[0] == 0: break
            if e[0] == 0xE5: lfn_idx = []; continue
            if e[11] == 0x0F: lfn_idx.append(i); continue
            ent = [x for x in self.entries() if x['index'] == i][0]
            if ent['name'].lower() == name.lower() or ent['short'].lower() == name.lower():
                for c in self.chain(ent['cluster']): self.fat_set(c, 0)
                for j in lfn_idx + [i]: self.img[self.root_off + 32 * j] = 0xE5
                return True
            lfn_idx = []
        return False

def create(path, files, total_mb=64, spc=8):
    total = total_mb * 1024 * 1024 // SECT
    reserved, nfats, root_entries = 1, 2, 512
    root_sectors = root_entries * 32 // SECT
    fatsz = 1
    while True:
        clusters = (total - reserved - nfats * fatsz - root_sectors) // spc
        need = math.ceil((clusters + 2) * 2 / SECT)
        if need <= fatsz: break
        fatsz = need
    assert 4085 <= clusters <= 65524, clusters
    img = bytearray(total * SECT)
    bs = bytearray(SECT)
    bs[0:3] = b'\xEB\x3C\x90'; bs[3:11] = b'MSDOS5.0'
    struct.pack_into('<HBHBHHBHHHII', bs, 0x0B, SECT, spc, reserved, nfats, root_entries, 0, 0xF8, fatsz, 63, 255, 0, total)
    bs[0x24] = 0x80; bs[0x26] = 0x29; struct.pack_into('<I', bs, 0x27, 0x12345678)
    bs[0x2B:0x36] = b'WORDPASS   '; bs[0x36:0x3E] = b'FAT16   '
    bs[0x1FE] = 0x55; bs[0x1FF] = 0xAA
    img[0:SECT] = bs
    open(path, 'wb').write(img)
    f = Fat16(path)
    f.fat_set(0, 0xFFF8); f.fat_set(1, 0xFFFF)
    for name, data in files: f.add(name, data)
    f.save()
    return f

if __name__ == '__main__':
    import sys
    f = Fat16(sys.argv[1])
    for e in f.entries(): print(e['name'], e['short'], e['size'], e['cluster'])
