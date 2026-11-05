# GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
# Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
#   src/main/java/twilightforest/block/TrophyWallBlock.java (getDescriptionId: the trophy item's)
#   src/main/java/twilightforest/init/TFBannerPatterns.java (block.minecraft.banner.twilightforest.*)
# Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
# Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
# of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#
# U5 (building): the unit's Bedrock text keys (assets/pack/rp/texts.d/<locale>/building.lang, the
# manual layer over F1's per-key):
# - the wall trophies' block names are their trophy's name (TrophyWallBlock#getDescriptionId returns
#   the standing trophy's), in every locale F1 wrote building texts for;
# - Bedrock's banner layer lines item.banner.<pattern>.<colour> of TF's nine patterns (Bedrock names a
#   banner layer by the pattern's name, banners.py's tf_<boss>) from TF's
#   block.minecraft.banner.twilightforest.<asset>.<colour> (F9's imported lang), en_US where a locale
#   has none.
#
#   python tools/structures/building/texts.py           # writes the files
#   python tools/structures/building/texts.py --check   # fails when a written file is stale

import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GEN_TEXTS = os.path.join(ROOT, "assets", "gen", "import", "building", "rp", "texts.d")
F9_LANG = os.path.join(ROOT, "assets", "gen", "import", "item", "lang")
OUT = os.path.join(ROOT, "assets", "pack", "rp", "texts.d")

BOSSES = ["naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti", "quest_ram"]
# TF's banner_pattern asset ids per Bedrock pattern name (Banners.cpp)
PATTERNS = {"tf_" + b: b for b in BOSSES}
# Java DyeColor -> Bedrock's colour suffix of item.banner.* keys
COLORS = {"white": "white", "orange": "orange", "magenta": "magenta", "light_blue": "lightBlue", "yellow": "yellow",
          "lime": "lime", "pink": "pink", "gray": "gray", "light_gray": "silver", "cyan": "cyan", "purple": "purple",
          "blue": "blue", "brown": "brown", "green": "green", "red": "red", "black": "black"}


def read_lang(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                out[k] = v
    return out


def read_json(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def outputs():
    files = {}
    english = read_json(os.path.join(F9_LANG, "en_US", "item.json"))
    for locale in sorted(os.listdir(GEN_TEXTS)):
        gen = read_lang(os.path.join(GEN_TEXTS, locale, "building.lang"))
        tf = read_json(os.path.join(F9_LANG, locale, "item.json"))
        lines = []
        for b in BOSSES:
            name = gen.get("tile.twilightforest:%s_trophy.name" % b)
            if name:
                lines.append("tile.twilightforest:%s_wall_trophy.name=%s" % (b, name))
        for pattern, asset in PATTERNS.items():
            for java, bedrock in COLORS.items():
                key = "block.minecraft.banner.twilightforest.%s.%s" % (asset, java)
                text = tf.get(key) or english.get(key)
                if text:
                    lines.append("item.banner.%s.%s=%s" % (pattern, bedrock, text))
        files[os.path.join(OUT, locale, "building.lang")] = "\n".join(lines) + "\n"
    return files


def main():
    check = "--check" in sys.argv
    stale = []
    for path, text in outputs().items():
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
