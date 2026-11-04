// GloamingForest import tool (contract "import", unit F1): the shared state of one run: the TF
// checkout, docs/spec/owners.tsv, the generated files per unit, overrides, and the write / check step.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class GenFile
    {
        public string Unit;     // unit name ("trees", "import", ...)
        public string Rel;      // path below assets/gen/import/<unit>/ ("bp/blocks/x.json")
        public byte[] Bytes;
        public string Kind;     // the -Kinds value that produces it
    }

    public sealed partial class Ctx
    {
        public string Root;       // repository root
        public string Tf;         // TF checkout root
        public string GenRes, MainRes, JavaDir;
        public readonly List<string> Log = new List<string>();
        public readonly List<string> Warnings = new List<string>();

        // owners.tsv: kind -> id -> unit id (F1, U4, ...); unit id -> unit name
        public readonly Dictionary<string, Dictionary<string, string>> Owners = new Dictionary<string, Dictionary<string, string>>();
        public readonly Dictionary<string, string> UnitNames = new Dictionary<string, string>();
        public const string ImportUnit = "F1";

        // generated output: unit name -> rel -> file
        public readonly SortedDictionary<string, SortedDictionary<string, GenFile>> Out =
            new SortedDictionary<string, SortedDictionary<string, GenFile>>(StringComparer.Ordinal);
        // docs written by the tool (repo-relative path -> text)
        public readonly SortedDictionary<string, string> Docs = new SortedDictionary<string, string>(StringComparer.Ordinal);

        public Ctx(string root, string tf)
        {
            Root = root;
            Tf = tf;
            GenRes = Path.Combine(tf, "src", "generated", "resources");
            MainRes = Path.Combine(tf, "src", "main", "resources");
            JavaDir = Path.Combine(tf, "src", "main", "java", "twilightforest");
            if (!Directory.Exists(JavaDir)) throw new Exception("TF checkout not found at " + tf + " (run: pwsh tools/tf/fetch.ps1)");
            LoadOwners();
        }

        // ---- reading TF ----------------------------------------------------------------------------

        /// <summary>Text of a TF file, line endings normalised to LF (the checkout may be CRLF on CI).</summary>
        public static string ReadText(string path)
        {
            var s = File.ReadAllText(path, new UTF8Encoding(false));
            if (s.Length > 0 && s[0] == '﻿') s = s.Substring(1);
            return s.Replace("\r\n", "\n").Replace("\r", "\n");
        }

        public static J ReadJson(string path)
        {
            try { return J.Parse(ReadText(path)); }
            catch (Exception e) { throw new Exception("cannot parse " + path + ": " + e.Message); }
        }

        /// <summary>Files below dir, as sorted forward-slash paths relative to dir.</summary>
        public static List<string> Files(string dir, string pattern = "*")
        {
            if (!Directory.Exists(dir)) return new List<string>();
            var baseLen = Path.GetFullPath(dir).TrimEnd('\\', '/').Length + 1;
            var list = Directory.GetFiles(dir, pattern, SearchOption.AllDirectories)
                .Select(f => Path.GetFullPath(f).Substring(baseLen).Replace('\\', '/'))
                .Where(f => !f.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase) && !f.EndsWith(".nbt", StringComparison.OrdinalIgnoreCase))
                .ToList();
            list.Sort(StringComparer.Ordinal);
            return list;
        }

        public static string StripExt(string rel)
        {
            int dot = rel.LastIndexOf('.');
            int slash = rel.LastIndexOf('/');
            return dot > slash ? rel.Substring(0, dot) : rel;
        }

        // ---- owners --------------------------------------------------------------------------------

        void LoadOwners()
        {
            var rules = ReadJson(Path.Combine(Root, "tools", "spec", "owners.rules.json"));
            foreach (var u in rules["units"].Keys) UnitNames[u] = rules["units"][u]["name"].AsString();
            foreach (var line in File.ReadAllLines(Path.Combine(Root, "docs", "spec", "owners.tsv")))
            {
                if (line.StartsWith("#") || line.StartsWith("kind\t") || line.Length == 0) continue;
                var c = line.Split('\t');
                if (c.Length < 3) continue;
                Dictionary<string, string> m;
                if (!Owners.TryGetValue(c[0], out m)) Owners[c[0]] = m = new Dictionary<string, string>(StringComparer.Ordinal);
                m[c[1]] = c[2];
            }
        }

        /// <summary>The owning unit id of an id of a kind (null when owners.tsv has no row).</summary>
        public string OwnerOf(string kind, string id)
        {
            Dictionary<string, string> m;
            string o;
            if (Owners.TryGetValue(kind, out m) && m.TryGetValue(id, out o)) return o;
            return null;
        }

        /// <summary>The owner of an item id: item, block item, block or spawn egg.</summary>
        public string ItemOwner(string id)
        {
            foreach (var k in new[] { "item", "block_item", "block", "spawn_egg" })
            {
                var o = OwnerOf(k, id);
                if (o != null) return o;
            }
            return null;
        }

        public string NameOf(string unitId)
        {
            string n;
            if (unitId != null && UnitNames.TryGetValue(unitId, out n)) return n;
            return UnitNames[ImportUnit];
        }

        public IEnumerable<string> Ids(string kind)
        {
            Dictionary<string, string> m;
            if (!Owners.TryGetValue(kind, out m)) return Enumerable.Empty<string>();
            var l = m.Keys.ToList();
            l.Sort(StringComparer.Ordinal);
            return l;
        }

        // ---- output --------------------------------------------------------------------------------

        public void Emit(string kind, string unitId, string rel, byte[] bytes)
        {
            string unit = NameOf(unitId);
            SortedDictionary<string, GenFile> files;
            if (!Out.TryGetValue(unit, out files)) Out[unit] = files = new SortedDictionary<string, GenFile>(StringComparer.Ordinal);
            GenFile existing;
            if (files.TryGetValue(rel, out existing))
            {
                if (existing.Bytes.SequenceEqual(bytes)) return;
                throw new Exception(string.Format("{0}: {1}/{2} generated twice with different content (kinds {3} and {4})", "emit", unit, rel, existing.Kind, kind));
            }
            files[rel] = new GenFile { Unit = unit, Rel = rel, Bytes = bytes, Kind = kind };
        }

        public void EmitText(string kind, string unitId, string rel, string text)
        {
            Emit(kind, unitId, rel, new UTF8Encoding(false).GetBytes(text));
        }

        public void EmitJson(string kind, string unitId, string rel, J doc)
        {
            EmitText(kind, unitId, rel, doc.ToText());
        }

        /// <summary>Fragments merged per unit: unit -> rel -> key -> value (an object fragment).</summary>
        readonly Dictionary<string, SortedDictionary<string, J>> fragments = new Dictionary<string, SortedDictionary<string, J>>();
        readonly Dictionary<string, string> fragmentKinds = new Dictionary<string, string>();

        /// <summary>Adds key=value to the object fragment rel of a unit (sorted by key on write).</summary>
        public void Fragment(string kind, string unitId, string rel, string key, J value)
        {
            string fk = NameOf(unitId) + "|" + rel;
            SortedDictionary<string, J> f;
            if (!fragments.TryGetValue(fk, out f)) { fragments[fk] = f = new SortedDictionary<string, J>(StringComparer.Ordinal); fragmentKinds[fk] = kind; }
            J old;
            if (f.TryGetValue(key, out old) && old.ToCompact() != value.ToCompact())
                throw new Exception("fragment " + fk + ": key " + key + " defined twice");
            f[key] = value;
        }

        public void FlushFragments()
        {
            foreach (var fk in fragments.Keys.OrderBy(x => x, StringComparer.Ordinal))
            {
                var parts = fk.Split('|');
                var o = J.Obj();
                foreach (var kv in fragments[fk]) o.Set(kv.Key, kv.Value);
                string unitId = UnitNames.First(u => u.Value == parts[0]).Key;
                EmitJson(fragmentKinds[fk], unitId, parts[1], o);
            }
            fragments.Clear();
        }

        public void Warn(string msg) { Warnings.Add(msg); }

        // ---- overrides (tools/import/overrides/<unit>.json) ----------------------------------------

        public void ApplyOverrides()
        {
            string dir = Path.Combine(Root, "tools", "import", "overrides");
            if (!Directory.Exists(dir)) return;
            foreach (var file in Directory.GetFiles(dir, "*.json").OrderBy(f => f, StringComparer.Ordinal))
            {
                string unit = Path.GetFileNameWithoutExtension(file);
                if (!UnitNames.ContainsValue(unit)) { Errors.Add("override tools/import/overrides/" + Path.GetFileName(file) + " is not named after a unit"); continue; }
                SortedDictionary<string, GenFile> files;
                if (!Out.TryGetValue(unit, out files)) continue;
                var ov = ReadJson(file);
                // drop: globs of generated paths to leave out
                if (ov["drop"] != null)
                {
                    foreach (var g in ov["drop"].A)
                    {
                        var re = GlobRegex(g.AsString());
                        var hit = files.Keys.Where(k => re.IsMatch(k)).ToList();
                        if (hit.Count == 0) Warn("override " + unit + ": drop " + g.AsString() + " matches nothing");
                        foreach (var k in hit) files.Remove(k);
                    }
                }
                // patch: {"<rel>": [JSON patch ops]} applied to generated JSON files
                if (ov["patch"] != null)
                {
                    foreach (var rel in ov["patch"].Keys)
                    {
                        GenFile f;
                        if (!files.TryGetValue(rel, out f)) { Errors.Add("override " + unit + ": patch target " + rel + " is not generated"); continue; }
                        var doc = J.Parse(Encoding.UTF8.GetString(f.Bytes));
                        var before = IsFamiliesData(rel) ? FamilyTagsById(doc) : null;
                        try { doc = J.ApplyPatch(doc, ov["patch"][rel], "override " + unit + " " + rel); }
                        catch (Exception ex) { Errors.Add(ex.Message + ", not applied"); continue; }
                        // a family block's tags are overridden by id (familyTags), never through the entries' array index,
                        // which moves when a regeneration adds an earlier entry: such a patch is an error and is not applied
                        // (the run goes on, so it reports every other problem too)
                        if (before != null)
                        {
                            var after = FamilyTagsById(doc);
                            bool rejected = false;
                            // (an entry the patch adds or removes is reported by CheckEmittedBlocks / FamilyEntries)
                            foreach (var id in before.Keys.Intersect(after.Keys).OrderBy(x => x, StringComparer.Ordinal))
                            {
                                string b = before[id], a = after[id];
                                if (b == a) continue;
                                Errors.Add("override " + unit + ": patch of " + rel + " changes params.tags of " + id + " ([" + b + "] -> [" + a
                                    + "]), not applied: override a family block's tags with \"familyTags\": {\"" + id + "\": [...]} (docs/spec/import.md, Overrides)");
                                rejected = true;
                            }
                            if (rejected) continue;
                        }
                        f.Bytes = new UTF8Encoding(false).GetBytes(doc.ToText());
                    }
                }
                // familyTags: {"twilightforest:<id>": [tag ids]} sets params.tags of the unit's families entry of that id
                // (data/gf/families/<unit>.json), the single source of a family block's Bedrock block tags (SyncFamilyTags)
                if (ov["familyTags"] != null)
                {
                    // each problem is an error line and that entry (or the whole familyTags) is not applied
                    string rel = "data/gf/families/" + unit + ".json";
                    GenFile f = null;
                    J doc = null;
                    if (!ov["familyTags"].IsObj) Errors.Add("override " + unit + ": familyTags is not an object of block id -> [tag ids], not applied");
                    else if (!files.TryGetValue(rel, out f)) Errors.Add("override " + unit + ": familyTags needs " + rel + ", which is not generated (the unit owns no family block)");
                    else if (!(doc = J.Parse(Encoding.UTF8.GetString(f.Bytes))).IsArr) { Errors.Add("override " + unit + ": " + rel + " is not an array of family entries"); doc = null; }
                    foreach (var id in doc != null ? ov["familyTags"].Keys : Enumerable.Empty<string>())
                    {
                        string where = "override " + unit + ": familyTags " + id;
                        var tags = ov["familyTags"][id];
                        if (!tags.IsArr || !tags.A.All(x => x.IsStr)) { Errors.Add(where + " is not an array of tag ids: " + tags.ToCompact() + ", not applied"); continue; }
                        var bad = tags.A.Select(x => x.S).Where(UnknownHarvestTag).Distinct(StringComparer.Ordinal).ToList();
                        foreach (var t in bad) Errors.Add(where + ": unknown harvest tag " + t + ", not applied (the import knows " + KnownHarvestTagList + ")");
                        if (ReportDuplicateTags(where, tags.A.Select(x => x.S)) || bad.Count > 0) continue;
                        var entries = doc.A.Where(e => e.IsObj && e["id"] != null && e["id"].IsStr && e["id"].S == id).ToList();
                        if (entries.Count != 1) { Errors.Add(where + " names " + (entries.Count == 0 ? "no entry" : entries.Count + " entries") + " of " + rel + ", not applied"); continue; }
                        var entry = entries[0];
                        if (entry["params"] == null) entry.Set("params", J.Obj());
                        if (!entry["params"].IsObj) { Errors.Add(where + ": " + rel + " " + id + " params is not an object, not applied"); continue; }
                        if (tags.A.Count > 0) entry["params"].Set("tags", J.Strs(tags.A.Select(x => x.S)));
                        else entry["params"].Remove("tags"); // absent for a block without tags (docs/spec/import.md, Families)
                    }
                    if (doc != null) f.Bytes = new UTF8Encoding(false).GetBytes(doc.ToText());
                }
                // rename: {"<rel>": "<new rel>"}
                if (ov["rename"] != null)
                {
                    foreach (var rel in ov["rename"].Keys)
                    {
                        GenFile f;
                        if (!files.TryGetValue(rel, out f)) { Errors.Add("override " + unit + ": rename source " + rel + " is not generated"); continue; }
                        string to = ov["rename"][rel].AsString();
                        if (to == null) { Errors.Add("override " + unit + ": rename " + rel + " to " + ov["rename"][rel].ToCompact() + ", not a path"); continue; }
                        if (files.ContainsKey(to)) { Errors.Add("override " + unit + ": rename target " + to + " exists"); continue; }
                        files.Remove(rel);
                        f.Rel = to;
                        files[to] = f;
                    }
                }
            }
        }

        static bool IsFamiliesData(string rel)
        {
            return rel.StartsWith("data/gf/families/", StringComparison.Ordinal) && rel.EndsWith(".json", StringComparison.Ordinal);
        }

        /// <summary>params.tags of every entry of a families data file, by id, as a set (sorted, distinct: a reorder or a
        /// duplicated tag is no change; SyncFamilyTags reports a duplicate on its own); an entry without tags maps to "".
        /// Entries without an id are left out (an error later).</summary>
        static Dictionary<string, string> FamilyTagsById(J doc)
        {
            var r = new Dictionary<string, string>(StringComparer.Ordinal);
            if (doc == null || !doc.IsArr) return r;
            foreach (var e in doc.A)
            {
                if (!e.IsObj || e["id"] == null || !e["id"].IsStr) continue;
                var pt = e["params"] != null && e["params"].IsObj ? e["params"]["tags"] : null;
                string v = pt == null ? "" : pt.IsArr && pt.A.All(x => x.IsStr)
                    ? string.Join(",", pt.A.Select(x => x.S).Distinct(StringComparer.Ordinal).OrderBy(x => x, StringComparer.Ordinal))
                    : pt.ToCompact();
                r[e["id"].S] = v; // a duplicated id keeps the last (FamilyEntries reports it)
            }
            return r;
        }

        public static Regex GlobRegex(string glob)
        {
            var sb = new StringBuilder("^");
            int braces = 0;
            for (int i = 0; i < glob.Length; i++)
            {
                char c = glob[i];
                if (c == '*' && i + 1 < glob.Length && glob[i + 1] == '*') { sb.Append(".*"); i++; }
                else if (c == '*') sb.Append("[^/]*");
                else if (c == '?') sb.Append("[^/]");
                else if (c == '{') { sb.Append("(?:"); braces++; }
                else if (c == '}' && braces > 0) { sb.Append(")"); braces--; }
                else if (c == ',' && braces > 0) sb.Append("|");
                else sb.Append(Regex.Escape(c.ToString()));
            }
            sb.Append("$");
            return new Regex(sb.ToString());
        }

        // ---- write / check -------------------------------------------------------------------------

        /// <summary>The kind that produces a generated path (for -Kinds filtering of existing files).</summary>
        public static string KindOfPath(string rel)
        {
            var p = rel.Split('/');
            string root = p[0], sub = p.Length > 1 ? p[1] : "";
            if (root == "lang") return "lang";
            if (root == "rp")
            {
                switch (sub)
                {
                    case "textures": case "terrain_texture.d": case "item_texture.d": case "flipbook_textures.d": return "textures";
                    case "models": return p.Length > 2 && p[2] == "blocks" ? "models" : "skeleton";
                    case "blocks.d": return "blockstates";
                    case "texts.d": return "lang";
                    case "particles": return "particles";
                    default: return "skeleton";
                }
            }
            if (root == "bp")
            {
                switch (sub)
                {
                    case "blocks": return "blockstates";
                    case "recipes": return "recipes";
                    case "item_catalog.d": return "catalog";
                    default: return "skeleton";
                }
            }
            if (root == "data")
            {
                if (sub == "gf")
                {
                    string k = p.Length > 2 ? p[2] : "";
                    switch (k)
                    {
                        case "catalog": return "catalog";
                        case "families": return "families";
                        case "sound_events": return "sounds";
                        case "particles": return "particles";
                        default: return "skeleton";
                    }
                }
                string reg = p.Length > 2 ? p[2] : "";
                switch (reg)
                {
                    case "tags": return "tags";
                    case "loot_table": case "loot_modifiers": return "loot";
                    case "recipe": return "recipes";
                    case "advancement": return "advancements";
                    case "worldgen": case "dimension": case "dimension_type": return "worldgen";
                    case "data_maps": return "datamaps";
                    default: return "registries";
                }
            }
            return "skeleton";
        }

        public string GenRoot { get { return Path.Combine(Root, "assets", "gen", "import"); } }

        /// <summary>Existing generated files (unit -> rel), skipping the F0 README.</summary>
        public SortedDictionary<string, SortedSet<string>> ExistingFiles()
        {
            var res = new SortedDictionary<string, SortedSet<string>>(StringComparer.Ordinal);
            if (!Directory.Exists(GenRoot)) return res;
            foreach (var d in Directory.GetDirectories(GenRoot).OrderBy(x => x, StringComparer.Ordinal))
            {
                var set = new SortedSet<string>(Files(d), StringComparer.Ordinal);
                res[Path.GetFileName(d)] = set;
            }
            return res;
        }

        /// <summary>Writes (or, with check, compares) the selected part of the tree. Returns the number of differences.</summary>
        public int Sync(HashSet<string> kinds, string onlyUnit, bool check)
        {
            Func<string, string, bool> selected = (unit, rel) =>
                (onlyUnit == null || unit == onlyUnit) && kinds.Contains(KindOfPath(rel));
            int diffs = 0, written = 0, removed = 0, same = 0;
            var existing = ExistingFiles();
            var enc = new UTF8Encoding(false);
            foreach (var unit in Out.Keys)
            {
                foreach (var f in Out[unit].Values)
                {
                    if (!selected(unit, f.Rel)) continue;
                    string full = Path.Combine(GenRoot, unit, f.Rel.Replace('/', Path.DirectorySeparatorChar));
                    bool exists = File.Exists(full);
                    if (exists && File.ReadAllBytes(full).SequenceEqual(f.Bytes)) { same++; continue; }
                    if (check)
                    {
                        diffs++;
                        if (diffs <= 40) Log.Add((exists ? "differs: " : "missing: ") + "assets/gen/import/" + unit + "/" + f.Rel);
                        continue;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(full));
                    File.WriteAllBytes(full, f.Bytes);
                    written++;
                }
            }
            foreach (var unit in existing.Keys)
            {
                foreach (var rel in existing[unit])
                {
                    if (!selected(unit, rel)) continue;
                    SortedDictionary<string, GenFile> files;
                    if (Out.TryGetValue(unit, out files) && files.ContainsKey(rel)) continue;
                    if (unit == "README.md") continue;
                    if (check)
                    {
                        diffs++;
                        if (diffs <= 40) Log.Add("stale: assets/gen/import/" + unit + "/" + rel);
                        continue;
                    }
                    File.Delete(Path.Combine(GenRoot, unit, rel.Replace('/', Path.DirectorySeparatorChar)));
                    removed++;
                }
            }
            // docs (docs/spec/blockstates.md, ...) when their kind is selected for every unit
            foreach (var kv in Docs)
            {
                string full = Path.Combine(Root, kv.Key.Replace('/', Path.DirectorySeparatorChar));
                var bytes = enc.GetBytes(kv.Value);
                bool exists = File.Exists(full);
                if (exists && File.ReadAllBytes(full).SequenceEqual(bytes)) { same++; continue; }
                if (check) { diffs++; Log.Add((exists ? "differs: " : "missing: ") + kv.Key); continue; }
                Directory.CreateDirectory(Path.GetDirectoryName(full));
                File.WriteAllBytes(full, bytes);
                written++;
            }
            if (!check)
            {
                // remove directories left empty
                if (Directory.Exists(GenRoot))
                    foreach (var d in Directory.GetDirectories(GenRoot, "*", SearchOption.AllDirectories).OrderByDescending(x => x.Length))
                        if (!Directory.EnumerateFileSystemEntries(d).Any()) Directory.Delete(d);
                Log.Add(string.Format("{0} written, {1} removed, {2} unchanged", written, removed, same));
            }
            else Log.Add(string.Format("{0} identical, {1} different", same, diffs));
            return diffs;
        }
    }
}
