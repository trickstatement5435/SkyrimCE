"""Generate EnergySword.esp (ESL-flagged) for Skyrim SE. Layouts follow xEdit's wbDefinitionsTES5.pas.
  MGEF EnergySwordShockFallback  shock damage on hit (the plugin swaps in vanilla's shock enchantment effect,
                                 which brings the crackling-lightning visuals on the blade)
  ENCH EnergySwordShock          contact enchantment, costs no charge
  STAT EnergySwordFirstPerson    1st person model (same mesh)
  WEAP EnergySword               one-handed sword
  COBJ EnergySwordRecipe         forge recipe
Master: Skyrim.esm. FormIDs 0x800+ (ESL range)."""
import struct, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else 'EnergySword.esp'
FORM_VERSION = 44
MOD = 0x01000000
ID_MGEF, ID_ENCH, ID_STAT, ID_WEAP, ID_COBJ = (MOD | i for i in (0x800, 0x801, 0x802, 0x803, 0x804))
AV_HEALTH, AV_ONEHANDED, AV_RESIST_SHOCK = 24, 6, 42
EQUP_EITHER_HAND = 0x00013F44
KYWD_CRAFTING_FORGE = 0x00088105
REFINED_MALACHITE, QUICKSILVER_INGOT, DWARVEN_INGOT = 0x0005ADA1, 0x0005ADA0, 0x000DB8A2
SHOCK_DAMAGE = 20.0      # per hit, like a strong vanilla shock enchantment

def sub(sig, data):
    assert len(sig) == 4 and len(data) < 0x10000, sig
    return sig.encode() + struct.pack('<H', len(data)) + data
def zstr(s): return s.encode('cp1252') + b'\0'
def record(sig, formid, subs, flags=0):
    data = b''.join(subs)
    return sig.encode() + struct.pack('<IIIIHH', len(data), flags, formid, 0, FORM_VERSION, 0) + data
def group(sig, records):
    body = b''.join(records)
    return b'GRUP' + struct.pack('<I4sIHHI', 24 + len(body), sig.encode(), 0, 0, 0, 0) + body
def obnd(*b): return sub('OBND', struct.pack('<6h', *b))

# MGEF: value modifier on Health, resisted by shock resistance, contact delivery
flags = (1 << 0) | (1 << 2) | (1 << 9) | (1 << 11)  # hostile, detrimental, no duration, no area
mgef_data = struct.pack('<IfIiiHHIfIIIIffffIiIIIIiIIIfIfIIIIIIIff',
    flags, 0.0, 0, -1, AV_RESIST_SHOCK, 0, 0, 0, 0.0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0,
    0, AV_HEALTH, 0, 0,
    1, 1,                 # fire and forget, contact
    -1, 0, 0, 0, 1.0, 0, 1.0, 0, 0, 0, 0, 0, 0, 1, 0.0, 0.0)
assert len(mgef_data) == 152
mgef = record('MGEF', ID_MGEF, [sub('EDID', zstr('EnergySwordShockFallback')), sub('FULL', zstr('Plasma Shock')),
                                sub('DATA', mgef_data), sub('DNAM', zstr('Burns for <mag> points of shock damage.'))])
ench = record('ENCH', ID_ENCH, [
    sub('EDID', zstr('EnergySwordShock')), obnd(0, 0, 0, 0, 0, 0), sub('FULL', zstr('Plasma Shock')),
    sub('ENIT', struct.pack('<iIIiIIfII', 0, 0x1, 1, 0, 1, 0x06, 0.0, 0, 0)),  # cost 0 (never drains), no auto-calc, FF, contact
    sub('EFID', struct.pack('<I', ID_MGEF)), sub('EFIT', struct.pack('<fII', SHOCK_DAMAGE, 0, 0)),
])
stat = record('STAT', ID_STAT, [sub('EDID', zstr('EnergySwordFirstPerson')), obnd(-12, -3, -11, 15, 3, 61),
                                sub('MODL', zstr('EnergySword\\energysword.nif')), sub('DNAM', struct.pack('<fI', 90.0, 0))])
dnam = struct.pack('<B3sffHHf4sBBBBffIIfffffff4si8si4sf',
    1, b'\0' * 3,      # animation: one-hand sword
    1.1, 1.0,          # speed, reach
    0, 0, 0.0, b'\0' * 4,
    0, 255, 1, 0,      # VATS, attack anim default, 1 projectile, embedded AV
    0.0, 0.0, 0, 0,
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, b'\0' * 4,
    AV_ONEHANDED, b'\0' * 8, -1, b'\0' * 4, 0.75)
assert len(dnam) == 100
crdt = struct.pack('<HHfB7sII', 12, 0, 1.0, 0, b'\0' * 7, 0, 0)
weap = record('WEAP', ID_WEAP, [
    sub('EDID', zstr('EnergySword')), obnd(-12, -3, -11, 15, 3, 61), sub('FULL', zstr('Energy Sword')),
    sub('MODL', zstr('EnergySword\\energysword.nif')),
    sub('EITM', struct.pack('<I', ID_ENCH)), sub('EAMT', struct.pack('<H', 3000)),
    sub('ETYP', struct.pack('<I', EQUP_EITHER_HAND)),
    sub('DESC', zstr('A blade of contained plasma. Crackles with shock on every hit.')),
    sub('WNAM', struct.pack('<I', ID_STAT)),
    sub('DATA', struct.pack('<IfH', 1500, 4.0, 22)),
    sub('DNAM', dnam), sub('CRDT', crdt), sub('VNAM', struct.pack('<I', 1)),
])
parts = [(REFINED_MALACHITE, 2), (QUICKSILVER_INGOT, 2), (DWARVEN_INGOT, 1)]
cobj = record('COBJ', ID_COBJ, [sub('EDID', zstr('EnergySwordRecipe')), sub('COCT', struct.pack('<I', len(parts))),
    *[sub('CNTO', struct.pack('<Ii', f, n)) for f, n in parts],
    sub('CNAM', struct.pack('<I', ID_WEAP)), sub('BNAM', struct.pack('<I', KYWD_CRAFTING_FORGE)), sub('NAM1', struct.pack('<H', 1))])

groups = [group('MGEF', [mgef]), group('ENCH', [ench]), group('STAT', [stat]), group('WEAP', [weap]), group('COBJ', [cobj])]
tes4 = record('TES4', 0, [sub('HEDR', struct.pack('<fiI', 1.71, 10, 0x805)), sub('CNAM', zstr('Ashton')),
    sub('SNAM', zstr('Energy sword. Requires EnergySword.dll (SKSE).')), sub('MAST', zstr('Skyrim.esm')),
    sub('DATA', struct.pack('<Q', 0))], flags=0x200)
with open(OUT, 'wb') as f:
    f.write(tes4 + b''.join(groups))
print('wrote', OUT)
