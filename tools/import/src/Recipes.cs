// GloamingForest import tool (contract "import", unit F1): TF's vanilla-type recipes (crafting shaped /
// shapeless, smelting, blasting, smoking, campfire, stonecutting, smithing) as Bedrock behaviour-pack
// recipes, unlocked by the items of TF's recipe advancements. Custom TF types (drying, uncrafting, ...)
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

        const int MaxRecipeExpansion = 64;

        /// <summary>The alternatives of a Java ingredient as Bedrock recipe items ({"item": ...} or {"tag": ...}).</summary>
        List<J> Alternatives(J ing)
        {
            var res = new List<J>();
            if (ing == null) return res;
            if (ing.IsArr) { foreach (var x in ing.A) res.AddRange(Alternatives(x)); return res; }
            if (ing["item"] != null) { res.Add(J.Obj().Set("item", ing["item"].AsString())); return res; }
            if (ing["tag"] != null)
            {
                string tag = ing["tag"].AsString();
                string bt;
                if (BedrockItemTags.TryGetValue(tag, out bt)) { res.Add(J.Obj().Set("tag", bt)); return res; }
                var members = TagMembers(tag);
                // tags of TF items only (not blocks) are declared on those items (minecraft:tags, see ItemDoc)
                if (members.Count > 1 && members.All(IsTaggableItem)) { res.Add(J.Obj().Set("tag", tag)); return res; }
                // other tags do not exist on Bedrock: one recipe per member (see BedrockRecipes)
                foreach (var m in members) res.Add(J.Obj().Set("item", m));
                if (members.Count == 0) Warn("recipe ingredient tag " + tag + " has no known members");
            }
            return res;
        }

        /// <summary>A TF item with its own item JSON (so it can carry minecraft:tags).</summary>
        bool IsTaggableItem(string full)
        {
            return full.StartsWith("twilightforest:", StringComparison.Ordinal) && OwnerOf("item", full) != null && !BlockInfos.ContainsKey(full.Substring(15));
        }

        static J Result(J r)
        {
            if (r == null) return null;
            if (r.IsStr) return J.Obj().Set("item", r.AsString());
            var o = J.Obj().Set("item", (r["id"] ?? r["item"]).AsString());
            if (r["count"] != null && r["count"].AsLong(1) != 1) o.Set("count", r["count"].AsLong(1));
            return o;
        }

        /// <summary>The unlock list from the recipe's advancement (items of its inventory_changed criteria).</summary>
        J Unlock(string ns, string path, J recipe)
        {
            var items = new SortedSet<string>(StringComparer.Ordinal);
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
                        if (c["trigger"] == null || c["trigger"].AsString() != "minecraft:inventory_changed") continue;
                        var cond = c["conditions"];
                        if (cond == null || cond["items"] == null) continue;
                        foreach (var pred in cond["items"].A)
                        {
                            var it = pred["items"];
                            if (it == null) continue;
                            if (it.IsStr && !it.AsString().StartsWith("#", StringComparison.Ordinal)) items.Add(it.AsString());
                            else if (it.IsArr) foreach (var x in it.A) if (x.IsStr) items.Add(x.AsString());
                        }
                    }
                    break;
                }
            }
            // an item list, or (no item criteria, e.g. tag-only ones) the context object
            if (items.Count == 0) return J.Obj().Set("context", "AlwaysUnlocked");
            var arr = J.Arr();
            foreach (var i in items) arr.Add(J.Obj().Set("item", i));
            return arr;
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
                foreach (var ns in new[] { "twilightforest", "minecraft" })
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
                        // Bedrock measures pattern rows in bytes: map non-ASCII keys (TF uses "∴", "•") to free ASCII letters
                        var pattern = r["pattern"].A.Select(p => p.AsString()).ToList();
                        var free = new Queue<char>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789".Where(c => !keys.Contains(c.ToString())));
                        for (int i = 0; i < keys.Count; i++)
                        {
                            if (keys[i].All(c => c < 128)) continue;
                            string to = free.Dequeue().ToString();
                            pattern = pattern.Select(p => p.Replace(keys[i], to)).ToList();
                            keys[i] = to;
                        }
                        if (alts.Any(a => a.Count == 0)) return outList;
                        var combos = Cartesian(alts).Take(MaxRecipeExpansion + 1).ToList();
                        if (combos.Count > MaxRecipeExpansion) { Warn("recipe " + id + ": too many ingredient combinations, using the first alternatives"); combos = new List<List<J>> { alts.Select(a => a[0]).ToList() }; }
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
                        if (alts.Any(a => a.Count == 0)) return outList;
                        var combos = Cartesian(alts).Take(MaxRecipeExpansion + 1).ToList();
                        if (combos.Count > MaxRecipeExpansion) { Warn("recipe " + id + ": too many ingredient combinations, using the first alternatives"); combos = new List<List<J>> { alts.Select(a => a[0]).ToList() }; }
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
                        var alts = Alternatives(r["ingredient"]).Where(a => a["item"] != null).ToList();
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
                        var t = Alternatives(r["template"]); var b = Alternatives(r["base"]); var a = Alternatives(r["addition"]);
                        if (t.Count == 0 || b.Count == 0 || a.Count == 0) return outList;
                        var body = J.Obj().Set("description", J.Obj().Set("identifier", id)).Set("tags", J.Arr().Add("smithing_table"))
                            .Set("template", t[0]["item"] != null ? t[0]["item"].AsString() : t[0]["tag"].AsString())
                            .Set("base", b[0]["item"] != null ? b[0]["item"].AsString() : b[0]["tag"].AsString())
                            .Set("addition", a[0]["item"] != null ? a[0]["item"].AsString() : a[0]["tag"].AsString())
                            .Set("result", Result(r["result"])["item"].AsString());
                        outList.Add(J.Obj().Set("format_version", "1.20.10").Set("minecraft:recipe_smithing_transform", body));
                        return outList;
                    }
                default:
                    return outList; // custom types: Java data only (data/<ns>/recipe), loaded by their owner
            }
        }
    }
}
