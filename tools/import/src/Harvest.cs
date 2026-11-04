// GloamingForest import tool (contract "import", unit F1): which tools mine a TF block (TF's
// #minecraft:mineable/* tags, #minecraft:sword_efficient and the shears' #minecraft:leaves / #minecraft:wool,
// with the vanilla tags they include), which tool tier drops it (#minecraft:needs_<tier>_tool) and whether it
// drops only for a correct tool (requiresCorrectToolForDrops), as Bedrock block tags; and the minecraft:digger
// component and tool tags of TF's tiered tools (DiggerItem / SwordItem rules with the tier's speed). Facts only;
// no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class Harvest
    {
        public readonly SortedSet<string> Tools = new SortedSet<string>(StringComparer.Ordinal);   // pickaxe axe shovel hoe sword shears leaves
        public string Tier;            // stone | iron | diamond (#minecraft:needs_<tier>_tool), or null
        public bool RequiresTool;      // requiresCorrectToolForDrops()
        public bool Digger { get { return Tools.Overlaps(DiggerKinds); } }
        public static readonly string[] DiggerKinds = { "pickaxe", "axe", "shovel", "hoe" };
    }

    public sealed partial class Ctx
    {
        // Java's tool tags (Minecraft 1.21.1: DiggerItem mines #minecraft:mineable/<kind> at the tier's speed, SwordItem
        // #minecraft:sword_efficient at 1.5, ShearsItem #minecraft:leaves at 15 and #minecraft:wool at 5). A block of
        // #minecraft:leaves gets the kind "leaves" (Bedrock's tag minecraft:leaves, as minecraft:oak_leaves has: TF's swords
        // read it) and "shears": Bedrock's vanilla shears and swords single out their own leaves by block type, not by a tag
        // a JSON block can carry (measured on 1.26.51: on a TF leaf block with minecraft:leaves the vanilla shears mine at 1,
        // with minecraft:is_shears_item_destructible at 5, on oak leaves at 15; a vanilla sword at 1, on oak leaves 1.5)
        static readonly string[][] ToolTags =
        {
            new[] { "minecraft:mineable/pickaxe", "pickaxe" }, new[] { "minecraft:mineable/axe", "axe" }, new[] { "minecraft:mineable/shovel", "shovel" },
            new[] { "minecraft:mineable/hoe", "hoe" }, new[] { "minecraft:sword_efficient", "sword" }, new[] { "minecraft:leaves", "leaves" },
            new[] { "minecraft:leaves", "shears" }, new[] { "minecraft:wool", "shears" },
        };

        /// <summary>The Bedrock block tag the vanilla tools of a kind read (VanillaBlockTags, 1.26.51).</summary>
        public static readonly Dictionary<string, string> BedrockToolTag = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "pickaxe", "minecraft:is_pickaxe_item_destructible" }, { "axe", "minecraft:is_axe_item_destructible" },
            { "shovel", "minecraft:is_shovel_item_destructible" }, { "hoe", "minecraft:is_hoe_item_destructible" },
            { "sword", "minecraft:is_sword_item_destructible" }, { "shears", "minecraft:is_shears_item_destructible" },
            { "leaves", "minecraft:leaves" },
        };

        // the block tags of the tiers above a tool's (a digger's condition leaves them out: a custom item harvests every block
        // its digger lists, its tier tags notwithstanding, measured on 1.26.51). Vanilla blocks carry the same tags (iron ore
        // minecraft:stone_tier_destructible, diamond block minecraft:iron_tier_destructible, obsidian
        // minecraft:diamond_tier_destructible); their legacy <tier>_pick_diggable tags list the pickaxes that can dig them
        // (iron ore: iron_pick_diggable and diamond_pick_diggable), which is no tier requirement, so they are not read
        static readonly string[][] TierBlockTags =
        {
            new[] { "stone", "minecraft:stone_tier_destructible" },
            new[] { "iron", "minecraft:iron_tier_destructible" },
            new[] { "diamond", "minecraft:diamond_tier_destructible" },
            new[] { "netherite", "minecraft:netherite_tier_destructible" },
        };
        // the tier a tool drops (Tier.getIncorrectBlocksForDrops: wooden and gold drop the same blocks) -> its rank among TierBlockTags
        static readonly Dictionary<string, int> DropRank = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "wooden", 0 }, { "gold", 0 }, { "stone", 1 }, { "iron", 2 }, { "diamond", 3 }, { "netherite", 4 },
        };

        // #minecraft:needs_<tier>_tool, the highest first (a block in several needs the highest)
        static readonly string[][] NeedsTags =
        {
            new[] { "minecraft:needs_diamond_tool", "diamond" }, new[] { "minecraft:needs_iron_tool", "iron" }, new[] { "minecraft:needs_stone_tool", "stone" },
        };

        /// <summary>The Bedrock block tag of a needed tier (VanillaBlockTags::IsDestructibleBy<Tier>Tier, 1.26.51).</summary>
        public static readonly Dictionary<string, string> BedrockTierTag = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "stone", "minecraft:stone_tier_destructible" }, { "iron", "minecraft:iron_tier_destructible" }, { "diamond", "minecraft:diamond_tier_destructible" },
        };

        /// <summary>The Bedrock item tag of a Java tier that drops a vanilla tier's blocks (Tier.getIncorrectBlocksForDrops).</summary>
        public static readonly Dictionary<string, string> BedrockItemTierTag = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "wooden", "minecraft:wooden_tier" }, { "stone", "minecraft:stone_tier" }, { "iron", "minecraft:iron_tier" },
            { "diamond", "minecraft:diamond_tier" }, { "gold", "minecraft:golden_tier" }, { "netherite", "minecraft:netherite_tier" },
        };

        /// <summary>Block id (no namespace) -> its tools, tier and drop rule (every TF block has an entry).</summary>
        public readonly SortedDictionary<string, Harvest> Harvests = new SortedDictionary<string, Harvest>(StringComparer.Ordinal);

        /// <summary>Tools whose TF class changes the mining speed by block (runtime behaviour of their owners).</summary>
        public readonly SortedSet<string> RuntimeDigger = new SortedSet<string>(StringComparer.Ordinal);

        public void ComputeHarvest()
        {
            foreach (var id in BlockInfos.Keys) Harvests[id] = new Harvest { RequiresTool = BlockInfos[id].J.RequiresTool };
            Func<string, IEnumerable<string>> tfMembers = tag => TagMembers(tag, null, "block")
                .Where(m => m.StartsWith("twilightforest:", StringComparison.Ordinal)).Select(m => m.Substring("twilightforest:".Length));
            foreach (var t in ToolTags)
                foreach (var id in tfMembers(t[0]))
                {
                    Harvest h;
                    if (Harvests.TryGetValue(id, out h)) h.Tools.Add(t[1]);
                    else Warn("tag #" + t[0] + " names twilightforest:" + id + ", which is not a TF block");
                }
            foreach (var t in NeedsTags)
                foreach (var id in tfMembers(t[0]))
                {
                    Harvest h;
                    if (Harvests.TryGetValue(id, out h) && h.Tier == null) h.Tier = t[1];
                }
            // what TF itself never drops (a correct tool is required, and no tool is correct for it), and needs-tier tags
            // without a tool (a tier only restricts the tools that mine the block)
            var neverDrops = Harvests.Where(kv => kv.Value.RequiresTool && !kv.Value.Digger).Select(kv => kv.Key).ToList();
            if (neverDrops.Count > 0) Log.Add(neverDrops.Count + " block(s) require a correct tool but are in no #minecraft:mineable tag (TF never drops them for a player): " + string.Join(", ", neverDrops));
            var tierOnly = Harvests.Where(kv => kv.Value.Tier != null && !kv.Value.Digger).Select(kv => kv.Key).ToList();
            if (tierOnly.Count > 0) Log.Add(tierOnly.Count + " block(s) in a #minecraft:needs_<tier>_tool tag but in no #minecraft:mineable tag (the tier has no effect, no Bedrock tier tag): " + string.Join(", ", tierOnly));
        }

        /// <summary>The Bedrock block tags (namespaced ids, "minecraft:is_axe_item_destructible") of the block `id` (TF
        /// path, no namespace): its tools and, for a block a tool mines, its tier, in the order the block file lists them.
        /// Empty for a block without tags.</summary>
        public List<string> BedrockBlockTags(string id)
        {
            Harvest h;
            if (!Harvests.TryGetValue(id, out h)) return new List<string>();
            var tags = h.Tools.Select(t => BedrockToolTag[t]).ToList();
            if (h.Tier != null && h.Digger) tags.Add(BedrockTierTag[h.Tier]);
            return tags;
        }

        /// <summary>The Bedrock block tags of a block (BedrockBlockTags) in the form its format takes (docs/spikes/S7.md):
        /// from 1.26.20 (modern) the minecraft:tags component, an array of tag ids; before, one "tag:&lt;id&gt;"
        /// component per tag. The families data carries the same list format-free (params.tags, Skeleton.ImportFamilies).</summary>
        void HarvestTags(BlockInfo bi, J comps, bool modern)
        {
            var tags = BedrockBlockTags(bi.Id);
            if (tags.Count == 0) return;
            if (modern) comps.Set("minecraft:tags", J.Strs(tags));
            else foreach (var t in tags) comps.Set("tag:" + t, J.Obj());
        }

        /// <summary>The Bedrock block tags of block components, in either form: the minecraft:tags array (formats
        /// 1.26.20 and later) or "tag:&lt;id&gt;" components (earlier formats).</summary>
        public static List<string> BlockTagsIn(J comps)
        {
            var tags = new List<string>();
            if (comps == null || !comps.IsObj) return tags;
            var t = comps["minecraft:tags"];
            if (t != null && t.IsArr) tags.AddRange(t.A.Where(x => x.IsStr).Select(x => x.S));
            tags.AddRange(comps.Keys.Where(k => k.StartsWith("tag:", StringComparison.Ordinal)).Select(k => k.Substring(4)));
            return tags;
        }

        /// <summary>The tools TF's tiered tools are (Bedrock item tags: kind, tool, tier) and their minecraft:digger: a
        /// DiggerItem mines its #minecraft:mineable/<kind> blocks (the block tag above) at its tier's speed, a SwordItem
        /// cobwebs at 15 and #minecraft:sword_efficient at 1.5 (Minecraft 1.21.1's Tool rules; Bedrock's efficiency
        /// enchantment applies, as Java's mining_efficiency). Null for other items.</summary>
        J Digger(JItem ji, List<string> itemTags)
        {
            if (ji == null || ji.ToolKind == null) return null;
            // a TF class that changes the speed by block (GiantPickItem x64 on giant blocks, MazebreakerPickItem x16 on
            // #twilightforest:mazebreaker_accelerated): the owner's runtime behaviour on top of this digger, listed in the log
            foreach (var cls in ji.Chain)
            {
                string ct = Java.ClassText(this, cls);
                if (ct != null && Regex.IsMatch(ct, @"\bfloat\s+getDestroySpeed\s*\(")) RuntimeDigger.Add(ji.Id + ": " + cls + ".getDestroySpeed");
            }
            var speeds = J.Arr();
            if (ji.ToolKind == "sword")
            {
                // cobwebs, then #minecraft:sword_efficient: TF's blocks in it, and the vanilla leaves (minecraft:leaves)
                speeds.Add(J.Obj().Set("block", "minecraft:web").Set("speed", 15));
                speeds.Add(J.Obj().Set("block", J.Obj().Set("tags", "q.any_tag('" + BedrockToolTag["sword"] + "', '" + BedrockToolTag["leaves"] + "')")).Set("speed", DiggerSpeed(ji, 1.5)));
            }
            else if (ji.Tier != null && ji.Tier.Drops != null && DropRank.ContainsKey(ji.Tier.Drops))
            {
                // the kind's blocks, except those of a higher tier: the game lets a custom item harvest every block its
                // digger lists, so Java's incorrect_for_<tier>_tool blocks are left out (they then mine at speed 1, where
                // Java keeps the tier's speed without the drop: the closer of the two behaviours Bedrock can express)
                var above = TierBlockTags.Skip(DropRank[ji.Tier.Drops]).SelectMany(t => t.Skip(1)).ToList();
                string cond = "q.any_tag('" + BedrockToolTag[ji.ToolKind] + "')" + (above.Count > 0 ? " && !q.any_tag(" + string.Join(", ", above.Select(a => "'" + a + "'")) + ")" : "");
                speeds.Add(J.Obj().Set("block", J.Obj().Set("tags", cond)).Set("speed", DiggerSpeed(ji, ji.Tier.Speed)));
            }
            else return null;
            // the tool tags only with the digger (a tool tagged is_<kind> without one would mine at hand speed)
            itemTags.Add("minecraft:is_" + ji.ToolKind);
            if (ji.ToolKind != "sword") itemTags.Add("minecraft:is_tool");
            if (ji.Tier != null && ji.Tier.Drops != null) itemTags.Add(BedrockItemTierTag[ji.Tier.Drops]);
            return J.Obj().Set("use_efficiency", true).Set("destroy_speeds", speeds);
        }

        /// <summary>Tools whose Java speed is not an integer, with the speed written (Bedrock's digger takes integers).</summary>
        public readonly SortedSet<string> RoundedDigger = new SortedSet<string>(StringComparer.Ordinal);

        /// <summary>A digger speed: Bedrock 1.26.51 rejects a fractional minecraft:digger speed ("invalid numeric value"
        /// for 6.5 and 1.5), so Java's speed is rounded half up (the mining time closest to Java's: ironwood 6.5 -> 7,
        /// a sword's 1.5 -> 2) and the tool listed in the import log for its owner.</summary>
        long DiggerSpeed(JItem ji, double speed)
        {
            long r = (long)Math.Round(speed, MidpointRounding.AwayFromZero);
            if (r != speed) RoundedDigger.Add(ji.Id + " " + speed.ToString(System.Globalization.CultureInfo.InvariantCulture) + " -> " + r);
            return r;
        }

        /// <summary>data/gf/import/harvest.json: the blocks that drop only for a correct tool (the runtime sets the engine's
        /// requires-correct-tool flag on them, which Bedrock's JSON blocks cannot declare) and every block's tools and tier,
        /// for the selftests. Every block's tools and tier come from the Bedrock block tags of its final skeleton file
        /// (after the overrides; a family block's follow its params.tags, SyncFamilyTags), so harvest.json lists what the
        /// game reads, whatever override changed them (FinalHarvest).</summary>
        public void ImportHarvestIndex()
        {
            var blocks = J.Obj();
            foreach (var kv in Harvests)
            {
                var h = FinalHarvest(kv.Key, kv.Value);
                if (h.Tools.Count == 0 && h.Tier == null && !h.RequiresTool) continue;
                var e = J.Obj();
                if (h.Tools.Count > 0) e.Set("tools", J.Strs(h.Tools));
                if (h.Tier != null && h.Digger) e.Set("tier", h.Tier);
                if (h.RequiresTool) e.Set("requiresTool", true);
                blocks.Set("twilightforest:" + kv.Key, e);
            }
            EmitJson("skeleton", ImportUnit, "data/gf/import/harvest.json", J.Obj().Set("blocks", blocks));
        }

        /// <summary>The harvest tags the import knows: the tool tags (BedrockToolTag) and the tier tags (BedrockTierTag).</summary>
        static readonly HashSet<string> KnownHarvestTags = new HashSet<string>(BedrockToolTag.Values.Concat(BedrockTierTag.Values), StringComparer.Ordinal);

        /// <summary>A tag of a harvest tag's form (..._item_destructible, ..._tier_destructible) that the import does not
        /// know: a misspelling ("minecraft:is_pickax_item_destructible") the game ignores, so the block would lose the
        /// tool or tier without a word.</summary>
        public static bool UnknownHarvestTag(string tag)
        {
            return !KnownHarvestTags.Contains(tag) && Regex.IsMatch(tag, @"_(item|tier)_destructible$");
        }

        /// <summary>The known harvest tags, for messages.</summary>
        public static string KnownHarvestTagList { get { return string.Join(", ", KnownHarvestTags.OrderBy(x => x, StringComparer.Ordinal)); } }

        /// <summary>Reports, in one error, the tags `where` lists more than once (block tags are a set: every check then
        /// compares them as one). Returns whether there were any.</summary>
        public bool ReportDuplicateTags(string where, IEnumerable<string> tags)
        {
            var dups = tags.GroupBy(t => t, StringComparer.Ordinal).Where(g => g.Count() > 1).Select(g => g.Key).OrderBy(x => x, StringComparer.Ordinal).ToList();
            if (dups.Count == 0) return false;
            Errors.Add(where + " lists the tag(s) " + string.Join(", ", dups) + " more than once (block tags are a set)");
            return true;
        }

        /// <summary>The harvest of a block as the Bedrock block tags of its final skeleton file declare them (after the
        /// overrides; a family block's follow its params.tags): the tools of its tool tags (BedrockToolTag), the highest
        /// tier of its tier tags (BedrockTierTag), and TF's drop rule (requiresCorrectToolForDrops, which no tag carries).
        /// Other tags are not harvest facts and are left out. A tag of a harvest tag's form the import does not know is an
        /// error, as a duplicated tag of a non-family block (a family block's are reported by SyncFamilyTags). An override
        /// that leaves a block TF drops only for a correct tool without a pickaxe, axe, shovel or hoe tag (it then never
        /// drops) is a warning, as a tier tag without one. Java's harvest when the skeleton file is missing or has no
        /// components (reported by CheckEmittedBlocks / SyncFamilyTags).</summary>
        Harvest FinalHarvest(string id, Harvest java)
        {
            BlockInfo bi;
            SortedDictionary<string, GenFile> files;
            GenFile f;
            if (!BlockInfos.TryGetValue(id, out bi) || !Out.TryGetValue(NameOf(bi.Owner), out files) || !files.TryGetValue("bp/blocks/" + id + ".json", out f)) return java;
            var root = J.Parse(System.Text.Encoding.UTF8.GetString(f.Bytes));
            var block = root.IsObj ? root["minecraft:block"] : null;
            var comps = block != null && block.IsObj ? block["components"] : null;
            if (comps == null || !comps.IsObj) return java;
            var list = BlockTagsIn(comps);
            string rel = "bp/blocks/" + id + ".json", source = bi.Family != null
                ? "params.tags of its data/gf/families entry"
                : "its skeleton, as tools/import/overrides/" + NameOf(bi.Owner) + ".json patches it";
            if (bi.Family == null) ReportDuplicateTags(rel, list);
            foreach (var t in list.Where(UnknownHarvestTag).Distinct(StringComparer.Ordinal))
                Errors.Add(rel + ": unknown harvest tag " + t + " (from " + source + "; the import knows " + KnownHarvestTagList + ")");
            var tags = new HashSet<string>(list, StringComparer.Ordinal);
            var h = new Harvest { RequiresTool = java.RequiresTool };
            foreach (var kv in BedrockToolTag)
                if (tags.Contains(kv.Value)) h.Tools.Add(kv.Key);
            // the highest tier tag (as a block in several #minecraft:needs_<tier>_tool tags needs the highest)
            foreach (var n in NeedsTags)
                if (h.Tier == null && tags.Contains(BedrockTierTag[n[1]])) h.Tier = n[1];
            if (h.Tier != null && !h.Digger)
                Warn(rel + ": the tier tag " + BedrockTierTag[h.Tier] + " (from " + source
                    + ") without a pickaxe, axe, shovel or hoe tag has no effect (not listed in data/gf/import/harvest.json)");
            // TF's own never-dropping blocks are listed by ComputeHarvest; this is one an override made so
            if (h.RequiresTool && !h.Digger && java.Digger)
                Warn(rel + ": TF drops twilightforest:" + id + " only for a correct tool, and its final tags (from " + source
                    + ") have no pickaxe, axe, shovel or hoe tag: no tool is correct, so the block never drops (harvest.json lists requiresTool without a digger)");
            return h;
        }
    }
}
