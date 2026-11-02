// GloamingForest import tool (contract "import", unit F1): the reference check of the imported data.
// Every twilightforest: string of the generated data and BP files must name something that exists: a
// generated data file (by registry, tags with Java's registry paths), a BP definition, an owners.tsv id, or
// an entry of the reviewed allowlist tools/import/known-ids.json (code-registered types, texture locations,
// template pools, each with its kind, reason and TF source). A reference found nowhere fails the import;
// references that are dangling in TF itself ("tfDangling") are kept verbatim and reported as warnings.
//
// Bridge for assemble (F0, tools/xmake/assemble.lua): its dangling-reference check knows only data files,
// BP definitions and owners.tsv rows, checks the values of "tag" / "tags" keys as plain ids, and reads
// data/<ns>/tags/twilight/<registry>/ as the registry "tags/twilight". Until it reads the allowlist itself
// (requested from F0 / A8: issue #21, docs/spec/import.md), the import writes one index file per reference this check
// resolved but assemble cannot (data/twilightforest/gf_known/**, data/twilightforest/tags/gf_known/**, each
// naming its kind and reason). Nothing reads them at run time. The tfDangling reference is bridged too: assemble's check
// has no warning level, so leaving it out would fail every build (issue #21, change 4).
using System;
using System.Collections.Generic;
using System.IO;
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

        /// <summary>The tag id of a data path as Java reads it (tags/worldgen/x/ and tags/twilight/x/ are
        /// two-segment registries), or null.</summary>
        static string JavaTagId(string rel)
        {
            var m = Regex.Match(rel, @"^([^/]+)/tags/((?:worldgen|twilight)/[^/]+|[^/]+)/(.+)\.json$");
            return m.Success ? m.Groups[1].Value + ":" + m.Groups[3].Value : null;
        }

        sealed class Ref
        {
            public string Value;                                                   // "#twilightforest:x" or "twilightforest:x"
            public bool TagKey;                                                    // the value of a "tag" / "tags" key
            public SortedSet<string> Files = new SortedSet<string>(StringComparer.Ordinal);
        }

        public void ImportKnownIndex()
        {
            var allowFile = Path.Combine(Root, "tools", "import", "known-ids.json");
            var allow = File.Exists(allowFile) ? ReadJson(allowFile) : J.Obj();
            var allowIds = allow["ids"] ?? J.Obj();
            var dangling = allow["tfDangling"] ?? J.Obj();
            // JSON keys whose value is a tag written without '#' (TagKey codecs, Bedrock item tags)
            var tagKeys = new HashSet<string>((allow["tagValuedKeys"] ?? J.Obj()).Keys, StringComparer.Ordinal);

            // what assemble knows (its known_ids: owners.tsv ids, data files by its registry rule, BP identifiers)
            var known = new HashSet<string>(StringComparer.Ordinal);
            var knownTags = new HashSet<string>(StringComparer.Ordinal);
            // what Java knows besides: tags by Java's registry paths
            var javaTags = new HashSet<string>(StringComparer.Ordinal);
            foreach (var kind in Owners.Keys)
            {
                foreach (var id in Owners[kind].Keys)
                {
                    if (id.IndexOfAny(new[] { '*', '?', '{' }) >= 0) continue;
                    var tm = Regex.Match(id, @"^(.+)/#(.+)$");
                    if (tm.Success) { if (kind == "tag") { knownTags.Add(tm.Groups[2].Value); javaTags.Add(tm.Groups[2].Value); } continue; }
                    var em = Regex.Match(id, @"^([^:]+)/([^/]+:.+)$");
                    if (kind == "registry_entry" && em.Success) known.Add(em.Groups[2].Value);
                    else if (id.Contains(":")) known.Add(id);
                    else if (kind == "sound_event") known.Add("twilightforest:" + id);
                }
            }
            var dataFiles = new List<GenFile>();
            var bpFiles = new List<GenFile>();
            foreach (var unit in Out.Values)
                foreach (var f in unit.Values)
                {
                    if (f.Rel.StartsWith("data/", StringComparison.Ordinal))
                    {
                        if (f.Rel.StartsWith("data/twilightforest/gf_known/", StringComparison.Ordinal) || f.Rel.StartsWith("data/twilightforest/tags/gf_known/", StringComparison.Ordinal)) continue;
                        var dr = DataRegistry(f.Rel.Substring(5));
                        if (dr != null)
                        {
                            if (dr.Item1.StartsWith("tags/", StringComparison.Ordinal)) knownTags.Add(dr.Item2);
                            else known.Add(dr.Item2);
                        }
                        var jt = JavaTagId(f.Rel.Substring(5));
                        if (jt != null) javaTags.Add(jt);
                        if (!f.Rel.StartsWith("data/gf/", StringComparison.Ordinal) && f.Rel.EndsWith(".json", StringComparison.Ordinal)) dataFiles.Add(f);
                    }
                    else if (f.Rel.StartsWith("bp/", StringComparison.Ordinal) && f.Rel.EndsWith(".json", StringComparison.Ordinal))
                    {
                        var m = Regex.Match(Encoding.UTF8.GetString(f.Bytes), @"""identifier"":\s*""(twilightforest:[^""]+)""");
                        if (m.Success) known.Add(m.Groups[1].Value);
                        bpFiles.Add(f);
                    }
                }

            // the references assemble would not resolve
            var refs = new SortedDictionary<string, Ref>(StringComparer.Ordinal);
            Action<string, string, string> note = (s, key, file) =>
            {
                bool tag = s.StartsWith("#", StringComparison.Ordinal);
                string id = tag ? s.Substring(1) : s;
                if (tag ? knownTags.Contains(id) : known.Contains(id)) return;
                Ref r;
                if (!refs.TryGetValue(s, out r)) refs[s] = r = new Ref { Value = s };
                if (key != null && tagKeys.Contains(key)) r.TagKey = true;
                r.Files.Add(file);
            };
            foreach (var f in dataFiles)
            {
                J doc;
                try { doc = J.Parse(Encoding.UTF8.GetString(f.Bytes)); } catch { continue; }
                Visit(doc, null, (s, key) => note(s, key, f.Rel));
            }
            // Bedrock item tags (minecraft:tags, recipe "tag" keys) are plain strings
            foreach (var f in bpFiles)
            {
                J doc;
                try { doc = J.Parse(Encoding.UTF8.GetString(f.Bytes)); } catch { continue; }
                VisitKeys(doc, null, new[] { "tags", "tag" }, s => note(s, "tags", f.Rel));
            }

            // resolve each one the way Java does, then the allowlist; the rest is a dangling reference
            var usedAllow = new HashSet<string>(StringComparer.Ordinal);
            int bridged = 0;
            foreach (var r in refs.Values)
            {
                bool tag = r.Value.StartsWith("#", StringComparison.Ordinal);
                string id = tag ? r.Value.Substring(1) : r.Value;
                string kind, reason;
                if (tag && javaTags.Contains(id))
                {
                    kind = "tag";
                    reason = "a TF tag of a two-segment registry (data/<ns>/tags/twilight/<registry>/ or tags/worldgen/<registry>/), which assemble reads with a one-segment registry";
                }
                else if (!tag && r.TagKey && javaTags.Contains(id))
                {
                    kind = "tag";
                    reason = "a TF tag named without '#' as the value of a tag-valued key (tools/import/known-ids.json tagValuedKeys: Java ingredients and block predicates, TagKey codecs, Bedrock item tags), which assemble checks as a plain id";
                }
                else if (allowIds[r.Value] != null)
                {
                    kind = allowIds[r.Value]["kind"].AsString();
                    reason = allowIds[r.Value]["reason"].AsString() + " (" + allowIds[r.Value]["source"].AsString() + ")";
                    usedAllow.Add(r.Value);
                }
                else if (dangling[r.Value] != null)
                {
                    kind = dangling[r.Value]["kind"].AsString();
                    reason = "dangling in TF itself: " + dangling[r.Value]["reason"].AsString();
                    usedAllow.Add(r.Value);
                    Warn("dangling reference " + r.Value + " (" + kind + ") in " + string.Join(", ", r.Files) + ": " + dangling[r.Value]["reason"].AsString());
                }
                else
                {
                    Errors.Add("dangling reference " + r.Value + " in " + string.Join(", ", r.Files.Take(4)) + (r.Files.Count > 4 ? " (+" + (r.Files.Count - 4) + ")" : "")
                        + ": not a generated data file, BP definition or owners.tsv id; add it to tools/import/known-ids.json with its kind and TF source if TF registers it in code");
                    continue;
                }
                // the index file assemble reads (see the header)
                string path = id.Substring(id.IndexOf(':') + 1);
                var entry = J.Obj()
                    .Set("comment", "Index for the assemble reference check only (F1, tools/import/src/Known.cs): assemble cannot resolve this reference yet; nothing reads this file at run time.")
                    .Set("kind", kind).Set("reason", reason)
                    .Set("referencedBy", J.Strs(r.Files));
                if (tag) EmitJson("tags", ImportUnit, "data/twilightforest/tags/gf_known/" + path + ".json", entry.Set("values", J.Arr()));
                else EmitJson("registries", ImportUnit, "data/twilightforest/gf_known/" + path + ".json", entry);
                bridged++;
            }
            foreach (var k in allowIds.Keys.Concat(dangling.Keys))
                if (!usedAllow.Contains(k)) Warn("tools/import/known-ids.json: " + k + " is not referenced (stale entry, or assemble resolves it now)");
            Log.Add(refs.Count + " references assemble cannot resolve: " + bridged + " resolved (Java tag paths, tools/import/known-ids.json), "
                + (refs.Count - bridged) + " dangling");
        }

        static void VisitKeys(J v, string key, string[] keys, Action<string> onRef)
        {
            if (v == null) return;
            if (v.IsStr) { if (key != null && keys.Contains(key) && TfRef.IsMatch(v.S) && !v.S.StartsWith("#", StringComparison.Ordinal)) onRef(v.S); return; }
            if (v.IsArr) { foreach (var x in v.A) VisitKeys(x, key, keys, onRef); return; }
            if (v.IsObj) foreach (var kv in v.O) VisitKeys(kv.Value, kv.Key, keys, onRef);
        }

        static void Visit(J v, string key, Action<string, string> onRef)
        {
            if (v == null) return;
            if (v.IsStr) { if (key != "identifier" && TfRef.IsMatch(v.S)) onRef(v.S, key); return; }
            if (v.IsArr) { foreach (var x in v.A) Visit(x, key, onRef); return; }
            if (v.IsObj) foreach (var kv in v.O) Visit(kv.Value, kv.Key, onRef);
        }
    }
}
