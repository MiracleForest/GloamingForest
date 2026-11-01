// GloamingForest import tool (contract "import", unit F1): the index of TF ids that TF's data names but
// that are not data files: ids TF registers in code (travellers modifier types, biome layer types,
// marker handlers, restrictions, damage effects, ...), texture locations, template ids, and tags of
// TF's custom registries. assemble's dangling-reference check (tools/xmake/assemble.lua) knows ids from
// data files, BP definitions and docs/spec/owners.tsv only; this index (data/twilightforest/gf_known/**,
// data/twilightforest/tags/gf_known/**) records the others so the check sees them as known. It has no
// runtime meaning.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed partial class Ctx
    {
        static readonly Regex TfRef = new Regex(@"^#?twilightforest:[a-z0-9_/.\-]+$", RegexOptions.Compiled);

        /// <summary>assemble.lua's data_registry: data path below data/ -> (registry, "ns:path"), or null.</summary>
        public static Tuple<string, string> DataRegistry(string rel)
        {
            if (!rel.EndsWith(".json", StringComparison.Ordinal)) return null;
            int slash = rel.IndexOf('/');
            if (slash < 0) return null;
            string ns = rel.Substring(0, slash);
            var parts = rel.Substring(slash + 1, rel.Length - slash - 1 - 5).Split('/');
            int depth = 1;
            if (parts[0] == "tags") depth = parts.Length > 1 && parts[1] == "worldgen" ? 3 : 2;
            else if (parts[0] == "worldgen" || parts[0] == "twilight" || parts[0] == "data_maps") depth = 2;
            if (parts.Length <= depth) return null;
            return Tuple.Create(string.Join("/", parts.Take(depth)), ns + ":" + string.Join("/", parts.Skip(depth)));
        }

        public void ImportKnownIndex()
        {
            var known = new HashSet<string>(StringComparer.Ordinal);
            var knownTags = new HashSet<string>(StringComparer.Ordinal);
            // owners.tsv ids
            foreach (var kind in Owners.Keys)
            {
                foreach (var id in Owners[kind].Keys)
                {
                    if (id.IndexOfAny(new[] { '*', '?', '{' }) >= 0) continue;
                    var tm = Regex.Match(id, @"^(.+)/#(.+)$");
                    if (tm.Success) { if (kind == "tag") knownTags.Add(tm.Groups[2].Value); continue; }
                    var em = Regex.Match(id, @"^([^:]+)/([^/]+:.+)$");
                    if (kind == "registry_entry" && em.Success) known.Add(em.Groups[2].Value);
                    else if (id.Contains(":")) known.Add(id);
                    else if (kind == "sound_event") known.Add("twilightforest:" + id);
                }
            }
            // generated data files and BP definitions
            var dataFiles = new List<GenFile>();
            var bpFiles = new List<GenFile>();
            foreach (var unit in Out.Values)
                foreach (var f in unit.Values)
                {
                    if (f.Rel.StartsWith("data/", StringComparison.Ordinal))
                    {
                        var dr = DataRegistry(f.Rel.Substring(5));
                        if (dr != null)
                        {
                            if (dr.Item1.StartsWith("tags/", StringComparison.Ordinal)) knownTags.Add(dr.Item2);
                            else known.Add(dr.Item2);
                        }
                        if (!f.Rel.StartsWith("data/gf/", StringComparison.Ordinal) && f.Rel.EndsWith(".json", StringComparison.Ordinal)) dataFiles.Add(f);
                    }
                    else if (f.Rel.StartsWith("bp/", StringComparison.Ordinal) && f.Rel.EndsWith(".json", StringComparison.Ordinal))
                    {
                        var m = Regex.Match(Encoding.UTF8.GetString(f.Bytes), @"""identifier"":\s*""(twilightforest:[^""]+)""");
                        if (m.Success) known.Add(m.Groups[1].Value);
                        bpFiles.Add(f);
                    }
                }
            // TF strings in the imported data that are neither
            var missing = new SortedDictionary<string, SortedSet<string>>(StringComparer.Ordinal);
            foreach (var f in dataFiles)
            {
                J doc;
                try { doc = J.Parse(Encoding.UTF8.GetString(f.Bytes)); } catch { continue; }
                Visit(doc, null, s =>
                {
                    bool tag = s.StartsWith("#", StringComparison.Ordinal);
                    string id = tag ? s.Substring(1) : s;
                    if (tag ? knownTags.Contains(id) : known.Contains(id)) return;
                    SortedSet<string> refs;
                    if (!missing.TryGetValue(s, out refs)) missing[s] = refs = new SortedSet<string>(StringComparer.Ordinal);
                    refs.Add(f.Rel);
                });
            }
            // Bedrock item tags (minecraft:tags, recipe "tag" keys) are plain strings too
            foreach (var f in bpFiles)
            {
                J doc;
                try { doc = J.Parse(Encoding.UTF8.GetString(f.Bytes)); } catch { continue; }
                VisitKeys(doc, null, new[] { "tags", "tag" }, s =>
                {
                    if (known.Contains(s)) return;
                    SortedSet<string> refs;
                    if (!missing.TryGetValue(s, out refs)) missing[s] = refs = new SortedSet<string>(StringComparer.Ordinal);
                    refs.Add(f.Rel);
                });
            }
            foreach (var kv in missing)
            {
                bool tag = kv.Key.StartsWith("#", StringComparison.Ordinal);
                string path = kv.Key.Substring(kv.Key.IndexOf(':') + 1);
                var entry = J.Obj()
                    .Set("comment", "Named by TF data, not a data file (code registry id, texture, template or custom-registry tag); index for the assemble reference check only (F1, tools/import/src/Known.cs).")
                    .Set("referencedBy", J.Strs(kv.Value));
                if (tag)
                {
                    entry.Set("values", J.Arr());
                    EmitJson("tags", ImportUnit, "data/twilightforest/tags/gf_known/" + path + ".json", entry);
                }
                else EmitJson("registries", ImportUnit, "data/twilightforest/gf_known/" + path + ".json", entry);
            }
        }

        static void VisitKeys(J v, string key, string[] keys, Action<string> onRef)
        {
            if (v == null) return;
            if (v.IsStr) { if (key != null && keys.Contains(key) && TfRef.IsMatch(v.S) && !v.S.StartsWith("#", StringComparison.Ordinal)) onRef(v.S); return; }
            if (v.IsArr) { foreach (var x in v.A) VisitKeys(x, key, keys, onRef); return; }
            if (v.IsObj) foreach (var kv in v.O) VisitKeys(kv.Value, kv.Key, keys, onRef);
        }

        static void Visit(J v, string key, Action<string> onRef)
        {
            if (v == null) return;
            if (v.IsStr) { if (key != "identifier" && TfRef.IsMatch(v.S)) onRef(v.S); return; }
            if (v.IsArr) { foreach (var x in v.A) Visit(x, key, onRef); return; }
            if (v.IsObj) foreach (var kv in v.O) Visit(kv.Value, kv.Key, onRef);
        }
    }
}
