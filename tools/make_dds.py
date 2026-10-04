"""Write uncompressed B8G8R8A8 DDS files with a full mip chain (Skyrim reads these natively).
Usage: make_dds.py placeholder <outdir>   |   make_dds.py convert <image> <out.dds>"""
import struct, sys, numpy as np
from PIL import Image

def write_dds(path, rgba):
    img = Image.fromarray(rgba, 'RGBA'); w, h = img.size
    mips = [img]
    while mips[-1].size != (1, 1):
        mw, mh = mips[-1].size
        mips.append(mips[-1].resize((max(1, mw // 2), max(1, mh // 2)), Image.LANCZOS))
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8  # caps|height|width|pixelformat|mipmapcount|pitch
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr = struct.pack('<4s7I', b'DDS ', 124, flags, h, w, w * 4, 0, len(mips)) + b'\0' * 44 + pf
    hdr += struct.pack('<IIIII', 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr)
        for m in mips:
            a = np.asarray(m.convert('RGBA'))
            f.write(a[..., [2, 1, 0, 3]].tobytes())  # RGBA -> BGRA

if __name__ == '__main__':
    if sys.argv[1] == 'placeholder':
        out = sys.argv[2]; S = 512
        rng = np.random.default_rng(7)
        noise = rng.normal(0, 1, (S // 8, S // 8))
        noise = np.array(Image.fromarray(((noise - noise.min()) / np.ptp(noise) * 255).astype(np.uint8)).resize((S, S), Image.BICUBIC)) / 255.0
        fine = rng.normal(0, 0.03, (S, S))
        base = np.array([0.42, 0.43, 0.45])  # gunmetal
        col = np.clip(base[None, None, :] * (0.8 + 0.35 * noise[..., None]) + fine[..., None], 0, 1)
        diff = np.dstack([(col * 255).astype(np.uint8), np.full((S, S), 255, np.uint8)])
        write_dds(f'{out}/physcannon.dds', diff)
        nrm = np.zeros((S, S, 4), np.uint8); nrm[..., 0] = 128; nrm[..., 1] = 128; nrm[..., 2] = 255
        nrm[..., 3] = (110 + 60 * noise).astype(np.uint8)  # alpha = specular strength
        write_dds(f'{out}/physcannon_n.dds', nrm)
    else:
        a = np.asarray(Image.open(sys.argv[2]).convert('RGBA'))
        write_dds(sys.argv[3], a)
