import struct

with open('E:/Pumps/PREX3-Original/BGA/03.DAT','rb') as f:
    data = f.read()

count = struct.unpack_from('<I', data, 8)[0]
print(f"count={count}")

# With 28-byte entries
dir_start = 0x18
hdr2_start = dir_start + count * 28
seek_rel = struct.unpack_from('<I', data, hdr2_start + 0x40)[0]
data_start = hdr2_start + 0x44 + seek_rel
print(f"With 28-byte entries: hdr2_start=0x{hdr2_start:x}, seek_rel=0x{seek_rel:x}, data_start=0x{data_start:x}")

# With 24-byte entries (current C code)
hdr2_start2 = dir_start + count * 24
seek_rel2 = struct.unpack_from('<I', data, hdr2_start2 + 0x40)[0]
data_start2 = hdr2_start2 + 0x44 + seek_rel2
print(f"With 24-byte entries: hdr2_start=0x{hdr2_start2:x}, seek_rel=0x{seek_rel2:x}, data_start=0x{data_start2:x}")

# Decrypt directory with 28-byte entries
dir_enc = bytearray(data[dir_start:dir_start+count*28])
key = 0xEF
for i in range(len(dir_enc)):
    dir_enc[i] ^= key
    key = (key + 0x4F) & 0xFF

# Entry 0 - 03.BGA
e = struct.unpack_from('<III', dir_enc, 16)
print(f"Entry 0: name={dir_enc[0:16]}, size=0x{e[0]:x}, offset=0x{e[1]:x}, extra=0x{e[2]:x}")
real_ofs = data_start + e[1]
print(f"  real_ofs=0x{real_ofs:x}")
bga_raw = data[real_ofs:real_ofs+e[0]]
print(f"  encrypted first 4 bytes: {bga_raw[:4].hex()}")
key = 0xEF
dec = bytearray(bga_raw)
for i in range(len(dec)):
    dec[i] ^= key
    key = (key + 0x4F) & 0xFF
print(f"  decrypted first 4 bytes: {bytes(dec[:4])}")
print(f"  is BGA2: {dec[:4] == b'BGA2'}")
print(f"  is BGA: {dec[:3] == b'BGA'}")

# Verify disk file extraction - read the BGA from Extracted folder
try:
    with open('E:/Pumps/PREX3-Original/BGA_extracted/03/03.BGA','rb') as f:
        disk_bga = f.read()
    print(f"Disk file first 4 bytes: {disk_bga[:4]}")
    print(f"Decrypted matches disk: {bytes(dec[:len(disk_bga)]) == disk_bga}")
except:
    print("Can't read disk file")
