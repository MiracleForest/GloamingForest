// GloamingForest import tool (contract "import", unit F1): TF's data (LGPL datagen output and
// hand-written data): tags, loot tables and modifiers, recipes, advancements, worldgen, dimension,
// twilight/* registries, data maps, banner patterns, damage types, jukebox songs, trim materials,
// enchantments and functions, copied into data/<namespace>/** of the owning unit (Java format, kept by
// id for the runtime contracts: loot, recipes, tags, data maps, landmarks, ...).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace GfImport
{
    public sealed partial class Ctx
    {
        /// <summary>The (kind, owner) of a data file data/<ns>/<rest>.</summary>
        Tuple<string, string> DataOwner(string ns, string rest)
        {
            var p = rest.Split('/');
            string reg = p[0];
            string path = Ctx.StripExt(string.Join("/", p.Skip(1)));
            string id = ns + ":" + path;
            switch (reg)
            {
                case "tags":
                    {
                        string treg = p[1];
                        int skip = 2;
                        if (treg == "worldgen") { treg = "worldgen/" + p[2]; skip = 3; }
                        string tpath = StripExt(string.Join("/", p.Skip(skip)));
                        return Tuple.Create("tags", OwnerOf("tag", treg + "/#" + ns + ":" + tpath));
                    }
                case "loot_table": return Tuple.Create("loot", OwnerOf("loot_table", id));
                case "loot_modifiers": return Tuple.Create("loot", OwnerOf("loot_modifier", id));
                case "recipe": return Tuple.Create("recipes", OwnerOf("recipe", id));
                case "advancement": return Tuple.Create("advancements", OwnerOf("advancement", id));
                case "worldgen":
                    {
                        string wk = p[1];
                        string wid = ns + ":" + StripExt(string.Join("/", p.Skip(2)));
                        return Tuple.Create("worldgen", OwnerOf(wk, wid));
                    }
                case "dimension": return Tuple.Create("worldgen", OwnerOf("dimension", id));
                case "dimension_type": return Tuple.Create("worldgen", OwnerOf("dimension_type", id));
                case "twilight":
                    {
                        string treg = p[1];
                        string eid = ns + ":" + StripExt(string.Join("/", p.Skip(2)));
                        return Tuple.Create("registries", OwnerOf("registry_entry", treg + "/" + eid) ?? OwnerOf("registry", treg));
                    }
                case "data_maps": return Tuple.Create("datamaps", OwnerOf("data_map", ns + ":" + StripExt(p.Last())));
                case "banner_pattern": case "damage_type": case "jukebox_song": case "trim_material": case "enchantment":
                    return Tuple.Create("registries", OwnerOf(reg, id));
                case "function": return Tuple.Create("registries", OwnerOf("function", id));
                default: return Tuple.Create("registries", (string)null);
            }
        }

        // TF data the port does not carry: compat-only content (deviations in docs/spec/units.json)
        static readonly string[] SkippedData = { "curios/", "museumexhibits/" };

        public void ImportData(HashSet<string> kinds)
        {
            foreach (var res in new[] { GenRes, MainRes })
            {
                string dataDir = Path.Combine(res, "data");
                if (!Directory.Exists(dataDir)) continue;
                foreach (var nsDir in Directory.GetDirectories(dataDir).OrderBy(x => x, StringComparer.Ordinal))
                {
                    string ns = Path.GetFileName(nsDir);
                    foreach (var rel in Files(nsDir))
                    {
                        if (SkippedData.Any(s => rel.StartsWith(s, StringComparison.Ordinal))) continue;
                        var ko = DataOwner(ns, rel);
                        if (!kinds.Contains(ko.Item1)) continue;
                        string owner = ko.Item2 ?? ImportUnit;
                        string file = Path.Combine(nsDir, rel);
                        string outRel = "data/" + ns + "/" + rel;
                        byte[] bytes = rel.EndsWith(".png", StringComparison.Ordinal) ? File.ReadAllBytes(file) : new UTF8Encoding(false).GetBytes(ReadText(file));
                        Emit(ko.Item1, owner, outRel, bytes);
                    }
                }
            }
        }

        // ---- tag membership (used by item tags and recipe ingredients) ------------------------------------

        readonly Dictionary<string, J> tagFiles = new Dictionary<string, J>(StringComparer.Ordinal);

        /// <summary>The imported tag file data/<ns>/tags/<registry>/<path>.json for "#ns:path" in registry, or null.</summary>
        public J TagFile(string registry, string tag)
        {
            string key = registry + "|" + tag;
            J j;
            if (tagFiles.TryGetValue(key, out j)) return j;
            int c = tag.IndexOf(':');
            string ns = tag.Substring(0, c), path = tag.Substring(c + 1);
            j = null;
            foreach (var res in new[] { GenRes, MainRes })
            {
                string f = Path.Combine(res, "data", ns, "tags", registry, path.Replace('/', Path.DirectorySeparatorChar) + ".json");
                if (File.Exists(f)) { j = ReadJson(f); break; }
            }
            tagFiles[key] = j;
            return j;
        }

        // Minecraft 1.21.1's wood types (Blocks / ItemTags order) and dye colours (declared before BaseItemTags, which uses them)
        static readonly string[] VanillaWoods = { "oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "crimson", "warped", "mangrove", "bamboo", "cherry" };
        static readonly string[] DyeColors = { "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black" };

        // NeoForge / vanilla tags TF's recipes rely on, which TF's data only extends (members as on Java 1.21.1)
        public static readonly Dictionary<string, string[]> BaseItemTags = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            { "c:rods/wooden", new[] { "minecraft:stick" } },
            { "c:rods/blaze", new[] { "minecraft:blaze_rod" } },
            { "c:ingots/iron", new[] { "minecraft:iron_ingot" } },
            { "c:ingots/gold", new[] { "minecraft:gold_ingot" } },
            { "c:ingots/copper", new[] { "minecraft:copper_ingot" } },
            { "c:nuggets/iron", new[] { "minecraft:iron_nugget" } },
            { "c:nuggets/gold", new[] { "minecraft:gold_nugget" } },
            { "c:dusts/redstone", new[] { "minecraft:redstone" } },
            { "c:dusts/glowstone", new[] { "minecraft:glowstone_dust" } },
            { "c:strings", new[] { "minecraft:string" } },
            { "c:leathers", new[] { "minecraft:leather" } },
            { "c:paper", new[] { "minecraft:paper" } },
            { "c:glass_blocks", new[] { "minecraft:glass" } },
            { "c:glass_panes/colorless", new[] { "minecraft:glass_pane" } },
            { "c:ores/redstone", new[] { "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore" } },
            { "c:storage_blocks/diamond", new[] { "minecraft:diamond_block" } },
            { "c:storage_blocks/gold", new[] { "minecraft:gold_block" } },
            { "c:storage_blocks/iron", new[] { "minecraft:iron_block" } },
            { "c:chests/wooden", new[] { "minecraft:chest", "minecraft:trapped_chest" } },
            { "c:chests/trapped", new[] { "minecraft:trapped_chest" } },
            // vanilla item tags TF's recipe advancements unlock by (same item names on Bedrock); "#x" includes tag x
            { "minecraft:boats", new[] { "minecraft:oak_boat", "minecraft:spruce_boat", "minecraft:birch_boat", "minecraft:jungle_boat", "minecraft:acacia_boat",
                "minecraft:dark_oak_boat", "minecraft:mangrove_boat", "minecraft:bamboo_raft", "minecraft:cherry_boat", "#minecraft:chest_boats" } },
            { "minecraft:chest_boats", new[] { "minecraft:oak_chest_boat", "minecraft:spruce_chest_boat", "minecraft:birch_chest_boat", "minecraft:jungle_chest_boat",
                "minecraft:acacia_chest_boat", "minecraft:dark_oak_chest_boat", "minecraft:mangrove_chest_boat", "minecraft:bamboo_chest_raft", "minecraft:cherry_chest_boat" } },
            { "minecraft:saplings", new[] { "minecraft:oak_sapling", "minecraft:spruce_sapling", "minecraft:birch_sapling", "minecraft:jungle_sapling",
                "minecraft:acacia_sapling", "minecraft:dark_oak_sapling", "minecraft:azalea", "minecraft:flowering_azalea", "minecraft:mangrove_propagule",
                "minecraft:cherry_sapling" } },
            // the vanilla members of the tags Bedrock also has (Recipes.BedrockItemTags), for the recipes that take one
            // item per slot (furnace, smithing); same item names on Bedrock
            { "minecraft:planks", VanillaWoods.Select(w => "minecraft:" + w + "_planks").ToArray() },
            { "minecraft:wooden_slabs", VanillaWoods.Select(w => "minecraft:" + w + "_slab").ToArray() },
            { "minecraft:wool", DyeColors.Select(c => "minecraft:" + c + "_wool").ToArray() },
            { "minecraft:logs", VanillaWoods.Where(w => w != "bamboo").SelectMany(w => w == "crimson" || w == "warped"
                ? new[] { w + "_stem", "stripped_" + w + "_stem", w + "_hyphae", "stripped_" + w + "_hyphae" }
                : new[] { w + "_log", w + "_wood", "stripped_" + w + "_log", "stripped_" + w + "_wood" }).Select(x => "minecraft:" + x).ToArray() },
        };

        // The vanilla block tags (Minecraft 1.21.1, data/minecraft/tags/block) the tool tags reach through other tags,
        // with only their tag references: TF's blocks join them through TF's own files for the referenced tags
        // (TF adds its planks to #minecraft:planks, which #minecraft:mineable/axe includes, ...). Vanilla blocks are
        // not listed (the import only places TF blocks).
        public static readonly Dictionary<string, string[]> BaseBlockTagRefs = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            { "minecraft:mineable/axe", new[] { "minecraft:banners", "minecraft:fence_gates", "minecraft:logs", "minecraft:planks", "minecraft:saplings",
                "minecraft:signs", "minecraft:wooden_buttons", "minecraft:wooden_doors", "minecraft:wooden_fences", "minecraft:wooden_pressure_plates",
                "minecraft:wooden_slabs", "minecraft:wooden_stairs", "minecraft:wooden_trapdoors", "minecraft:all_hanging_signs", "minecraft:bamboo_blocks" } },
            { "minecraft:mineable/pickaxe", new[] { "minecraft:stone_buttons", "minecraft:walls", "minecraft:shulker_boxes", "minecraft:anvil",
                "minecraft:cauldrons", "minecraft:rails" } },
            { "minecraft:mineable/shovel", new[] { "minecraft:concrete_powder" } },
            { "minecraft:sword_efficient", new[] { "minecraft:leaves", "minecraft:saplings", "minecraft:small_flowers", "minecraft:crops" } },
            { "minecraft:signs", new[] { "minecraft:standing_signs", "minecraft:wall_signs" } },
            { "minecraft:all_hanging_signs", new[] { "minecraft:ceiling_hanging_signs", "minecraft:wall_hanging_signs" } },
            { "minecraft:logs", new[] { "minecraft:logs_that_burn", "minecraft:crimson_stems", "minecraft:warped_stems" } },
            { "minecraft:logs_that_burn", new[] { "minecraft:dark_oak_logs", "minecraft:oak_logs", "minecraft:acacia_logs", "minecraft:birch_logs",
                "minecraft:jungle_logs", "minecraft:spruce_logs", "minecraft:mangrove_logs", "minecraft:cherry_logs" } },
            { "minecraft:incorrect_for_wooden_tool", new[] { "minecraft:needs_diamond_tool", "minecraft:needs_iron_tool", "minecraft:needs_stone_tool" } },
            { "minecraft:incorrect_for_gold_tool", new[] { "minecraft:needs_diamond_tool", "minecraft:needs_iron_tool", "minecraft:needs_stone_tool" } },
            { "minecraft:incorrect_for_stone_tool", new[] { "minecraft:needs_diamond_tool", "minecraft:needs_iron_tool" } },
            { "minecraft:incorrect_for_iron_tool", new[] { "minecraft:needs_diamond_tool" } },
            { "minecraft:incorrect_for_diamond_tool", new string[0] },
            { "minecraft:incorrect_for_netherite_tool", new string[0] },
        };

        /// <summary>Every member of a tag of a registry ("item" by default; nested tags resolved; unknown vanilla
        /// tags yield nothing but the base item tags and the base block tag references known to the import).</summary>
        public List<string> TagMembers(string tag, HashSet<string> seen = null, string registry = "item")
        {
            seen = seen ?? new HashSet<string>(StringComparer.Ordinal);
            var res = new List<string>();
            if (!seen.Add(tag)) return res;
            string[] baseMembers;
            if (registry == "item" && BaseItemTags.TryGetValue(tag, out baseMembers))
                foreach (var m in baseMembers) { if (m.StartsWith("#", StringComparison.Ordinal)) res.AddRange(TagMembers(m.Substring(1), seen, registry)); else res.Add(m); }
            if (registry == "block" && BaseBlockTagRefs.TryGetValue(tag, out baseMembers))
                foreach (var r in baseMembers) res.AddRange(TagMembers(r, seen, registry));
            var j = TagFile(registry, tag);
            if (j != null && j["values"] != null)
            {
                foreach (var v in j["values"].A)
                {
                    string s = v.IsStr ? v.AsString() : v["id"] != null ? v["id"].AsString() : null;
                    if (s == null) continue;
                    if (s.StartsWith("#", StringComparison.Ordinal)) res.AddRange(TagMembers(s.Substring(1), seen, registry));
                    else res.Add(s);
                }
            }
            return res.Distinct().ToList();
        }

        /// <summary>The TF items each item tag (of every namespace) contains: item id -> tags.</summary>
        public void ComputeItemTags()
        {
            foreach (var res in new[] { GenRes, MainRes })
            {
                string dataDir = Path.Combine(res, "data");
                if (!Directory.Exists(dataDir)) continue;
                foreach (var nsDir in Directory.GetDirectories(dataDir).OrderBy(x => x, StringComparer.Ordinal))
                {
                    string ns = Path.GetFileName(nsDir);
                    foreach (var rel in Files(Path.Combine(nsDir, "tags", "item"), "*.json"))
                    {
                        string tag = ns + ":" + StripExt(rel);
                        foreach (var m in TagMembers(tag))
                        {
                            if (!m.StartsWith("twilightforest:", StringComparison.Ordinal)) continue;
                            string id = m.Substring("twilightforest:".Length);
                            SortedSet<string> s;
                            if (!ItemTags.TryGetValue(id, out s)) ItemTags[id] = s = new SortedSet<string>(StringComparer.Ordinal);
                            s.Add(tag);
                        }
                    }
                }
            }
        }
    }
}
