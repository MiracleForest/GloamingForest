// GloamingForest import tool (contract "import", unit F1): upstream's vanilla-type recipes (crafting shaped /
// shapeless, smelting, blasting, smoking, campfire, stonecutting, smithing) as Bedrock behaviour-pack
// recipes, unlocked by the items of upstream's recipe advancements. Custom upstream types (drying, uncrafting, ...)
// stay Java data for the loaders their owners register (contract "recipes").
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace GfImport
{
    public sealed partial class Ctx
    {
        // Java tags with a Bedrock item tag of the same meaning
        static readonly Dictionary<string, string> BedrockItemTags = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "minecraft:planks", "minecraft:planks" },
            { "minecraft:logs", "minecraft:logs" },
            { "minecraft:wool", "minecraft:wool" },
            { "minecraft:wooden_slabs", "minecraft:wooden_slabs" },
        };

        // Java item ids that name another item on Bedrock (src/feature/tags/JavaIds.cpp has the full table); only the
        // ones Bedrock's recipes do not resolve as aliases: Java's minecraft:stone_slab (the plain stone slab) is
        // Bedrock's minecraft:normal_stone_slab (F9's item.sweep_recipes found nagastone_spiral uncraftable)
        static readonly Dictionary<string, string> BedrockRecipeItems = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "minecraft:stone_slab", "minecraft:normal_stone_slab" },
        };

        static string BedrockItem(string id)
        {
            string b;
            return id != null && BedrockRecipeItems.TryGetValue(id, out b) ? b : id;
        }

        const int MaxRecipeExpansion = 64;

        /// <summary>The alternatives of a Java ingredient as Bedrock recipe items ({"item": ...} or {"tag": ...}).</summary>
        List<J> Alternatives(J ing)
        {
            var res = new List<J>();
            if (ing == null) return res;
            if (ing.IsArr) { foreach (var x in ing.A) res.AddRange(Alternatives(x)); return res; }
            if (ing["item"] != null) { res.Add(J.Obj().Set("item", BedrockItem(ing["item"].AsString()))); return res; }
            string itype = ing["type"] != null ? ing["type"].AsString() : null;
            if (itype == "neoforge:difference" || itype == "neoforge:intersection" || itype == "neoforge:compound")
            {
                // NeoForge's set ingredients over item lists: base minus subtracted, every child, any child
                List<string> set;
                if (itype == "neoforge:difference") set = IngredientItems(ing["base"]).Except(IngredientItems(ing["subtracted"])).ToList();
                else
                {
                    var kids = ing["children"] != null && ing["children"].IsArr ? ing["children"].A.Select(IngredientItems).ToList() : new List<List<string>>();
                    set = kids.Count == 0 ? new List<string>()
                        : itype == "neoforge:compound" ? kids.SelectMany(k => k).Distinct().ToList() : kids.Aggregate((a, b) => a.Intersect(b).ToList());
                }
                foreach (var s in set) res.Add(J.Obj().Set("item", BedrockItem(s)));
                return res;
            }
            if (itype == "neoforge:components")
            {
                // items with data components: Bedrock has the water bottle as potion data 0 (the only one upstream uses)
                var potion = ing["components"] != null && ing["components"]["minecraft:potion_contents"] != null ? ing["components"]["minecraft:potion_contents"]["potion"] : null;
                string it = ing["items"] != null && ing["items"].IsStr ? ing["items"].AsString() : null;
                if (it == "minecraft:potion" && potion != null && potion.AsString() == "minecraft:water" && ing["components"].Count == 1)
                {
                    res.Add(J.Obj().Set("item", "minecraft:potion").Set("data", 0));
                    return res;
                }
            }
            if (ing["tag"] == null) { Warn("recipe ingredient " + ing.ToCompact() + " has no Bedrock equivalent (custom ingredient type)"); return res; }
            if (ing["tag"] != null)
            {
                string tag = ing["tag"].AsString();
                string bt;
                if (BedrockItemTags.TryGetValue(tag, out bt)) { res.Add(J.Obj().Set("tag", bt)); return res; }
                var members = TagMembers(tag);
                // tags of upstream items only (not blocks) are declared on those items (minecraft:tags, see ItemDoc)
                if (members.Count > 1 && members.All(IsTaggableItem)) { res.Add(J.Obj().Set("tag", tag)); return res; }
                // other tags do not exist on Bedrock: one recipe per member (see BedrockRecipes)
                foreach (var m in members) res.Add(J.Obj().Set("item", BedrockItem(m)));
                if (members.Count == 0) Warn("recipe ingredient tag " + tag + " has no known members");
            }
            return res;
        }

        /// <summary>The items a Java ingredient matches (tags expanded to their members, never kept as Bedrock tags),
        /// for NeoForge's set ingredients; a tag whose members the import does not know is a warning.</summary>
        List<string> IngredientItems(J ing)
        {
            var res = new List<string>();
            if (ing == null) return res;
            if (ing.IsArr) { foreach (var x in ing.A) res.AddRange(IngredientItems(x)); return res.Distinct().ToList(); }
            if (ing["item"] != null) { res.Add(ing["item"].AsString()); return res; }
            if (ing["tag"] != null)
            {
                var members = TagMembers(ing["tag"].AsString());
                if (members.Count == 0) Warn("recipe ingredient tag " + ing["tag"].AsString() + " has no known members");
                return members;
            }
            // nested set ingredients
            return Alternatives(ing).Where(a => a["item"] != null).Select(a => a["item"].AsString()).Distinct().ToList();
        }

        /// <summary>An upstream item with its own item JSON (so it can carry minecraft:tags).</summary>
        bool IsTaggableItem(string full)
        {
            return full.StartsWith("gloamingforest:", StringComparison.Ordinal) && OwnerOf("item", full) != null && !BlockInfos.ContainsKey(full.Substring(15));
        }

        static J Result(J r)
        {
            if (r == null) return null;
            if (r.IsStr) return J.Obj().Set("item", BedrockItem(r.AsString()));
            var o = J.Obj().Set("item", BedrockItem((r["id"] ?? r["item"]).AsString()));
            if (r["count"] != null && r["count"].AsLong(1) != 1) o.Set("count", r["count"].AsLong(1));
            return o;
        }

        /// <summary>The unlock list from the recipe's advancement: the items of its inventory_changed criteria, an item tag
        /// ("#c:ingots/ironwood") as its members (Bedrock's unlock lists items); without item criteria, an enter_block
        /// water criterion (upstream's boats) is Bedrock's PlayerInWater context (as Bedrock's own boat recipes), else
        /// AlwaysUnlocked.</summary>
        J Unlock(string ns, string path, J recipe)
        {
            var items = new SortedSet<string>(StringComparer.Ordinal);
            bool inWater = false;
            Action<string> add = s =>
            {
                if (!s.StartsWith("#", StringComparison.Ordinal)) { items.Add(s); return; }
                // members a Bedrock world has: vanilla items and upstream's own (optional compat entries of other mods are left out)
                var members = TagMembers(s.Substring(1)).Where(m => m.StartsWith("minecraft:", StringComparison.Ordinal)
                    || (m.StartsWith("gloamingforest:", StringComparison.Ordinal) && (OwnerOf("item", m) != null || OwnerOf("block", m) != null))).ToList();
                if (members.Count == 0) Warn("recipe " + ns + ":" + path + ": unlock tag " + s + " has no known members");
                foreach (var m in members) items.Add(m);
            };
            string advDir = Path.Combine(GenRes, "data", ns, "advancement", "recipes");
            if (Directory.Exists(advDir))
            {
                foreach (var cat in Directory.GetDirectories(advDir).OrderBy(x => x, StringComparer.Ordinal))
                {
                    string f = Path.Combine(cat, path.Replace('/', Path.DirectorySeparatorChar) + ".json");
                    if (!File.Exists(f)) continue;
                    var adv = ReadJson(f);
                    var crit = adv["criteria"];
                    if (crit == null) break;
                    foreach (var ck in crit.Keys)
                    {
                        var c = crit[ck];
                        if (c["trigger"] != null && c["trigger"].AsString() == "minecraft:enter_block" && c["conditions"] != null
                            && c["conditions"]["block"] != null && c["conditions"]["block"].AsString() == "minecraft:water") inWater = true;
                        if (c["trigger"] == null || c["trigger"].AsString() != "minecraft:inventory_changed") continue;
                        var cond = c["conditions"];
                        if (cond == null || cond["items"] == null) continue;
                        foreach (var pred in cond["items"].A)
                        {
                            var it = pred["items"];
                            if (it == null) continue;
                            if (it.IsStr) add(it.AsString());
                            else if (it.IsArr) foreach (var x in it.A) if (x.IsStr) add(x.AsString());
                        }
                    }
                    break;
                }
            }
            // an item list, or (no inventory_changed item criteria) the context object
            if (items.Count == 0) return J.Obj().Set("context", inWater ? "PlayerInWater" : "AlwaysUnlocked");
            var arr = J.Arr();
            foreach (var i in items) arr.Add(J.Obj().Set("item", i));
            return arr;
        }

        /// <summary>The vanilla-type recipes the import could not express on Bedrock (listed in the import log).</summary>
        public readonly SortedSet<string> DroppedRecipes = new SortedSet<string>(StringComparer.Ordinal);

        void Dropped(string id, string type, string why)
        {
            DroppedRecipes.Add(id);
            Warn("recipe " + id + " (" + type + ") has no Bedrock recipe: " + why);
        }

        /// <summary>Every combination of the alternatives (one recipe each), or null with an error when there are more
        /// than MaxRecipeExpansion: a recipe is never cut down to some of its alternatives.</summary>
        List<List<J>> Expand(string id, List<List<J>> alts)
        {
            var combos = Cartesian(alts).Take(MaxRecipeExpansion + 1).ToList();
            if (combos.Count <= MaxRecipeExpansion) return combos;
            Errors.Add("recipe " + id + ": more than " + MaxRecipeExpansion + " ingredient combinations (" + string.Join(" x ", alts.Select(a => a.Count))
                + "); declare the tag on its members (an upstream item tag) or raise Recipes.MaxRecipeExpansion");
            return null;
        }

        /// <summary>The alternatives as single items: a Bedrock tag alternative becomes its members (furnace and smithing
        /// recipes take one item per slot).</summary>
        List<J> ItemsOnly(List<J> alts)
        {
            var res = new List<J>();
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var a in alts)
            {
                if (a["item"] != null)
                {
                    if (a["data"] != null || seen.Add(a["item"].AsString())) res.Add(a);
                    continue;
                }
                string tag = a["tag"].AsString();
                var members = TagMembers(tag);
                if (members.Count == 0) Warn("recipe ingredient tag " + tag + " has no known members");
                foreach (var m in members) if (seen.Add(m)) res.Add(J.Obj().Set("item", m));
            }
            return res;
        }

        static IEnumerable<List<T>> Cartesian<T>(List<List<T>> lists)
        {
            IEnumerable<List<T>> acc = new[] { new List<T>() };
            foreach (var l in lists)
            {
                var cur = l;
                acc = acc.SelectMany(prefix => cur.Select(x => { var n = new List<T>(prefix); n.Add(x); return n; }));
            }
            return acc;
        }

        public void ImportRecipes()
        {
            foreach (var res in new[] { GenRes, MainRes })
                foreach (var ns in new[] { "gloamingforest", "minecraft" })
                {
                    string dir = Path.Combine(res, "data", ns, "recipe");
                    foreach (var rel in Files(dir, "*.json"))
                    {
                        string path = StripExt(rel);
                        string id = ns + ":" + path;
                        string owner = OwnerOf("recipe", id) ?? ImportUnit;
                        var r = ReadJson(Path.Combine(dir, rel));
                        string type = r["type"] != null ? r["type"].AsString() : "";
                        var docs = BedrockRecipes(ns, path, r, type);
                        for (int i = 0; i < docs.Count; i++)
                        {
                            string name = ns + "/" + path + (docs.Count > 1 ? "_" + i : "");
                            EmitJson("recipes", owner, "bp/recipes/" + name + ".json", docs[i]);
                        }
                    }
                }
            Log.Add(DroppedRecipes.Count == 0 ? "every vanilla-type recipe has a Bedrock recipe"
                : DroppedRecipes.Count + " vanilla-type recipe(s) without a Bedrock recipe: " + string.Join(", ", DroppedRecipes));
        }

        List<J> BedrockRecipes(string ns, string path, J r, string type)
        {
            var outList = new List<J>();
            string id = ns + ":" + path;
            switch (type)
            {
                case "minecraft:crafting_shaped":
                    {
                        var keys = r["key"].Keys.ToList();
                        var alts = keys.Select(k => Alternatives(r["key"][k])).ToList();
                        // Bedrock measures pattern rows in bytes: map non-ASCII keys (upstream uses "∴", "•") to free ASCII letters
                        var pattern = r["pattern"].A.Select(p => p.AsString()).ToList();
                        var free = new Queue<char>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789".Where(c => !keys.Contains(c.ToString())));
                        for (int i = 0; i < keys.Count; i++)
                        {
                            if (keys[i].All(c => c < 128)) continue;
                            string to = free.Dequeue().ToString();
                            pattern = pattern.Select(p => p.Replace(keys[i], to)).ToList();
                            keys[i] = to;
                        }
                        if (alts.Any(a => a.Count == 0)) { Dropped(id, type, "ingredient(s) " + string.Join(", ", keys.Where((k, i) => alts[i].Count == 0)) + " have no Bedrock item"); return outList; }
                        var combos = Expand(id, alts);
                        if (combos == null) return outList;
                        int n = 0;
                        foreach (var combo in combos)
                        {
                            var key = J.Obj();
                            for (int i = 0; i < keys.Count; i++) key.Set(keys[i], combo[i]);
                            string rid = id + (combos.Count > 1 ? "_" + n : "");
                            var body = J.Obj().Set("description", J.Obj().Set("identifier", rid)).Set("tags", J.Arr().Add("crafting_table"))
                                .Set("pattern", J.Strs(pattern)).Set("key", key).Set("unlock", Unlock(ns, path, r)).Set("result", Result(r["result"]));
                            outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_shaped", body));
                            n++;
                        }
                        return outList;
                    }
                case "minecraft:crafting_shapeless":
                    {
                        var alts = r["ingredients"].A.Select(Alternatives).ToList();
                        if (alts.Any(a => a.Count == 0)) { Dropped(id, type, "ingredient(s) " + string.Join(", ", r["ingredients"].A.Where((x, i) => alts[i].Count == 0).Select(x => x.ToCompact())) + " have no Bedrock item"); return outList; }
                        var combos = Expand(id, alts);
                        if (combos == null) return outList;
                        int n = 0;
                        foreach (var combo in combos)
                        {
                            string rid = id + (combos.Count > 1 ? "_" + n : "");
                            var body = J.Obj().Set("description", J.Obj().Set("identifier", rid)).Set("tags", J.Arr().Add("crafting_table"))
                                .Set("ingredients", J.Arr(combo)).Set("unlock", Unlock(ns, path, r)).Set("result", Result(r["result"]));
                            outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_shapeless", body));
                            n++;
                        }
                        return outList;
                    }
                case "minecraft:stonecutting":
                    {
                        var alts = Alternatives(r["ingredient"]);
                        if (alts.Count == 0) Dropped(id, type, "the ingredient has no Bedrock item");
                        int n = 0;
                        foreach (var a in alts)
                        {
                            string rid = id + (alts.Count > 1 ? "_" + n : "");
                            var body = J.Obj().Set("description", J.Obj().Set("identifier", rid)).Set("tags", J.Arr().Add("stonecutter"))
                                .Set("ingredients", J.Arr(new[] { a })).Set("unlock", Unlock(ns, path, r)).Set("result", Result(r["result"]));
                            outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_shapeless", body));
                            n++;
                        }
                        return outList;
                    }
                case "minecraft:smelting": case "minecraft:blasting": case "minecraft:smoking": case "minecraft:campfire_cooking":
                    {
                        var tags = J.Arr();
                        if (type == "minecraft:smelting") tags.Add("furnace");
                        else if (type == "minecraft:blasting") tags.Add("blast_furnace");
                        else if (type == "minecraft:smoking") tags.Add("smoker");
                        else { tags.Add("campfire"); tags.Add("soul_campfire"); }
                        // Bedrock furnace recipes take one item: tags (Bedrock-native ones included) become one recipe per member
                        var alts = ItemsOnly(Alternatives(r["ingredient"]));
                        if (alts.Count == 0) Dropped(id, type, "no item input for a Bedrock furnace recipe (Java ingredient " + r["ingredient"].ToCompact() + ")");
                        // (the input is a plain item name, as the smithing slots below: a data value cannot be named)
                        if (alts.Any(a => a["data"] != null))
                        {
                            Errors.Add("recipe " + id + " (" + type + "): input alternative(s) with a data value, which a Bedrock furnace recipe's input cannot name");
                            return outList;
                        }
                        int n = 0;
                        foreach (var a in alts)
                        {
                            string rid = id + (alts.Count > 1 ? "_" + n : "");
                            var res = Result(r["result"]);
                            var body = J.Obj().Set("description", J.Obj().Set("identifier", rid)).Set("tags", tags.Clone())
                                .Set("input", a["item"].AsString()).Set("output", res["item"].AsString()).Set("unlock", Unlock(ns, path, r));
                            outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_furnace", body));
                            n++;
                        }
                        return outList;
                    }
                case "minecraft:smithing_transform":
                    {
                        // Bedrock's smithing recipe names one item per slot: one recipe per combination of the template,
                        // base and addition alternatives (tags expanded to their members). Its slots are plain item names
                        // (no aux data), so an alternative with a data value cannot be expressed: the recipe is an error
                        var slots = new List<List<J>> { ItemsOnly(Alternatives(r["template"])), ItemsOnly(Alternatives(r["base"])), ItemsOnly(Alternatives(r["addition"])) };
                        if (slots.Any(s => s.Count == 0)) { Dropped(id, type, "template, base or addition has no Bedrock item"); return outList; }
                        var withData = slots.SelectMany(s => s).Where(a => a["data"] != null).ToList();
                        if (withData.Count > 0)
                        {
                            Errors.Add("recipe " + id + " (" + type + "): smithing alternative(s) " + string.Join(", ", withData.Select(a => a.ToCompact())) + " carry a data value, which Bedrock's smithing recipe cannot name");
                            return outList;
                        }
                        var combos = Expand(id, slots);
                        if (combos == null) return outList;
                        int n = 0;
                        foreach (var combo in combos)
                        {
                            string rid = id + (combos.Count > 1 ? "_" + n : "");
                            var body = J.Obj().Set("description", J.Obj().Set("identifier", rid)).Set("tags", J.Arr().Add("smithing_table"))
                                .Set("template", combo[0]["item"].AsString()).Set("base", combo[1]["item"].AsString()).Set("addition", combo[2]["item"].AsString())
                                .Set("result", Result(r["result"])["item"].AsString());
                            outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_smithing_transform", body));
                            n++;
                        }
                        return outList;
                    }
                default:
                    return outList; // custom types: Java data only (data/<ns>/recipe), loaded by their owner
            }
        }
    }
}
