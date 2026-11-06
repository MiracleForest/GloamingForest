# GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
# Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
#   src/main/java/twilightforest/block/TrophyBlock.java
#   src/main/java/twilightforest/block/TrophyWallBlock.java
#   src/main/java/twilightforest/client/renderer/block/TrophyRenderer.java
#   src/main/java/twilightforest/item/TrophyItem.java
# Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
# Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
# of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#
# U5 (building): writes the Bedrock trophy blocks, their block geometry and the trophy items from
# F2's trophy geometries (assets/gen/geo/building, TrophyRenderer's floor and wall poses):
#
#   python tools/structures/building/trophies.py           # writes the files below
#   python tools/structures/building/trophies.py --check   # fails when a written file is stale
#
#   assets/pack/rp/models/blocks/building/<boss>[_wall]_trophy.geo.json
#   assets/pack/bp/blocks/building/<boss>[_wall]_trophy.json
#   assets/pack/bp/items/building/<boss>_trophy.json
#   (the held and worn trophies are F2's attachables, assets/gen/geo/building/rp/attachables)
#   assets/pack/rp/terrain_texture.d/building.json (with variants.py's entries), assets/pack/rp/item_texture.d/building.json
#
# TrophyBlock's ROTATION_16 is a 22.5 degree step: Bedrock turns a block in quarter turns only, so the
# floor trophy has four geometries (<id>_r0.._r3, the head turned 0 / 22.5 / 45 / 67.5 degrees about
# the block centre) and each rotation's permutation picks its step's geometry and turns the block by
# its quarter. (One geometry with the four steps under Molang bone_visibility does not work:
# bone_visibility hides a bone's own cubes, not its children's, so every step showed at once.) The knight phantom's helmet (phantom_layer_1) and the questing ram's lines are second
# textures: their faces name the material instances "helmet" / "lines". Box UVs become per-face UVs,
# `inflate` grows the cube (its UVs keep the cube's size) and `mirror` flips the faces as Bedrock does.

import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import variants  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GEO = os.path.join(ROOT, "assets", "gen", "geo", "building")
OUT_GEO = os.path.join(ROOT, "assets", "pack", "rp", "models", "blocks", "building")
OUT_BLOCKS = os.path.join(ROOT, "assets", "pack", "bp", "blocks", "building")
OUT_ITEMS = os.path.join(ROOT, "assets", "pack", "bp", "items", "building")
OUT_TERRAIN = os.path.join(ROOT, "assets", "pack", "rp", "terrain_texture.d", "building.json")
OUT_ITEM_TEX = os.path.join(ROOT, "assets", "pack", "rp", "item_texture.d", "building.json")

NS = "twilightforest:"
# BossVariant: trophy type (the GUI backdrop, item/<model>) per boss
BOSSES = {
    "naga": "trophy",
    "lich": "trophy",
    "hydra": "trophy",
    "ur_ghast": "trophy",
    "knight_phantom": "trophy_minor",
    "snow_queen": "trophy",
    "minoshroom": "trophy_minor",
    "alpha_yeti": "trophy_minor",
    "quest_ram": "trophy_quest",
}
# ColorHandler: the castle rune bricks' constant tints (0xFF00FFFF blue, 0xFFFF00FF pink, ...)
RUNE_COLORS = {"blue": "#00ffff", "pink": "#ff00ff", "yellow": "#ffff00", "violet": "#4b0082"}
# second textures of a trophy (F2 geometry suffix -> material instance)
EXTRA = {"_armor": "helmet", "_lines": "lines"}
FORMAT_BLOCK = "1.26.50"
FORMAT_ITEM = "1.21.60"
FORMAT_GEO = "1.21.0"

# TrophyBlock / TrophyWallBlock shapes (Java block pixels, x0 y0 z0 x1 y1 z1)
SHAPE = (4, 0, 4, 12, 8, 12)
GHAST_SHAPE = (4, 8, 4, 12, 16, 12)
YETI_X = (3.25, 0, 4.5, 12.75, 10, 11.5)
YETI_Z = (4.5, 0, 3.25, 11.5, 10, 12.75)
YETI_CORNER = (4.5, 0, 4.5, 11.5, 10, 11.5)
WALL = {"north": (4, 4, 8, 12, 12, 16), "south": (4, 4, 0, 12, 12, 8), "east": (0, 4, 4, 8, 12, 12), "west": (8, 4, 4, 16, 12, 12)}
YETI_WALL = {"north": (3.25, 4, 8.5, 12.75, 14.5, 16), "south": (3.25, 4, 0, 12.75, 14.5, 7.5),
             "east": (0, 4, 3.25, 7.5, 14.5, 12.75), "west": (8.5, 4, 3.25, 16, 14.5, 12.75)}
# the head's yaw per wall facing: TrophyRenderer 22.5 * (2 + get2DDataValue) * 4 = rotation steps
# 8 / 12 / 0 / 4 (south / west / north / east); F2's wall pose is the north one (step 0)
WALL_TURN = {"north": 0, "east": -90, "south": 180, "west": 90}


def r6(v):
    return round(v, 6)


def box_faces(cube):
    """Per-face UVs of a box-UV cube (Bedrock's box layout), mirrored like Bedrock's `mirror`."""
    u, v = cube["uv"]
    w, h, d = cube["size"]
    faces = {
        "north": ([u + d, v + d], [w, h]),
        "east": ([u, v + d], [d, h]),
        "south": ([u + d + w + d, v + d], [w, h]),
        "west": ([u + d + w, v + d], [d, h]),
        "up": ([u + d, v], [w, d]),
        "down": ([u + d + w, v + d], [w, -d]),
    }
    out = {k: {"uv": [r6(a) for a in uv], "uv_size": [r6(a) for a in size]} for k, (uv, size) in faces.items()}
    if cube.get("mirror"):
        out["east"], out["west"] = out["west"], out["east"]
        for f in out.values():
            f["uv"][0] = r6(f["uv"][0] + f["uv_size"][0])
            f["uv_size"][0] = -f["uv_size"][0]
    return out


def block_cube(cube, material):
    c = {}
    inflate = cube.get("inflate", 0)
    c["origin"] = [r6(a - inflate) for a in cube["origin"]]
    c["size"] = [r6(a + 2 * inflate) for a in cube["size"]]
    for k in ("pivot", "rotation"):
        if k in cube:
            c[k] = cube[k]
    uv = cube["uv"]
    faces = box_faces(cube) if isinstance(uv, list) else copy.deepcopy(uv)
    if material:
        for f in faces.values():
            f["material_instance"] = material
    c["uv"] = faces
    return c


def load_geo(name):
    path = os.path.join(GEO, "rp", "models", "entity", name + ".geo.json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)["minecraft:geometry"][0]


def parts(base):
    """The geometries of a trophy: (geometry, material instance or None)."""
    out = [(load_geo(base), None)]
    for suffix, material in EXTRA.items():
        if os.path.exists(os.path.join(GEO, "rp", "models", "entity", base + suffix + ".geo.json")):
            out.append((load_geo(base + suffix), material))
    return out


def trophy_bones(base, yaw):
    """The trophy's bones turned by `yaw` degrees (clockwise seen from above) about the block centre.

    F2's extract (frame 'world') holds the parts in Bedrock's geometry frame (x mirrored) below a root
    bone turned 180 degrees: the entity convention, where the actor renderer turns the model again.
    Block geometry is drawn as it is (geometry -z is north), so that root showed the back of the head
    on the wrong side of the block: the root gets the step's own yaw instead. The parts of a second
    texture hang below their own root (bone suffix _<n>)."""
    bones = []
    width = height = None
    for gi, (geo, material) in enumerate(parts(base)):
        desc = geo["description"]
        if gi == 0:
            width, height = desc["texture_width"], desc["texture_height"]
        for b in geo["bones"]:
            # the ur-ghast's tentacles wave (TrophyBlockEntity#tick, TrophyRenderer): the client mod
            # draws them animated (src/client/building/TrophyItems.cpp, a block renderer); the block
            # geometry keeps the head only
            if base.startswith("ur_ghast") and b["name"].startswith("tentacle"):
                continue
            nb = {"name": "%s_%d" % (b["name"], gi), "pivot": b.get("pivot", [0, 0, 0])}
            if "parent" in b:
                nb["parent"] = "%s_%d" % (b["parent"], gi)
                if b.get("rotation"):
                    nb["rotation"] = b["rotation"]
            else:
                # Bedrock's geometry frame (x mirrored) turns a positive y rotation clockwise seen
                # from above, the way TrophyBlock's rotation counts
                nb["rotation"] = [0, r6(yaw), 0]
            cubes = [block_cube(c, material) for c in b.get("cubes", [])]
            # per-face UVs are in the geometry's texture size: scale a second texture's
            if (desc["texture_width"], desc["texture_height"]) != (width, height):
                sx = width / desc["texture_width"]
                sy = height / desc["texture_height"]
                for c in cubes:
                    for f in c["uv"].values():
                        f["uv"] = [r6(f["uv"][0] * sx), r6(f["uv"][1] * sy)]
                        f["uv_size"] = [r6(f["uv_size"][0] * sx), r6(f["uv_size"][1] * sy)]
            if cubes:
                nb["cubes"] = cubes
            bones.append(nb)
    return bones, width, height


def geometry_doc(geometries):
    """One geometry file holding (identifier, bones, width, height) entries."""
    return {
        "format_version": FORMAT_GEO,
        "minecraft:geometry": [{
            "description": {
                "identifier": identifier,
                "texture_width": width,
                "texture_height": height,
                "visible_bounds_width": 3,
                "visible_bounds_height": 3,
                "visible_bounds_offset": [0, 0.75, 0],
            },
            "bones": bones,
        } for identifier, bones, width, height in geometries],
    }


def tex_key(path):
    return "tf_building_" + path.split("/")[-1]


def textures_of(base):
    with open(os.path.join(GEO, "bones", base + ".json"), encoding="utf-8") as f:
        bones = json.load(f)
    out = {}
    for g in bones["geometries"]:
        ident = g["identifier"]
        material = None
        for suffix, m in EXTRA.items():
            if ident.endswith(suffix):
                material = m
        out[material or "*"] = (g["texture"]["bedrock"], "alpha_test")  # Bedrock: one render method per block
    return out


def box(b):
    """A Java block-pixel box as a Bedrock collision / selection box (origin x mirrored: Bedrock's x is Java's)."""
    x0, y0, z0, x1, y1, z1 = b
    return {"origin": [r6(x0 - 8), r6(y0), r6(z0 - 8)], "size": [r6(x1 - x0), r6(y1 - y0), r6(z1 - z0)]}


def materials(base):
    out = {}
    for key, (path, method) in textures_of(base).items():
        out[key] = {"texture": tex_key(path), "render_method": method}
    return out


def floor_shape(boss, rotation):
    if boss == "ur_ghast":
        return GHAST_SHAPE
    if boss == "alpha_yeti":
        if rotation in (3, 4, 5, 11, 12, 13):
            return YETI_Z
        if rotation in (2, 6, 10, 14):
            return YETI_CORNER
        return YETI_X
    return SHAPE


def wall_shape(boss, facing):
    if boss == "ur_ghast":
        return GHAST_SHAPE
    if boss == "alpha_yeti":
        return YETI_WALL[facing]
    return WALL[facing]


def state(name):
    return "q.block_state('%s%s')" % (NS, name)


def base_components(boss):
    return {
        # Properties.of().instabreak()
        "minecraft:destructible_by_mining": {"seconds_to_destroy": 0},
        "minecraft:destructible_by_explosion": {"explosion_resistance": 0},
        "minecraft:light_dampening": 0,
    }


def floor_block(boss, geo_id):
    perms = []
    for r in range(16):
        quarter, step = r // 4, r % 4
        shape = floor_shape(boss, r)
        perms.append({
            "condition": "%s == %d" % (state("rotation"), r),
            "components": {
                # the step's own geometry (turned 22.5 * step), the block turned by the quarter
                "minecraft:geometry": "%s_r%d" % (geo_id, step),
                "minecraft:transformation": {"rotation": [0, -90 * quarter, 0]},
                # the selection box is turned with the block: give it in the block's own frame
                "minecraft:selection_box": unturn(shape, quarter),
                "minecraft:collision_box": unturn(shape, quarter),
            },
        })
    comps = base_components(boss)
    comps["minecraft:geometry"] = geo_id + "_r0"
    comps["minecraft:material_instances"] = materials(boss + "_trophy")
    return {
        "format_version": FORMAT_BLOCK,
        "minecraft:block": {
            "description": {
                "identifier": NS + boss + "_trophy",
                "menu_category": {"category": "none"},
                "states": {
                    NS + "powered": [True, False],  # F1 skeleton order (import.bool_defaults)
                    NS + "rotation": list(range(16)),
                },
            },
            "components": comps,
            "permutations": perms,
        },
    }


def unturn(shape_box, quarter):
    """The Bedrock box of a world-space Java box in the frame of a block turned by -90 * quarter degrees."""
    x0, y0, z0, x1, y1, z1 = shape_box
    # Bedrock turns the block (and its boxes) with the transformation: rotate the world box back
    for _ in range(quarter % 4):
        # inverse of one -90 degree turn (clockwise seen from above): (x, z) -> (z, 16 - x)
        x0, z0, x1, z1 = z0, 16 - x1, z1, 16 - x0
    return box((min(x0, x1), y0, min(z0, z1), max(x0, x1), y1, max(z0, z1)))


def wall_block(boss, geo_id):
    perms = []
    quarters = {"north": 0, "east": 1, "south": 2, "west": 3}
    for facing in ("north", "south", "west", "east"):
        shape = wall_shape(boss, facing)
        perms.append({
            "condition": "%s == '%s'" % (state("facing"), facing),
            "components": {
                "minecraft:transformation": {"rotation": [0, WALL_TURN[facing], 0]},
                "minecraft:selection_box": unturn(shape, quarters[facing]),
                "minecraft:collision_box": unturn(shape, quarters[facing]),
            },
        })
    comps = base_components(boss)
    comps["minecraft:geometry"] = geo_id
    comps["minecraft:material_instances"] = materials(boss + "_wall_trophy")
    return {
        "format_version": FORMAT_BLOCK,
        "minecraft:block": {
            "description": {
                "identifier": NS + boss + "_wall_trophy",
                "menu_category": {"category": "none"},
                "states": {
                    NS + "facing": ["north", "south", "west", "east"],
                    NS + "powered": [True, False],  # F1 skeleton order (import.bool_defaults)
                },
            },
            "components": comps,
            "permutations": perms,
        },
    }


def item(boss):
    return {
        "format_version": FORMAT_ITEM,
        "minecraft:item": {
            "description": {"identifier": NS + boss + "_trophy", "menu_category": {"category": "construction"}},
            "components": {
                # ISTER draws the head over the trophy type's backdrop sprite (item/trophy...): the
                # backdrop is the icon, the client mod draws the head on it
                "minecraft:icon": {"textures": {"default": "tf_building_item_" + BOSSES[boss]}},
                "minecraft:display_name": {"value": "tile.twilightforest:%s_trophy.name" % boss},
                "minecraft:block_placer": {"block": NS + boss + "_trophy", "replace_block_item": True},
                # AbstractTrophyBlock#getEquipmentSlot: HEAD
                "minecraft:wearable": {"slot": "slot.armor.head", "protection": 0},
                "minecraft:max_stack_size": 64,
            },
        },
    }


def dump(doc):
    return json.dumps(doc, indent=2) + "\n"


def outputs():
    files = {}
    terrain = {}
    for boss in BOSSES:
        for wall in (False, True):
            base = boss + ("_wall_trophy" if wall else "_trophy")
            geo_id = "geometry.twilightforest.building." + base
            if wall:
                geos = [(geo_id,) + trophy_bones(base, 0)]
            else:
                # one geometry per 22.5 degree step (TrophyBlock's ROTATION_16 within a quarter turn)
                geos = [(geo_id + "_r%d" % k,) + trophy_bones(base, k * 22.5) for k in range(4)]
            files[os.path.join(OUT_GEO, base + ".geo.json")] = dump(geometry_doc(geos))
            block = wall_block(boss, geo_id) if wall else floor_block(boss, geo_id)
            files[os.path.join(OUT_BLOCKS, base + ".json")] = dump(block)
            for path, _ in textures_of(base).values():
                terrain[tex_key(path)] = {"textures": path}
        files[os.path.join(OUT_ITEMS, boss + "_trophy.json")] = dump(item(boss))
    # the castle rune bricks' rune layer (ColorHandler: a constant colour per block), tinted in the
    # atlas (blocks.py writes the blocks); the blockstate's eight equally weighted models
    # castle_rune_brick_0..7 (castleblock_magic_0..7, one picked per position) are the atlas entry's
    # variations, which Bedrock also picks per position
    for color, rgb in RUNE_COLORS.items():
        terrain["tf_building_rune_" + color] = {"textures": {"variations": [
            {"path": "textures/twilightforest/block/castleblock_magic_%d" % i, "tint_color": rgb, "weight": 1} for i in range(8)]}}
    # TF's random model variants as per-face texture variations (variants.py writes the rest)
    terrain.update(variants.terrain())
    files[OUT_TERRAIN] = dump(dict(sorted(terrain.items())))
    items = {"tf_building_item_" + t: {"textures": "textures/twilightforest/item/" + t} for t in sorted(set(BOSSES.values()))}
    files[OUT_ITEM_TEX] = dump(items)
    return files


def main():
    check = "--check" in sys.argv
    stale = []
    for path, text in outputs().items():
        old = None
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                old = f.read()
        if old == text:
            continue
        if check:
            stale.append(os.path.relpath(path, ROOT))
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote", os.path.relpath(path, ROOT))
    if stale:
        print("stale:", *stale, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
