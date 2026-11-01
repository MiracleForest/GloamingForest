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
        };

        /// <summary>Every item in an item tag (nested tags resolved; unknown vanilla tags yield nothing).</summary>
        public List<string> TagMembers(string tag, HashSet<string> seen = null)
        {
            seen = seen ?? new HashSet<string>(StringComparer.Ordinal);
            var res = new List<string>();
            if (!seen.Add(tag)) return res;
            string[] baseMembers;
            if (BaseItemTags.TryGetValue(tag, out baseMembers)) res.AddRange(baseMembers);
            var j = TagFile("item", tag);
            if (j != null && j["values"] != null)
            {
                foreach (var v in j["values"].A)
                {
                    string s = v.IsStr ? v.AsString() : v["id"] != null ? v["id"].AsString() : null;
                    if (s == null) continue;
                    if (s.StartsWith("#", StringComparison.Ordinal)) res.AddRange(TagMembers(s.Substring(1), seen));
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
