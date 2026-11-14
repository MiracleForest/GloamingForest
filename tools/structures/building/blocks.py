# GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
# Translated from the upstream 4.8 source (commit 9e40a757):
#   src/main/java/gloamingforest/block/RopeBlock.java
#   src/main/java/gloamingforest/block/IronLadderBlock.java (LadderBlock's shapes)
# Copyright (c) the upstream authors (see NOTICE).
# Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
# of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#
# U5 (building): hand-made BP files of the blocks whose shape changes with their states (F1's
# skeleton keeps the model; F11's family kit would only add the liquid detection):
#
#   python tools/structures/building/blocks.py           # writes assets/pack/bp/blocks/building/{rope,iron_ladder}.json
#   python tools/structures/building/blocks.py --check   # fails when a written file is stale
#
# - rope: RopeBlock#getShape (3 px strands along the active axes) as the selection box (their
#   bounds: Bedrock has one selection box), the horizontal strands as collision boxes (Java's
#   getCollisionShape keeps them for entities above them: the tightrope; Bedrock has no per-entity
#   collision, so the strand is always solid), no collision for a vertical strand (climbable).
# - iron_ladder: LadderBlock's 3 px shapes per facing.
# Both are waterloggable (SimpleWaterloggedBlock: Bedrock's liquid detection, as the kit writes it).

import copy
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SKELETON = os.path.join(ROOT, "assets", "gen", "import", "building", "bp", "blocks")
OUT = os.path.join(ROOT, "assets", "pack", "bp", "blocks", "building")
NS = "gloamingforest:"

WATERLOGGABLE = {"detection_rules": [{"liquid_type": "water", "can_contain_liquid": True, "on_liquid_touches": "no_reaction"}]}

ROPE = {"x": (0, 6.5, 6.5, 16, 9.5, 9.5), "y": (6.5, 0, 6.5, 9.5, 16, 9.5), "z": (6.5, 6.5, 0, 9.5, 9.5, 16)}
LADDER = {"north": (0, 0, 13, 16, 16, 16), "south": (0, 0, 0, 16, 16, 3), "west": (13, 0, 0, 16, 16, 16), "east": (0, 0, 0, 3, 16, 16)}


def box(b):
    x0, y0, z0, x1, y1, z1 = b
    return {"origin": [x0 - 8, y0, z0 - 8], "size": [x1 - x0, y1 - y0, z1 - z0]}


def bounds(boxes):
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), min(b[2] for b in boxes),
            max(b[3] for b in boxes), max(b[4] for b in boxes), max(b[5] for b in boxes))


def skeleton(name):
    with open(os.path.join(SKELETON, name + ".json"), encoding="utf-8") as f:
        return json.load(f)


def state(name, value):
    if isinstance(value, bool):
        return ("" if value else "!") + "q.block_state('%s%s')" % (NS, name)
    return "q.block_state('%s%s') == '%s'" % (NS, name, value)


def rope():
    doc = skeleton("rope")
    comps = doc["minecraft:block"]["components"]
    comps["minecraft:liquid_detection"] = copy.deepcopy(WATERLOGGABLE)
    comps["minecraft:collision_box"] = False
    comps["minecraft:selection_box"] = box(ROPE["y"])
    perms = []
    for x in (False, True):
        for y in (False, True):
            for z in (False, True):
                active = [a for a, on in (("x", x), ("y", y), ("z", z)) if on]
                if not active:
                    continue
                solid = [ROPE[a] for a in active if a != "y"]
                perms.append({
                    "condition": " && ".join([state("x", x), state("y", y), state("z", z)]),
                    "components": {
                        "minecraft:selection_box": box(bounds([ROPE[a] for a in active])),
                        "minecraft:collision_box": (box(solid[0]) if len(solid) == 1 else [box(s) for s in solid]) if solid else False,
                    },
                })
    doc["minecraft:block"]["permutations"] = perms
    return doc


def ladder():
    doc = skeleton("iron_ladder")
    comps = doc["minecraft:block"]["components"]
    comps["minecraft:liquid_detection"] = copy.deepcopy(WATERLOGGABLE)
    perms = []
    for facing, b in LADDER.items():
        perms.append({"condition": state("facing", facing), "components": {"minecraft:selection_box": box(b), "minecraft:collision_box": box(b)}})
    doc["minecraft:block"]["permutations"] = perms
    return doc


def rune_brick(color):
    """castle_rune_brick_0: the castle brick with the rune layer (castleblock_magic_0) over it, tinted
    with the block's constant colour (ColorHandler; the atlas entry gf_building_rune_<color> of
    trophies.py). upstream picks one of eight rune layers per position (blockstate variants): the atlas
    entry's eight variations, picked per position by Bedrock."""
    doc = skeleton(color + "_castle_rune_brick")
    comps = doc["minecraft:block"]["components"]
    brick = {"texture": "gf_block_castle_brick", "render_method": "alpha_test"}
    runes = {"texture": "gf_building_rune_" + color, "render_method": "alpha_test"}
    comps["minecraft:material_instances"] = {"*": brick, "t_gloamingforest_block_castle_brick": brick,
                                              **{f: runes for f in ("north", "south", "east", "west", "up", "down")}}
    # Properties.ofFullCopy(CASTLE_BRICK): a full opaque block (the skeleton's 0 let light through)
    comps.pop("minecraft:light_dampening", None)
    return doc


# MiniatureStructureBlock models whose faces carry tintindex 1 (grass top and side overlay):
# ColorHandler tints them with BiomeColors.getAverageFoliageColor (the client's own biome blend:
# Bedrock's tint method "default_foliage", vanilla clients and the client mod alike)
TINTED_MINIATURES = {"gloaming_portal": ("grass", "grass_side"), "naga_courtyard": ("grass", "grass_side")}


def miniature(name):
    doc = skeleton(name + "_miniature_structure")
    block = doc["minecraft:block"]
    for comps in [block["components"]] + [p["components"] for p in block.get("permutations", [])]:
        for key in TINTED_MINIATURES[name]:
            inst = comps.get("minecraft:material_instances", {}).get(key)
            if inst is not None:
                inst["tint_method"] = "default_foliage"
    return doc


def families():
    """assets/data/families/building.json: F1's family data (which it replaces as the manual layer)
    plus the components the kit merges into two blocks it generates."""
    with open(os.path.join(ROOT, "assets", "gen", "import", "building", "data", "gf", "families", "building.json"), encoding="utf-8") as f:
        entries = json.load(f)
    entries.append({"id": NS + "knightmetal_block", "family": "waterloggable", "params": {
        "comment": "U5: KnightmetalBlock.SHAPE box(1, 1, 1, 15, 15, 15): entities on it stand inside the block (entityInside)",
        "components": {"minecraft:collision_box": box((1, 1, 1, 15, 15, 15)), "minecraft:selection_box": box((1, 1, 1, 15, 15, 15))}}})
    entries.append({"id": NS + "arctic_fur_block", "family": "connected_texture", "params": {
        "comment": "U5: ArcticFurBlock#getDestroyProgress: 0.2 a tick with shears (5 ticks: 0.8 * 1.5 / 4.8 s)",
        "components": {"minecraft:destructible_by_mining": {"seconds_to_destroy": 0.8, "item_specific_speeds": [{"item": "minecraft:shears", "destroy_speed": 4.8}]}}}})
    return entries


def outputs():
    return {
        os.path.join(OUT, "rope.json"): rope(),
        os.path.join(OUT, "iron_ladder.json"): ladder(),
        os.path.join(ROOT, "assets", "data", "families", "building.json"): families(),
        **{os.path.join(OUT, c + "_castle_rune_brick.json"): rune_brick(c) for c in ("blue", "pink", "yellow", "violet")},
        **{os.path.join(OUT, m + "_miniature_structure.json"): miniature(m) for m in TINTED_MINIATURES},
    }


def main():
    check = "--check" in sys.argv
    stale = []
    for path, doc in outputs().items():
        text = json.dumps(doc, indent=2) + "\n"
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
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
