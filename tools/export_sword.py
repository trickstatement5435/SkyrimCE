"""Halo energy sword OBJ -> Skyrim mesh data (same .bin format as the Gravity Gun tools) + textures.
Only the real mesh (group __unnamed) is used; the other groups are Halo marker spheres.
Axes: Halo model X = blade tips, Z = up the handle bar (thumb side), origin = the grip marker.
NIF: blade along +Y, handle bar along +Z, grip at the origin  ->  nif = (-y, x, z) * SCALE.
usage: export_sword.py sword.obj sword.png <bin out dir> <Textures/EnergySword>"""
import struct, sys, os, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from make_dds import write_dds

SCALE = 1.75          # ~44 Halo units -> ~78 Skyrim units (about 1.1 m)
SHELL = 0.45          # glow shell offset (Skyrim units)
obj, png, bin_dir, tex_dir = sys.argv[1:5]

V, VT, VN, faces = [], [], [], []
g = None
for line in open(obj):
    p = line.split()
    if not p: continue
    if p[0] == 'v': V.append(list(map(float, p[1:4])))
    elif p[0] == 'vt': VT.append(list(map(float, p[1:3])))
    elif p[0] == 'vn': VN.append(list(map(float, p[1:4])))
    elif p[0] == 'g': g = p[1] if len(p) > 1 else ''
    elif p[0] == 'f' and g == '__unnamed':
        faces.append([tuple(int(i) - 1 for i in c.split('/')) for c in p[1:]])
V, VT, VN = np.array(V), np.array(VT), np.array(VN)
R = lambda a: np.stack([-a[..., 1], a[..., 0], a[..., 2]], -1)

verts, tris, shell_verts, shell_tris = [], [], [], []
flips = 0
for f in faces:
    for k in range(1, len(f) - 1):
        tri = [f[0], f[k], f[k + 1]]
        P = R(V[[c[0] for c in tri]]) * SCALE
        N = R(VN[[c[2] for c in tri]])
        T = VT[[c[1] for c in tri]].copy(); T[:, 1] = 1.0 - T[:, 1]
        if np.dot(np.cross(P[1] - P[0], P[2] - P[0]), N.sum(0)) < 0:  # make winding agree with the normals
            tri = tri[::-1]; P = P[::-1]; N = N[::-1]; T = T[::-1]; flips += 1
        i = len(verts)
        for j in range(3): verts.append((*P[j], *N[j], *T[j]))
        tris.append((i, i + 1, i + 2))
        if T[:, 1].mean() > 0.5:  # blue part of the atlas = the energy blade -> glow shell
            i = len(shell_verts)
            for j in range(3): shell_verts.append((*(P[j] + N[j] * SHELL), *N[j], *T[j]))
            shell_tris.append((i, i + 1, i + 2))

def save(path, vs, ts):
    with open(path, 'wb') as fh:
        fh.write(struct.pack('<II', len(vs), len(ts)))
        for v in vs: fh.write(struct.pack('<8f', *v))
        for t in ts: fh.write(struct.pack('<3H', *t))
    print(path, len(vs), 'verts', len(ts), 'tris')
save(f'{bin_dir}/sword.bin', verts, tris)
save(f'{bin_dir}/shell.bin', shell_verts, shell_tris)
P = np.array([v[:3] for v in verts]); print('flipped', flips, 'bbox', P.min(0).round(1), P.max(0).round(1))

# textures: 128px Halo atlas upscaled; top half = handle metal, bottom half = blade energy
img = Image.open(png).convert('RGBA').resize((512, 512), Image.LANCZOS)
a = np.asarray(img).astype(float) / 255
blade = np.zeros(a.shape[:2]); blade[256:, :] = 1.0
diff = a.copy(); diff[..., 3] = 1.0
diff[256:, :, :3] = np.clip(diff[256:, :, :3] * 1.15, 0, 1)
write_dds(f'{tex_dir}/energysword.dds', (diff * 255).astype(np.uint8))
glow = np.zeros_like(a); glow[..., 3] = 1.0
glow[256:, :, :3] = np.clip(a[256:, :, :3] * 1.4 + np.array([0.05, 0.25, 0.45]), 0, 1)
write_dds(f'{tex_dir}/energysword_g.dds', (glow * 255).astype(np.uint8))
n = np.zeros((512, 512, 4), np.uint8); n[..., :3] = (128, 128, 255); n[..., 3] = 90; n[:256, :, 3] = 180
write_dds(f'{tex_dir}/energysword_n.dds', n)
# soft glow for the halo shell: the blade colors, bright
halo = np.zeros_like(a); halo[256:, :, :3] = np.clip(a[256:, :, :3] * 0.6 + np.array([0.2, 0.55, 1.0]) * 0.6, 0, 1); halo[..., 3] = 1.0
write_dds(f'{tex_dir}/energysword_halo.dds', (halo * 255).astype(np.uint8))
print('textures ok')
