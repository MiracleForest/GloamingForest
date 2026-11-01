// GloamingForest import tool (contract "import", unit F1): TF's lang files (CC BY-NC-SA 4.0): en_us (2061
// keys) and the translations, for the 29 Bedrock locales, split by key owner into the mod's lang JSON
// (gf::tr) and resource-pack texts (block / item / entity names, and every TF key for rawtext).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace GfImport
{
    public sealed partial class Ctx
    {
        public List<string> BedrockLocales = new List<string>();
        public Dictionary<string, string> LocaleAliases = new Dictionary<string, string>(StringComparer.Ordinal);

        void LoadLocales()
        {
            var merges = ReadJson(Path.Combine(Root, "tools", "xmake", "merges.json"));
            BedrockLocales = merges["locales"].A.Select(x => x.AsString()).ToList();
            if (merges["localeAliases"] != null)
                foreach (var k in merges["localeAliases"].Keys) LocaleAliases[k] = merges["localeAliases"][k].AsString();
        }

        /// <summary>The Bedrock locale of a TF lang file name ("de_de" -> "de_DE"), or null.</summary>
        string BedrockLocale(string tfName)
        {
            var p = tfName.Split('_');
            if (p.Length != 2) return null;
            string l = p[0].ToLowerInvariant() + "_" + p[1].ToUpperInvariant();
            string a;
            if (LocaleAliases.TryGetValue(l, out a)) l = a;
            return BedrockLocales.Contains(l) ? l : null;
        }

        static string LangValue(string v)
        {
            // one line per key in .lang files
            return v.Replace("\r", "").Replace("\n", "~LINEBREAK~");
        }

        public void ImportLang()
        {
            if (BedrockLocales.Count == 0) LoadLocales();
            var en = ReadJson(Path.Combine(GenRes, "assets", "twilightforest", "lang", "en_us.json"));
            var enMap = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var k in en.Keys) enMap[k] = en[k].AsString() ?? en[k].ToCompact();
            // locale -> key -> text (TF's own file for that locale; the closest one wins for shared Bedrock locales)
            var byLocale = new SortedDictionary<string, Dictionary<string, string>>(StringComparer.Ordinal);
            byLocale["en_US"] = enMap;
            string langDir = Path.Combine(MainRes, "assets", "twilightforest", "lang");
            var files = Files(langDir, "*.json").OrderBy(f => f, StringComparer.Ordinal).ToList();
            foreach (var rel in files)
            {
                string name = StripExt(rel);
                string loc = BedrockLocale(name);
                if (loc == null || loc == "en_US") continue;
                // an exact match (nb_no -> nb_NO) beats an alias (no_no / nn_no -> nb_NO)
                bool exact = name.ToLowerInvariant() == loc.ToLowerInvariant();
                if (byLocale.ContainsKey(loc) && !exact) continue;
                var j = ReadJson(Path.Combine(langDir, rel));
                var m = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var k in j.Keys) if (enMap.ContainsKey(k) && j[k].IsStr && j[k].AsString().Length > 0) m[k] = j[k].AsString();
                byLocale[loc] = m;
            }

            // owners of the Bedrock name keys
            var nameKeys = new List<Tuple<string, string, string>>(); // bedrock key, TF key, owner
            foreach (var bi in BlockInfos.Values)
                nameKeys.Add(Tuple.Create("tile.twilightforest:" + bi.Id + ".name", "block.twilightforest." + bi.Id, bi.Owner));
            foreach (var full in Ids("item"))
            {
                string id = full.Substring(15);
                if (BlockInfos.ContainsKey(id)) continue; // the tile key names it
                nameKeys.Add(Tuple.Create("item." + full + ".name", "item.twilightforest." + id, OwnerOf("item", full)));
            }
            foreach (var full in Ids("entity"))
            {
                string id = full.Substring(15);
                nameKeys.Add(Tuple.Create("entity." + full + ".name", "entity.twilightforest." + id, OwnerOf("entity", full)));
            }
            foreach (var full in Ids("spawn_egg"))
            {
                string id = full.Substring(15);
                string ent = "twilightforest:" + id.Substring(0, id.Length - "_spawn_egg".Length);
                nameKeys.Add(Tuple.Create("item.spawn_egg.entity." + ent + ".name", "item.twilightforest." + id, OwnerOf("spawn_egg", full)));
            }
            // creative group titles: the catalog shows the group name ("twilightforest:<title key>") through the texts
            foreach (var tab in Tabs)
                if (tab.TitleKey != null) nameKeys.Add(Tuple.Create("twilightforest:" + tab.TitleKey, tab.TitleKey, OwnerOf("lang", tab.TitleKey) ?? ImportUnit));

            foreach (var loc in byLocale.Keys)
            {
                var map = byLocale[loc];
                // mod lang JSON: the TF keys this locale translates
                var perUnitJson = new SortedDictionary<string, J>(StringComparer.Ordinal);
                var perUnitLang = new SortedDictionary<string, SortedDictionary<string, string>>(StringComparer.Ordinal);
                Action<string, string, string> addText = (owner, key, value) =>
                {
                    SortedDictionary<string, string> d;
                    if (!perUnitLang.TryGetValue(owner, out d)) perUnitLang[owner] = d = new SortedDictionary<string, string>(StringComparer.Ordinal);
                    d[key] = value;
                };
                foreach (var key in map.Keys.OrderBy(k => k, StringComparer.Ordinal))
                {
                    string owner = OwnerOf("lang", key) ?? ImportUnit;
                    J o;
                    if (!perUnitJson.TryGetValue(owner, out o)) perUnitJson[owner] = o = J.Obj();
                    o.Set(key, map[key]);
                    addText(owner, key, LangValue(map[key]));
                }
                foreach (var nk in nameKeys)
                {
                    string text;
                    string tfKey = nk.Item2 ?? nk.Item1;
                    if (!map.TryGetValue(tfKey, out text))
                    {
                        if (loc != "en_US") continue;  // Bedrock falls back to en_US
                        text = nk.Item2 == null ? null : HumanName(nk.Item2);
                        if (text == null) continue;
                    }
                    if (nk.Item1 == tfKey) continue; // already written as the TF key itself
                    addText(nk.Item3 ?? ImportUnit, nk.Item1, LangValue(text));
                }
                foreach (var kv in perUnitJson) EmitJson("lang", kv.Key, "lang/" + loc + "/" + NameOf(kv.Key) + ".json", kv.Value);
                foreach (var kv in perUnitLang)
                {
                    var sb = new StringBuilder();
                    foreach (var line in kv.Value) sb.Append(line.Key).Append('=').Append(line.Value).Append('\n');
                    EmitText("lang", kv.Key, "rp/texts.d/" + loc + "/" + NameOf(kv.Key) + ".lang", sb.ToString());
                }
            }
        }

        /// <summary>"block.twilightforest.naga_stone" -> "Naga Stone" (for the few ids TF's en_us does not name).</summary>
        static string HumanName(string key)
        {
            string id = key.Substring(key.LastIndexOf('.') + 1);
            return string.Join(" ", id.Split('_').Where(s => s.Length > 0).Select(s => char.ToUpperInvariant(s[0]) + s.Substring(1)));
        }
    }
}
