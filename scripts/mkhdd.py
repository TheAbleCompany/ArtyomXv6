#!/usr/bin/env python3
"""mkhdd.py — сборка raw HDD-образа (GPT) с Limine для molloy-xv6-synth.

Структура образа (по USAGE.md Limine: «UEFI» и «BIOS/GPT»):
  p1: EFI System Partition (FAT32):
        /boot/limine/limine.conf, /boot/limine/limine-bios.sys,
        /boot/kernel.elf, /boot/initrd.bin,
        /EFI/BOOT/BOOTX64.EFI, /EFI/BOOT/BOOTIA32.EFI
  p2: BIOS boot partition (1 MiB >= 32 KiB) — сюда утилита
      `limine bios-install image 2` ставит stage 2 (выполняется в Makefile
      после этого скрипта).

Зависимостей нет: GPT и FAT32 (с LFN-записями) строятся на чистом Python.

Запуск:
  python3 scripts/mkhdd.py OUT_IMG KERNEL.ELF INITRD.BIN LIMINE.CONF LIMINE_BINARY_DIR
"""
import os
import struct
import sys
import uuid
import zlib

SECTOR = 512

EFI_SYSTEM_GUID = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
BIOS_BOOT_GUID = uuid.UUID("21686148-6449-6E6F-744E-656564454649")

DISK_SECTORS = 96 * 1024 * 1024 // SECTOR      # 96 MiB
ESP_START = 2048
ESP_SECTORS = 48 * 1024 * 1024 // SECTOR       # 48 MiB
BIOS_START = ESP_START + ESP_SECTORS
BIOS_SECTORS = 2048                            # 1 MiB

DATE = 0x2821                                  # 2000-01-01
TIME = 0x6000                                  # 12:00


# ---------------------------------------------------------------- GPT --------

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def gpt_entry(type_guid, start_lba, end_lba, name=""):
    e = bytearray(128)
    e[0:16] = type_guid.bytes_le
    e[16:32] = uuid.uuid4().bytes_le
    struct.pack_into("<Q", e, 32, start_lba)
    struct.pack_into("<Q", e, 40, end_lba)
    e[56:56 + len(name) * 2] = name.encode("utf-16-le")
    return bytes(e)


def build_gpt(img: bytearray, disk_sectors: int, partitions):
    # protective MBR
    mbr = bytearray(SECTOR)
    mbr[0x1BE + 4] = 0xEE
    struct.pack_into("<I", mbr, 0x1BE + 8, 1)
    struct.pack_into("<I", mbr, 0x1BE + 12,
                     min(disk_sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xaa"
    img[0:SECTOR] = mbr

    n_entries, entry_size = 128, 128
    arr_bytes = n_entries * entry_size
    arr_lbas = arr_bytes // SECTOR                       # 32
    backup_arr_lba = disk_sectors - 1 - arr_lbas
    entry_lba = 2

    entries = bytearray()
    for tg, s, e, nm in partitions:
        entries += gpt_entry(tg, s, e, nm)
    entries += bytes(arr_bytes - len(entries))

    def gpt_header(current, backup, arr_l):
        h = bytearray(92)
        h[0:8] = b"EFI PART"
        struct.pack_into("<I", h, 8, 0x00010000)
        struct.pack_into("<I", h, 12, 92)
        struct.pack_into("<I", h, 16, 0)                 # CRC ниже
        struct.pack_into("<Q", h, 24, current)
        struct.pack_into("<Q", h, 32, backup)
        first_usable = 34
        last_usable = disk_sectors - arr_lbas - 2
        struct.pack_into("<Q", h, 40, first_usable)
        struct.pack_into("<Q", h, 48, last_usable)
        h[56:72] = uuid.uuid4().bytes_le
        struct.pack_into("<Q", h, 72, arr_l)
        struct.pack_into("<I", h, 80, n_entries)
        struct.pack_into("<I", h, 84, entry_size)
        struct.pack_into("<I", h, 88, crc32(entries))
        struct.pack_into("<I", h, 16, crc32(bytes(h)))
        return bytes(h)

    off = entry_lba * SECTOR
    img[off:off + arr_bytes] = entries
    img[backup_arr_lba * SECTOR:(backup_arr_lba + arr_lbas) * SECTOR] = entries
    # заголовки пишем ПОЛНЫМИ секторами: присваивание 92 байт срезу длиной
    # 512 сжало бы bytearray и сместило все данные после него
    img[SECTOR:2 * SECTOR] = gpt_header(1, disk_sectors - 1,
                                        entry_lba).ljust(SECTOR, b"\x00")
    img[(disk_sectors - 1) * SECTOR:] = gpt_header(disk_sectors - 1, 1,
                                                   backup_arr_lba) \
        .ljust(SECTOR, b"\x00")


# --------------------------------------------------------------- FAT32 -------

def lfn_checksum(short: bytes) -> int:
    chk = 0
    for b in short:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
    return chk


def to_8dot3(name: str):
    """Полное 8.3 имя (без LFN) или None, если нужна LFN-запись."""
    base, _, ext = name.partition(".")
    base, ext = base.upper(), ext.upper()
    if not base or len(base) > 8 or len(ext) > 3:
        return None
    if not (base + ext).replace(" ", "").isalnum():
        return None
    if base != base.upper() or (ext and ext != ext.upper()):
        return None
    # строчные буквы в исходном имени требуют LFN (короткое хранится в upper)
    if name.partition(".")[0].islower() or (ext and ext.islower()):
        return None
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def make_short_name(name: str, existing: set) -> bytes:
    base, _, ext = name.partition(".")
    base = base.upper()
    ext = ext.upper()[:3]
    for i in range(1, 1000000):
        tail = f"~{i}"
        cand = (base[:8 - len(tail)] + tail).ljust(8).encode("ascii") \
            + ext.ljust(3).encode("ascii")
        if cand not in existing:
            existing.add(cand)
            return cand
    raise RuntimeError("too many names")


def lfn_entries(name: str, short: bytes) -> bytes:
    chk = lfn_checksum(short)
    utf16 = name.encode("utf-16-le") + b"\x00\x00" + b"\xff\xff"
    chunks = []
    for i in range(0, len(utf16), 26):
        c = bytearray(utf16[i:i + 26])
        c += b"\xff" * (26 - len(c))
        chunks.append(bytes(c))
    out = bytearray()
    for seq, chunk in enumerate(chunks, start=1):
        e = bytearray(32)
        e[0] = seq | (0x40 if seq == len(chunks) else 0)
        e[1:11] = chunk[0:10]
        e[11] = 0x0F
        e[13] = chk
        e[14:26] = chunk[10:22]
        e[28:32] = chunk[22:26]
        out += bytes(e)
    # LFN-записи идут в обратном порядке непосредственно перед короткой
    return b"".join(out[i * 32:(i + 1) * 32] for i in range(len(out) // 32 - 1, -1, -1))


class Fat32:
    def __init__(self, img: bytearray, part_start: int, part_sectors: int):
        self.img = img
        self.base = part_start * SECTOR
        self.sectors = part_sectors
        self.reserved = 32
        self.sp_c = 1                          # секторов на кластер (512 Б)
        # размер FAT: верхняя оценка ceil(tot_sectors*4/512) — покрывает все
        # кластеры при любом резерве; для 48 МиБ ESP кластеров ~96k >= 65525
        # (правило FAT32) выполняется автоматически
        fat_sectors = (part_sectors * 4 + SECTOR - 1) // SECTOR
        self.fat_sectors = fat_sectors
        self.fat_off = [self.base + (self.reserved + i * fat_sectors) * SECTOR
                        for i in range(2)]
        self.data_off = self.base + (self.reserved + 2 * fat_sectors) * SECTOR
        self.max_clusters = (part_sectors - self.reserved -
                             2 * fat_sectors) // self.sp_c
        self.next_free = 2

        self._boot_sector()
        self._fsinfo()
        self._set_fat(0, 0x0FFFFFF8)
        self._set_fat(1, 0x0FFFFFFF)

    def _boot_sector(self):
        b = bytearray(SECTOR)
        b[0:3] = b"\xeb\x3c\x90"
        b[3:11] = b"LIMINE  "
        struct.pack_into("<H", b, 11, SECTOR)
        b[13] = self.sp_c
        struct.pack_into("<H", b, 14, self.reserved)
        b[16] = 2
        b[21] = 0xF8
        struct.pack_into("<H", b, 24, 63)
        struct.pack_into("<H", b, 26, 255)
        struct.pack_into("<I", b, 32, self.sectors)
        struct.pack_into("<I", b, 36, self.fat_sectors)
        struct.pack_into("<I", b, 44, 2)       # root cluster
        struct.pack_into("<H", b, 48, 1)       # fsinfo sector
        struct.pack_into("<H", b, 50, 6)       # backup boot
        b[54:62] = b"ARTYOMXV6"
        b[64] = 0x29
        struct.pack_into("<I", b, 67, 0xA6A60001)
        b[71:82] = b"ARTYOMXV6 "
        b[82:90] = b"FAT32   "
        b[510:512] = b"\x55\xaa"
        self.img[self.base:self.base + SECTOR] = b
        self.img[self.base + 6 * SECTOR:self.base + 7 * SECTOR] = b

    def _fsinfo(self):
        f = bytearray(SECTOR)
        f[0:4] = b"RRaA"
        f[484:488] = b"rrAa"
        struct.pack_into("<I", f, 488, 0xFFFFFFFF)
        struct.pack_into("<I", f, 492, 0xFFFFFFFF)
        f[510:512] = b"\x55\xaa"
        self.img[self.base + SECTOR:self.base + 2 * SECTOR] = f

    def _set_fat(self, cluster: int, value: int):
        struct.pack_into("<I", self.img, self.fat_off[0] + cluster * 4, value)
        struct.pack_into("<I", self.img, self.fat_off[1] + cluster * 4, value)

    def alloc_chain(self, nbytes: int) -> int:
        n = max(1, (nbytes + self.sp_c * SECTOR - 1) // (self.sp_c * SECTOR))
        if self.next_free + n > self.max_clusters + 2:
            raise RuntimeError("ESP overflow: увеличьте размер образа")
        first = self.next_free
        cur = first
        for i in range(n):
            self._set_fat(cur, 0x0FFFFFFF if i == n - 1 else cur + 1)
            cur += 1
        self.next_free = cur
        return first

    def write_clusters(self, start: int, data: bytes):
        off = self.data_off + (start - 2) * self.sp_c * SECTOR
        self.img[off:off + len(data)] = data


def dir_entry(short: bytes, attr: int, first: int, size: int) -> bytes:
    e = bytearray(32)
    e[0:11] = short
    e[11] = attr
    struct.pack_into("<H", e, 14, TIME)        # creation
    struct.pack_into("<H", e, 16, DATE)
    struct.pack_into("<H", e, 18, DATE)        # access
    struct.pack_into("<H", e, 22, TIME)        # write
    struct.pack_into("<H", e, 24, DATE)
    struct.pack_into("<H", e, 20, first >> 16)
    struct.pack_into("<H", e, 26, first & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def dot_entry(name: bytes, cluster: int) -> bytes:
    e = bytearray(32)
    e[0:11] = name.ljust(11)
    e[11] = 0x10
    struct.pack_into("<H", e, 14, TIME)
    struct.pack_into("<H", e, 16, DATE)
    struct.pack_into("<H", e, 18, DATE)
    struct.pack_into("<H", e, 22, TIME)
    struct.pack_into("<H", e, 24, DATE)
    struct.pack_into("<H", e, 20, cluster >> 16)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    return bytes(e)


def entry_for(name: str, attr: int, first: int, size: int, existing: set) -> bytes:
    short = to_8dot3(name)
    if short is None:
        short = make_short_name(name, existing)
        return lfn_entries(name, short) + dir_entry(short, attr, first, size)
    return dir_entry(short, attr, first, size)


def build_esp(img: bytearray, part_start: int, part_sectors: int, files):
    """files: list of (path_parts, data)."""
    fs = Fat32(img, part_start, part_sectors)

    tree = {}
    for parts, data in files:
        node = tree
        for p in parts[:-1]:
            node = node.setdefault(p, {})
        node[parts[-1]] = data

    def emit(payload: dict, used: set) -> bytes:
        out = bytearray()
        for name, val in payload.items():
            if isinstance(val, dict):
                sub = emit(val, set())        # короткие имена уникальны в каталоге
                first = fs.alloc_chain(len(sub))
                fs.write_clusters(first, sub)
                out += entry_for(name, 0x10, first, 0, used)
            else:
                first = fs.alloc_chain(len(val))
                chunk = ((len(val) + SECTOR - 1) // SECTOR) * SECTOR
                fs.write_clusters(first, val + b"\x00" * (chunk - len(val)))
                out += entry_for(name, 0x20, first, len(val), used)
        return bytes(out)

    root_used = set()
    fs.alloc_chain(SECTOR)               # корневой каталог = кластер 2
    root = (dot_entry(b".", 2) + dot_entry(b"..", 2)
            + emit(tree, root_used))
    root += b"\x00" * (SECTOR - len(root) % SECTOR if len(root) % SECTOR else 0)
    if len(root) > SECTOR:
        raise RuntimeError("root directory overflow")
    fs.write_clusters(2, root)


def main():
    if len(sys.argv) != 6:
        print(__doc__)
        sys.exit(1)
    img_path, kernel, initrd, conf, limine_dir = sys.argv[1:6]

    def rd(p):
        with open(p, "rb") as f:
            return f.read()

    files = [
        (("boot", "kernel.elf"), rd(kernel)),
        (("boot", "initrd.bin"), rd(initrd)),
        (("boot", "limine", "limine.conf"), rd(conf)),
        (("boot", "limine", "limine-bios.sys"),
         rd(os.path.join(limine_dir, "limine-bios.sys"))),
        (("EFI", "BOOT", "BOOTX64.EFI"),
         rd(os.path.join(limine_dir, "BOOTX64.EFI"))),
        (("EFI", "BOOT", "BOOTIA32.EFI"),
         rd(os.path.join(limine_dir, "BOOTIA32.EFI"))),
    ]

    img = bytearray(DISK_SECTORS * SECTOR)
    build_gpt(img, DISK_SECTORS,
              [(EFI_SYSTEM_GUID, ESP_START, ESP_START + ESP_SECTORS - 1, "ESP"),
               (BIOS_BOOT_GUID, BIOS_START, BIOS_START + BIOS_SECTORS - 1,
                "BIOS boot")])
    build_esp(img, ESP_START, ESP_SECTORS, files)

    with open(img_path, "wb") as f:
        f.write(img)
    print(f"mkhdd: {img_path} ({len(img) // (1 << 20)} MiB, "
          f"ESP {ESP_SECTORS // 2048} MiB, BIOS boot 1 MiB)")


if __name__ == "__main__":
    main()
