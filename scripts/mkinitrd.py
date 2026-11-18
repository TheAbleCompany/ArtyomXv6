#!/usr/bin/env python3
"""mkinitrd — генератор initrd-образа.

Формат взят из туториала Джеймса Моллоя (kfs), но с исправлениями:
  * смещения и длины — little-endian u32, offset отсчитывается от начала образа;
  * генератор больше не «затирает» имя файла размером заголовка (баг оригинала);
  * загрузчик (kernel/initrd.c) проверяет границы и магию при разборе.

Раскладка:
  [u32 magic=0xBF00BF00][u32 nfiles]
  [n x {char name[64]; u32 offset; u32 length}]   -- заголовки файлов
  [данные файлов подряд]
"""
import struct
import sys

MAGIC = 0xBF00BF00


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit("usage: mkinitrd.py <out.bin> <file1> [file2 ...]")
    out, paths = sys.argv[1], sys.argv[2:]

    hdr_size = 8 + 72 * len(paths)
    headers, blobs, off = [], [], hdr_size
    for p in paths:
        with open(p, "rb") as f:
            data = f.read()
        name = p.split("/")[-1].encode()
        if len(name) >= 64:
            sys.exit(f"name too long: {p}")
        headers.append((name, off, len(data)))
        blobs.append(data)
        off += len(data)

    with open(out, "wb") as f:
        f.write(struct.pack("<II", MAGIC, len(paths)))
        for name, o, l in headers:
            f.write(name.ljust(64, b"\0"))
            f.write(struct.pack("<II", o, l))
        for b in blobs:
            f.write(b)
    print(f"mkinitrd: {out}: {len(paths)} files, {off} bytes")


if __name__ == "__main__":
    main()
