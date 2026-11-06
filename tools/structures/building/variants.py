# GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
# U5 (building): TF's random block model variants (the generated blockstates of TF 4.8,
# src/generated/resources/assets/twilightforest/blockstates/<block>.json) as Bedrock texture
# variations:
#
#   python tools/structures/building/variants.py           # writes the files below
#   python tools/structures/building/variants.py --check   # fails when a written file is stale
#
#   assets/pack/rp/textures/blocks/building/tf-derived/<texture>_<transform>.png (+ SOURCES.json)
#   assets/pack/bp/blocks/building/<block>.json   (the imported block, each face on its own atlas key)
#   terrain entries tf_building_var_<block>_<face> (merged into terrain_texture.d/building.json by
#   trophies.py, which imports terrain())
#
# A Java blockstate with a list of models picks one per position (equal weights). Bedrock cannot
# pick a model per position, but an atlas entry with "variations" picks a texture per position.
# Each variant is a model turned by the blockstate's x / y (BlockModelRotation: Ry(-y) * Rx(-x),
# without uvlock) or a "_flipped" model (every face's uv [0, 16, 16, 0]: upside down). For every face
# of the block this script works out which face of the model ends up there and how its texture is
# turned or mirrored, writes that texture once, and lists the variants in the blockstate's order on
# the face's atlas key, so one variant index means the same model on every face. Faces use Java's
# default UV frames (FaceBakery), which Bedrock's full block faces share.
#
# The derived textures are TF textures turned or mirrored: CC BY-NC-SA 4.0 (tf-derived/SOURCES.json).
# The towerwood blocks keep their tint (ColorHandler, client mod) on placed blocks; their block
# items get an untinted item visual (ColorHandler returns -1 without a position: inventory items are
# drawn untinted).

import copy
import json
import os
import struct
import sys
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GEN = os.path.join(ROOT, "assets", "gen", "import")
OUT_TEX = os.path.join(ROOT, "assets", "pack", "rp", "textures", "blocks", "building", "tf-derived")
TEX_PATH = "textures/blocks/building/tf-derived/"
OUT_BLOCKS = os.path.join(ROOT, "assets", "pack", "bp", "blocks", "building")
TF_TEX = "src/main/resources/assets/twilightforest/textures/block/"
NS = "twilightforest:"

# blockstate variants: (texture, x, y, flipped); the textures of TF's models
ROT10 = [(0, 0), (0, 90), (0, 180), (90, 0), (90, 90), (90, 180), (180, 0), (180, 90), (180, 180), (270, 270)]
VARIANTS = {
    "towerwood": [("towerwood", 0, 0, False), ("towerwood", 0, 0, True)],
    "cracked_towerwood": [("cracked_towerwood", 0, 0, False), ("cracked_towerwood", 0, 0, True),
                          ("cracked_towerwood_alt", 0, 0, False), ("cracked_towerwood_alt", 0, 0, True)],
    "mossy_towerwood": [("mossy_towerwood", 0, 0, False), ("mossy_towerwood", 0, 0, True)],
    "deadrock": [("deadrock", 0, 0, False), ("deadrock", 0, 180, False), ("deadrock", 0, 90, False), ("deadrock", 0, 270, False)],
    "cracked_deadrock": [("cracked_deadrock", x, y, False) for x, y in ROT10],
    "weathered_deadrock": [("weathered_deadrock", x, y, False) for x, y in ROT10],
    "mazestone": [("mazestone", 90, 90, False), ("mazestone", 270, 270, False)],
    "castle_roof_tile": [("castle_roof_tile", x, y, False) for x, y in ROT10],
    "carminite_block": [("carminite_block", x, y, False) for x, y in ROT10],
    "steeleaf_block": [("steeleaf_block", x, y, False) for x, y in ROT10],
}
TINTED = {"towerwood", "cracked_towerwood", "mossy_towerwood"}
# carminite_block's own geometry (two layers): material instance -> (face, texture)
CARMINITE = {
    "bottom": ("down", "carminite_block"), "top": ("up", "carminite_block"),
    "north": ("north", "carminite_block"), "south": ("south", "carminite_block"),
    "east": ("east", "carminite_block"), "west": ("west", "carminite_block"),
    "bottom2": ("down", "carminite_block_overlay"), "top2": ("up", "carminite_block_overlay"),
    "north2": ("north", "carminite_block_overlay"), "south2": ("south", "carminite_block_overlay"),
    "east2": ("east", "carminite_block_overlay"), "west2": ("west", "carminite_block_overlay"),
}

# outward normal, u and v of each face (FaceBakery's default UVs of a full cube)
FACES = {
    "up": ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
    "down": ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
    "north": ((0, 0, -1), (-1, 0, 0), (0, -1, 0)),
    "south": ((0, 0, 1), (1, 0, 0), (0, -1, 0)),
    "west": ((-1, 0, 0), (0, 0, 1), (0, -1, 0)),
    "east": ((1, 0, 0), (0, 0, -1), (0, -1, 0)),
}
FACE_ORDER = ["up", "down", "north", "south", "east", "west"]


def rot_x(v, deg):
    c, s = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}[deg % 360]
    x, y, z = v
    return (x, y * c - z * s, y * s + z * c)


def rot_y(v, deg):
    c, s = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}[deg % 360]
    x, y, z = v
    return (x * c + z * s, y, -x * s + z * c)


def rotate(v, x, y):
    # BlockModelRotation: new Quaternionf().rotateYXZ(-y, -x, 0) applied to the model
    return rot_y(rot_x(v, -x), -y)


def dot(a, b):
    return sum(i * j for i, j in zip(a, b))


def transform_of(face, x, y, flipped):
    """The texture transform seen on `face` of the turned model: (a, b, c, d) with
    (u, v) = (a*s + b*t, c*s + d*t) in centred coordinates of the face's own (s, t) frame."""
    _, su, sv = FACES[face]
    for src, (n, u, v) in FACES.items():
        if rotate(n, x, y) != FACES[face][0]:
            continue
        if flipped:
            v = tuple(-i for i in v)
        u2, v2 = rotate(u, x, y), rotate(v, x, y)
        # a point s*su + t*sv of the face lies at u = p.u2, v = p.v2 of the source texture
        return (dot(su, u2), dot(sv, u2), dot(su, v2), dot(sv, v2))
    raise ValueError(face)


NAMES = {(1, 0, 0, 1): "", (0, -1, 1, 0): "_r90", (-1, 0, 0, -1): "_r180", (0, 1, -1, 0): "_r270",
         (1, 0, 0, -1): "_fv", (-1, 0, 0, 1): "_fh", (0, 1, 1, 0): "_ft", (0, -1, -1, 0): "_fa"}


# --- PNG (8-bit RGB / RGBA / palette / grey) ---------------------------------------------------

def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, plte, trns = 8, b"", None, None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and interlace == 0, path
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        ftype, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for k in range(stride):
            a = line[k - bpp] if k >= bpp else 0
            b = prev[k]
            c = prev[k - bpp] if k >= bpp else 0
            if ftype == 1:
                line[k] = (line[k] + a) & 255
            elif ftype == 2:
                line[k] = (line[k] + b) & 255
            elif ftype == 3:
                line[k] = (line[k] + (a + b) // 2) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[k] = (line[k] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        row = []
        for xx in range(w):
            s = line[xx * bpp:(xx + 1) * bpp]
            if ctype == 6:
                row.append(tuple(s))
            elif ctype == 2:
                row.append((s[0], s[1], s[2], 255))
            elif ctype == 3:
                idx = s[0]
                alpha = trns[idx] if trns is not None and idx < len(trns) else 255
                row.append((plte[3 * idx], plte[3 * idx + 1], plte[3 * idx + 2], alpha))
            elif ctype == 0:
                row.append((s[0], s[0], s[0], 255))
            else:
                row.append((s[0], s[0], s[0], s[1]))
        px.append(row)
    return w, h, px


def write_png(w, h, px):
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in px)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def apply(img, t):
    w, h, px = img
    assert w == h, "square textures only"
    a, b, c, d = t
    out = []
    for j in range(h):
        row = []
        for i in range(w):
            s, tt = 2 * i - (w - 1), 2 * j - (h - 1)  # doubled centred coordinates
            u, v = a * s + b * tt, c * s + d * tt
            row.append(px[(v + h - 1) // 2][(u + w - 1) // 2])
        out.append(row)
    return out


def source_png(tex):
    for unit in sorted(os.listdir(GEN)):
        p = os.path.join(GEN, unit, "rp", "textures", "twilightforest", "block", tex + ".png")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(tex)


def gen_block(block):
    for unit in sorted(os.listdir(GEN)):
        p = os.path.join(GEN, unit, "bp", "blocks", block + ".json")
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f:
                return json.load(f)
    raise FileNotFoundError(block)


def key(block, inst):
    return "tf_building_var_%s_%s" % (block, inst)


def outputs():
    files, sources, terrain = {}, {}, {}

    def texture(tex, t):
        name = tex + NAMES[t]
        if not NAMES[t]:
            return "textures/twilightforest/block/" + tex
        path = os.path.join(OUT_TEX, name + ".png")
        if path not in files:
            img = read_png(source_png(tex))
            files[path] = write_png(img[0], img[1], apply(img, t))
            sources[name + ".png"] = TF_TEX + tex + ".png"
        return TEX_PATH + name

    for block, variants in VARIANTS.items():
        doc = gen_block(block)
        comps = doc["minecraft:block"]["components"]
        mats = comps["minecraft:material_instances"]
        if block in TINTED:
            # ColorHandler's towerwood colour on placed blocks (the only place it is set: the
            # generated block has no tint)
            for inst in list(mats):
                mats[inst] = dict(mats[inst], tint_method="dry_foliage")
        insts = CARMINITE if block == "carminite_block" else {f: (f, None) for f in FACE_ORDER}
        for inst, (face, tex_override) in insts.items():
            entries = []
            for tex, x, y, flipped in variants:
                tex = tex_override or tex
                entries.append({"path": texture(tex, transform_of(face, x, y, flipped)), "weight": 1})
            terrain[key(block, inst)] = {"textures": {"variations": entries}}
            mats[inst] = dict(mats.get(inst, mats["*"]), texture=key(block, inst))
        mats["*"] = dict(mats["*"], texture=key(block, "north"))
        if block in TINTED:
            # ColorHandler: no tint without a position (the block item in the inventory, in hand)
            untinted = {k: v for k, v in mats["*"].items() if k != "tint_method"}
            untinted["texture"] = "tf_building_icon_" + block
            terrain["tf_building_icon_" + block] = {"textures": "textures/twilightforest/block/" + block}
            comps["minecraft:item_visual"] = {
                "geometry": comps.get("minecraft:geometry", "minecraft:geometry.full_block"),
                "material_instances": {"*": untinted},
            }
        files[os.path.join(OUT_BLOCKS, block + ".json")] = (json.dumps(doc, indent=2) + "\n").encode("utf-8")
    files[os.path.join(OUT_TEX, "SOURCES.json")] = (json.dumps(dict(sorted(sources.items())), indent=2) + "\n").encode("utf-8")
    return files, terrain


def terrain():
    return outputs()[1]


def main():
    check = "--check" in sys.argv
    stale = []
    files, _ = outputs()
    for path, data in files.items():
        old = None
        if os.path.exists(path):
            with open(path, "rb") as f:
                old = f.read()
        if old == data:
            continue
        if check:
            stale.append(os.path.relpath(path, ROOT))
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        print("wrote", os.path.relpath(path, ROOT))
    if os.path.isdir(OUT_TEX):
        for name in os.listdir(OUT_TEX):
            if os.path.join(OUT_TEX, name) not in files:
                stale.append(os.path.relpath(os.path.join(OUT_TEX, name), ROOT) + " (not written by this script)")
    if stale:
        print("stale:", *stale, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
