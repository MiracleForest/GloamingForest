// GloamingForest import tool (contract "import", unit F1): consistency checks that do not trust the
// generator. The block states are compared with a hand-written golden (tools/import/expected-states.json,
// read by hand from TF's createBlockStateDefinition overrides), both as parsed from the Java code and as
// written into the generated BP block files (after the overrides); the rotation of every variant permutation
// is compared with TF's blockstate file. A failed check fails the run, -Check included.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace GfImport
{
    public sealed partial class Ctx
    {
        public readonly List<string> Errors = new List<string>();

        J ExpectedStates()
        {
            string f = Path.Combine(Root, "tools", "import", "expected-states.json");
            if (!File.Exists(f)) { Errors.Add("tools/import/expected-states.json is missing"); return null; }
            return ReadJson(f)["classes"];
        }

        /// <summary>The Java properties parsed from TF's classes against the hand-written golden.</summary>
        public void CheckJavaStates()
        {
            var exp = ExpectedStates();
            if (exp == null) return;
            var usedClasses = new HashSet<string>(StringComparer.Ordinal);
            foreach (var bi in BlockInfos.Values)
            {
                string cls = bi.Chain[0];
                usedClasses.Add(cls);
                var e = exp[cls];
                if (e == null) { Errors.Add("block " + bi.Id + ": class " + cls + " has no entry in tools/import/expected-states.json"); continue; }
                var want = e.A.Select(x => x.AsString()).OrderBy(x => x, StringComparer.Ordinal).ToList();
                var got = bi.JavaProps.Select(p => p.Name).OrderBy(x => x, StringComparer.Ordinal).ToList();
                if (!want.SequenceEqual(got))
                    Errors.Add("block " + bi.Id + " (" + cls + "): Java properties [" + string.Join(", ", got) + "], expected [" + string.Join(", ", want) + "] (tools/import/expected-states.json)");
            }
            foreach (var k in exp.Keys)
                if (!usedClasses.Contains(k)) Warn("tools/import/expected-states.json: class " + k + " is not used by any block");
        }

        /// <summary>The states of the generated BP block files (after the overrides) against the golden, and the
        /// rotation of every variant permutation against TF's blockstate file.</summary>
        public void CheckEmittedBlocks()
        {
            var exp = ExpectedStates();
            if (exp == null) return;
            string bsDir = Path.Combine(GenRes, "assets", "twilightforest", "blockstates");
            int checkedRotations = 0, checkedFamilyTags = 0;
            // the families data entries (after the overrides), by block id (duplicates are errors there)
            var familyEntries = FamilyEntries();
            foreach (var bi in BlockInfos.Values)
            {
                SortedDictionary<string, GenFile> files;
                GenFile f;
                if (!Out.TryGetValue(NameOf(bi.Owner), out files) || !files.TryGetValue("bp/blocks/" + bi.Id + ".json", out f))
                {
                    Errors.Add("block " + bi.Id + ": no generated bp/blocks/" + bi.Id + ".json");
                    continue;
                }
                var doc = J.Parse(Encoding.UTF8.GetString(f.Bytes))["minecraft:block"];
                // a family block's params.tags (the kit's format-free input) are the tags of its skeleton file
                J fam = null;
                if (bi.Family != null && !familyEntries.TryGetValue("twilightforest:" + bi.Id, out fam))
                    Errors.Add("data/gf/families: no entry for the family block twilightforest:" + bi.Id + " (" + bi.Family + ")");
                else if (fam != null && !familyTagOverrides.Contains(bi.Id))
                {
                    var pt = fam["params"] != null ? fam["params"]["tags"] : null;
                    // compared as sets (a duplicate is reported by SyncFamilyTags on its own)
                    var want = BlockTagsIn(doc["components"]).Distinct(StringComparer.Ordinal).OrderBy(x => x, StringComparer.Ordinal).ToList();
                    var have = (pt != null && pt.IsArr ? pt.A.Select(x => x.AsString()) : Enumerable.Empty<string>()).Distinct(StringComparer.Ordinal).OrderBy(x => x, StringComparer.Ordinal).ToList();
                    if (!want.SequenceEqual(have))
                        Errors.Add("data/gf/families: twilightforest:" + bi.Id + " params.tags [" + string.Join(", ", have) + "], its bp/blocks file has [" + string.Join(", ", want) + "] (the skeleton follows params.tags, SyncFamilyTags)");
                    checkedFamilyTags++;
                }
                var states = doc["description"]["states"];
                var got = (states != null ? states.Keys : Enumerable.Empty<string>()).OrderBy(x => x, StringComparer.Ordinal).ToList();
                var e = exp[bi.Chain[0]];
                if (e != null)
                {
                    var want = e.A.Select(x => x.AsString()).Where(x => !DroppedStates.Contains(x)).Select(x => "twilightforest:" + x).OrderBy(x => x, StringComparer.Ordinal).ToList();
                    // a unit's override may add states (rule R1: add only), never drop one
                    var missing = want.Except(got).ToList();
                    if (missing.Count > 0) Errors.Add("bp/blocks/" + bi.Id + ".json lacks the state(s) " + string.Join(", ", missing));
                }
                // every variant's rotation, evaluated as Bedrock does: the root components, then every
                // permutation whose condition holds (in order) replaces the components it names
                var bsFile = Path.Combine(bsDir, bi.Id + ".json");
                if (!File.Exists(bsFile)) continue;
                var bs = ReadJson(bsFile);
                if (bs["variants"] == null) continue;
                foreach (var key in bs["variants"].Keys)
                {
                    var kvs = ParseVariantKey(key).Where(kv => !DroppedStates.Contains(kv.Key)).ToList();
                    var vj = bs["variants"][key];
                    if (vj.IsArr) vj = vj.A[0];
                    int x = vj["x"] != null ? (int)vj["x"].AsDouble() : 0, y = vj["y"] != null ? (int)vj["y"].AsDouble() : 0;
                    // a full state over the states the file declares (after the overrides): the variant's values, the
                    // declared default (the first listed value) for the others
                    var state = new Dictionary<string, string>(StringComparer.Ordinal);
                    if (states != null)
                        foreach (var sk in states.Keys)
                            if (sk.StartsWith("twilightforest:", StringComparison.Ordinal) && states[sk].IsArr && states[sk].A.Count > 0)
                                state[sk.Substring("twilightforest:".Length)] = StateText(states[sk].A[0]);
                    foreach (var kv in kvs) state[kv.Key] = kv.Value;
                    var rot = Rotation(doc["components"]);
                    try
                    {
                        if (doc["permutations"] != null)
                            foreach (var p in doc["permutations"].A)
                                if (Holds(p["condition"].AsString(), bi, state))
                                {
                                    var r = Rotation(p["components"]);
                                    if (r != null || p["components"]["minecraft:transformation"] != null) rot = r;
                                }
                    }
                    catch (Exception ex)
                    {
                        Errors.Add("bp/blocks/" + bi.Id + ".json: " + ex.Message);
                        break;
                    }
                    string want = ((360 - x) % 360) + "," + ((360 - y) % 360) + ",0";
                    string have = rot ?? "0,0,0";
                    if (have != want) Errors.Add("bp/blocks/" + bi.Id + ".json: variant '" + key + "' has rotation [" + have + "], TF's blockstate gives [" + want + "]");
                    checkedRotations++;
                }
            }
            Log.Add(checkedRotations + " variant rotations checked against TF's blockstates; block states checked against tools/import/expected-states.json; "
                + checkedFamilyTags + " family blocks' params.tags checked against their skeleton files");
        }

        /// <summary>The behaviour values of the generated BP block files (after the overrides) and their RP sounds against
        /// the hand-written golden tools/import/expected-behaviour.json: hardness, explosion resistance and map colour (per
        /// state where TF makes them state-dependent) and sound.</summary>
        J expectedBehaviour;
        bool expectedBehaviourRead;

        /// <summary>tools/import/expected-behaviour.json, parsed once for CheckBehaviour and CheckSoundTypes; null (one
        /// error) when it is missing or not a JSON object.</summary>
        J ExpectedBehaviour()
        {
            if (expectedBehaviourRead) return expectedBehaviour;
            expectedBehaviourRead = true;
            string gf = Path.Combine(Root, "tools", "import", "expected-behaviour.json");
            if (!File.Exists(gf)) { Errors.Add("tools/import/expected-behaviour.json is missing"); return null; }
            var doc = ReadJson(gf);
            if (doc == null || !doc.IsObj) { Errors.Add("tools/import/expected-behaviour.json is not a JSON object"); return null; }
            return expectedBehaviour = doc;
        }

        public void CheckBehaviour()
        {
            var goldenDoc = ExpectedBehaviour();
            if (goldenDoc == null) return;
            var golden = goldenDoc["blocks"];
            if (golden == null || !golden.IsObj) { Errors.Add("tools/import/expected-behaviour.json has no blocks object"); return; }
            var soundTypes = goldenDoc["soundTypes"];
            if (soundTypes != null && !soundTypes.IsObj) soundTypes = null; // reported by CheckSoundTypes
            int checkedValues = 0;
            foreach (var id in golden.Keys)
            {
                var e = golden[id];
                BlockInfo bi;
                SortedDictionary<string, GenFile> files;
                GenFile f;
                if (!BlockInfos.TryGetValue(id, out bi) || !Out.TryGetValue(NameOf(bi.Owner), out files) || !files.TryGetValue("bp/blocks/" + id + ".json", out f))
                {
                    Errors.Add("tools/import/expected-behaviour.json: " + id + " has no generated bp/blocks file");
                    continue;
                }
                var doc = J.Parse(Encoding.UTF8.GetString(f.Bytes))["minecraft:block"];
                var declared = doc["description"]["states"];
                var defaults = new Dictionary<string, string>(StringComparer.Ordinal);
                if (declared != null)
                    foreach (var sk in declared.Keys)
                        if (sk.StartsWith("twilightforest:", StringComparison.Ordinal) && declared[sk].IsArr && declared[sk].A.Count > 0)
                            defaults[sk.Substring("twilightforest:".Length)] = StateText(declared[sk].A[0]);
                var cases = e["cases"] != null ? e["cases"].A.ToList() : new List<J> { J.Obj() };
                foreach (var c in cases)
                {
                    var state = new Dictionary<string, string>(defaults, StringComparer.Ordinal);
                    if (c["state"] != null)
                        foreach (var k in c["state"].Keys)
                        {
                            if (!defaults.ContainsKey(k)) Errors.Add("tools/import/expected-behaviour.json: " + id + " has no state twilightforest:" + k);
                            state[k] = StateText(c["state"][k]);
                        }
                    string where = "bp/blocks/" + id + ".json" + (c["state"] != null ? " " + c["state"].ToCompact() : "");
                    J mining, explosion, mapColor;
                    try
                    {
                        mining = Effective(doc, bi, state, "minecraft:destructible_by_mining");
                        explosion = Effective(doc, bi, state, "minecraft:destructible_by_explosion");
                        mapColor = Effective(doc, bi, state, "minecraft:map_color");
                    }
                    catch (Exception ex) { Errors.Add(where + ": " + ex.Message); continue; }
                    var h = c["hardness"] ?? e["hardness"];
                    if (h != null)
                    {
                        string want = h.IsStr ? "false" : J.Obj().Set("seconds_to_destroy", Math.Round(h.AsDouble(), 4)).ToCompact();
                        string have = mining != null ? mining.ToCompact() : "(none)";
                        if (want != have) Errors.Add(where + ": minecraft:destructible_by_mining " + have + ", TF gives " + want + " (" + e["source"].AsString() + ")");
                        checkedValues++;
                    }
                    var r = c["resistance"] ?? e["resistance"];
                    if (r != null)
                    {
                        // the golden is in Java units; the JSON value is Java's x ExplosionJsonPerJava (Blocks.ExplosionComponent)
                        string want = r.IsStr ? "false" : J.Obj().Set("explosion_resistance", Math.Round(r.AsDouble() * ExplosionJsonPerJava, 4)).ToCompact();
                        string have = explosion != null ? explosion.ToCompact() : "(none)";
                        if (want != have) Errors.Add(where + ": minecraft:destructible_by_explosion " + have + ", TF gives " + want + " (" + e["source"].AsString() + ")");
                        checkedValues++;
                    }
                    var mc = c["mapColor"] ?? e["mapColor"];
                    if (mc != null)
                    {
                        string have = mapColor != null ? mapColor.AsString() : null, want = null;
                        if (mc.Kind != JKind.Null && !MapColors.TryGetValue(mc.AsString(), out want)) want = "?" + mc.AsString();
                        if (have != want) Errors.Add(where + ": minecraft:map_color " + (have ?? "(none)") + ", TF gives " + (mc.Kind == JKind.Null ? "none" : mc.AsString()) + " (" + e["source"].AsString() + ")");
                        checkedValues++;
                    }
                }
                // the tools that mine it (Bedrock tool tags), the tier a tool needs (Bedrock tier tag) and the drop rule
                if (e["tools"] != null)
                {
                    var tags = BlockTagsIn(doc["components"]).Distinct(StringComparer.Ordinal).ToList(); // a set (duplicates: SyncFamilyTags / FinalHarvest)
                    var wantTools = e["tools"].A.Select(t => BedrockToolTag[t.AsString()]).OrderBy(x => x, StringComparer.Ordinal).ToList();
                    var haveTools = tags.Where(k => BedrockToolTag.Values.Contains(k)).OrderBy(x => x, StringComparer.Ordinal).ToList();
                    if (!wantTools.SequenceEqual(haveTools))
                        Errors.Add("bp/blocks/" + id + ".json: tool tags [" + string.Join(", ", haveTools) + "], TF gives [" + string.Join(", ", wantTools) + "] (" + e["source"].AsString() + ")");
                    string wantTier = e["tier"] != null ? BedrockTierTag[e["tier"].AsString()] : "(none)";
                    var haveTiers = tags.Where(k => BedrockTierTag.Values.Contains(k)).ToList();
                    string haveTier = haveTiers.Count == 0 ? "(none)" : string.Join(", ", haveTiers);
                    if (wantTier != haveTier) Errors.Add("bp/blocks/" + id + ".json: tier tag " + haveTier + ", TF gives " + wantTier + " (" + e["source"].AsString() + ")");
                    checkedValues += 2;
                }
                if (e["requiresTool"] != null)
                {
                    Harvest hv;
                    bool have = Harvests.TryGetValue(id, out hv) && hv.RequiresTool;
                    bool want = e["requiresTool"].Kind == JKind.Bool && e["requiresTool"].B;
                    if (have != want)
                        Errors.Add("data/gf/import/harvest.json: twilightforest:" + id + " requiresTool " + have + ", TF gives " + want + " (" + e["source"].AsString() + ")");
                    checkedValues++;
                }
                // the RP sound (rp/blocks.d fragment of the owner) and the root map colour
                GenFile rp;
                string sound = null;
                if (files.TryGetValue("rp/blocks.d/import-" + NameOf(bi.Owner) + ".json", out rp))
                {
                    var entry = J.Parse(Encoding.UTF8.GetString(rp.Bytes))["twilightforest:" + id];
                    if (entry != null && entry["sound"] != null) sound = entry["sound"].AsString();
                }
                if (e["sound"] != null)
                {
                    // TF's own sound types by the golden's soundTypes (javaType -> the pack's type, written by hand), the
                    // vanilla ones by the engine type of Blocks.SoundMap
                    string javaSound = e["sound"].AsString(), want;
                    if (javaSound.StartsWith("TFSoundTypes.", StringComparison.Ordinal))
                    {
                        want = soundTypes != null ? soundTypes.Keys.FirstOrDefault(k => soundTypes[k]["javaType"] != null && soundTypes[k]["javaType"].AsString() == javaSound) : null;
                        if (want == null) Errors.Add("tools/import/expected-behaviour.json: " + id + " has sound " + javaSound + ", which no soundTypes entry names as its javaType");
                    }
                    else want = BedrockSound(javaSound);
                    if (want != null && sound != want) Errors.Add("rp/blocks.d: twilightforest:" + id + " sound " + (sound ?? "(none)") + ", TF gives " + javaSound + " -> " + want);
                    checkedValues++;
                }
            }
            Log.Add(checkedValues + " behaviour values of " + golden.Keys.Count() + " blocks checked against tools/import/expected-behaviour.json");
        }

        /// <summary>TF's own sound types as written to the rp/sounds.json.d fragments (after the overrides) against the
        /// hand-written soundTypes of tools/import/expected-behaviour.json, independent of the generator: every golden
        /// type is defined by exactly one fragment, with volume and pitch 1 and exactly the golden's events (the sound
        /// event, its volume and pitch, or "" for a silent one; "default" silent) in block_sounds and
        /// interactive_sounds.block_sounds; its blocks name it in their owner's rp/blocks.d fragment; a fragment defines
        /// no type the golden lacks and nothing but those two categories.</summary>
        public void CheckSoundTypes()
        {
            var goldenDoc = ExpectedBehaviour();
            if (goldenDoc == null) return;
            var golden = goldenDoc["soundTypes"];
            if (golden == null || !golden.IsObj) { Errors.Add("tools/import/expected-behaviour.json has no soundTypes object"); return; }
            // every type the fragments define: id -> [(fragment, block_sounds entry, interactive_sounds entry)]
            var defined = new SortedDictionary<string, List<Tuple<string, J, J>>>(StringComparer.Ordinal);
            foreach (var unit in Out)
                foreach (var gen in unit.Value)
                {
                    if (!gen.Key.StartsWith("rp/sounds.json.d/", StringComparison.Ordinal)) continue;
                    string where = unit.Key + "/" + gen.Key;
                    var doc = J.Parse(Encoding.UTF8.GetString(gen.Value.Bytes));
                    foreach (var k in doc.Keys)
                        if (k != "block_sounds" && k != "interactive_sounds") Errors.Add(where + ": unexpected category " + k + " (the import writes block_sounds and interactive_sounds.block_sounds)");
                    var bs = doc["block_sounds"];
                    var ib = doc["interactive_sounds"] != null ? doc["interactive_sounds"]["block_sounds"] : null;
                    if (doc["interactive_sounds"] != null)
                        foreach (var k in doc["interactive_sounds"].Keys)
                            if (k != "block_sounds") Errors.Add(where + ": unexpected category interactive_sounds." + k);
                    foreach (var id in (bs != null ? bs.Keys : Enumerable.Empty<string>()).Union(ib != null ? ib.Keys : Enumerable.Empty<string>()))
                    {
                        List<Tuple<string, J, J>> list;
                        if (!defined.TryGetValue(id, out list)) defined[id] = list = new List<Tuple<string, J, J>>();
                        list.Add(Tuple.Create(where, bs != null ? bs[id] : null, ib != null ? ib[id] : null));
                    }
                }
            // a javaType names one pack type: CheckBehaviour maps a block's TFSoundTypes.* sound to the entry that names it
            foreach (var grp in golden.Keys.Where(k => golden[k].IsObj && golden[k]["javaType"] != null && golden[k]["javaType"].IsStr)
                         .GroupBy(k => golden[k]["javaType"].S, StringComparer.Ordinal).Where(g => g.Count() > 1))
                Errors.Add("tools/import/expected-behaviour.json: soundTypes " + string.Join(", ", grp) + " share the javaType " + grp.Key + " (one entry per Java sound type)");
            int checkedEvents = 0;
            foreach (var id in golden.Keys)
            {
                var g = golden[id];
                // a hand-written entry: {javaType, blocks: [TF block paths], block_sounds, interactive_sounds}
                if (!g.IsObj) { Errors.Add("tools/import/expected-behaviour.json: soundTypes " + id + " is not an object: " + g.ToCompact()); continue; }
                var gBlocks = g["blocks"];
                if (gBlocks != null && !(gBlocks.IsArr && gBlocks.A.All(x => x.IsStr)))
                {
                    Errors.Add("tools/import/expected-behaviour.json: soundTypes " + id + " blocks is not an array of block paths: " + gBlocks.ToCompact());
                    gBlocks = null;
                }
                if (g["javaType"] != null && !g["javaType"].IsStr) Errors.Add("tools/import/expected-behaviour.json: soundTypes " + id + " javaType is not a string: " + g["javaType"].ToCompact());
                List<Tuple<string, J, J>> defs;
                if (!defined.TryGetValue(id, out defs)) { Errors.Add("sound type " + id + " (tools/import/expected-behaviour.json soundTypes) is defined by no rp/sounds.json.d fragment"); continue; }
                if (defs.Count > 1) Errors.Add("sound type " + id + " is defined by " + defs.Count + " fragments: " + string.Join(", ", defs.Select(d => d.Item1)));
                var def = defs[0];
                checkedEvents += CheckSoundEntry(def.Item1 + " block_sounds." + id, def.Item2, g["block_sounds"]);
                checkedEvents += CheckSoundEntry(def.Item1 + " interactive_sounds.block_sounds." + id, def.Item3, g["interactive_sounds"]);
                // the blocks of the type name it (their owner's rp/blocks.d fragment)
                foreach (var b in gBlocks != null ? gBlocks.A.Select(x => x.S) : Enumerable.Empty<string>())
                {
                    BlockInfo bi;
                    SortedDictionary<string, GenFile> files;
                    GenFile rp;
                    string sound = null;
                    if (BlockInfos.TryGetValue(b, out bi) && Out.TryGetValue(NameOf(bi.Owner), out files) && files.TryGetValue("rp/blocks.d/import-" + NameOf(bi.Owner) + ".json", out rp))
                    {
                        var entry = J.Parse(Encoding.UTF8.GetString(rp.Bytes))["twilightforest:" + b];
                        if (entry != null && entry["sound"] != null) sound = entry["sound"].AsString();
                    }
                    if (sound != id) Errors.Add("rp/blocks.d: twilightforest:" + b + " sound " + (sound ?? "(none)") + ", TF gives " + (g["javaType"] != null ? g["javaType"].AsString() : "?") + " -> " + id);
                }
            }
            foreach (var id in defined.Keys)
                if (golden[id] == null) Errors.Add("sound type " + id + " (" + string.Join(", ", defined[id].Select(d => d.Item1)) + ") has no entry in tools/import/expected-behaviour.json soundTypes");
            Log.Add(checkedEvents + " sound type events of " + golden.Keys.Count() + " TF sound type(s) checked against tools/import/expected-behaviour.json");
        }

        /// <summary>One sound type entry ({volume, pitch, events}) against the golden's events ({event: [sound, volume,
        /// pitch] or ""}). Returns the number of events checked.</summary>
        int CheckSoundEntry(string where, J entry, J want)
        {
            if (want == null || !want.IsObj) { Errors.Add("tools/import/expected-behaviour.json: no golden events for " + where); return 0; }
            if (entry == null || !entry.IsObj) { Errors.Add(where + " is missing"); return 0; }
            Func<J, double> num = v => v != null && v.IsNum ? v.AsDouble() : double.NaN;
            Func<double, double, bool> near = (a, b) => Math.Abs(a - b) <= 1e-6;
            if (!near(num(entry["volume"]), 1.0) || !near(num(entry["pitch"]), 1.0))
                Errors.Add(where + ": volume " + (entry["volume"] != null ? entry["volume"].ToCompact() : "(none)") + " / pitch " + (entry["pitch"] != null ? entry["pitch"].ToCompact() : "(none)") + ", expected 1 / 1 (the events carry Java's values)");
            var events = entry["events"];
            if (events == null || !events.IsObj) { Errors.Add(where + " has no events"); return 0; }
            var expectedKeys = new SortedSet<string>(want.Keys, StringComparer.Ordinal) { "default" };
            var haveKeys = new SortedSet<string>(events.Keys, StringComparer.Ordinal);
            if (!expectedKeys.SetEquals(haveKeys))
                Errors.Add(where + ": events [" + string.Join(", ", haveKeys) + "], expected [" + string.Join(", ", expectedKeys) + "]");
            if (events["default"] != null && !(events["default"].IsStr && events["default"].S == ""))
                Errors.Add(where + ": event default " + events["default"].ToCompact() + ", expected \"\" (silent)");
            int n = 0;
            foreach (var ev in want.Keys)
            {
                var w = want[ev];
                var h = events[ev];
                if (h == null) continue; // reported with the keys above
                n++;
                if (w.IsStr)
                {
                    if (!(h.IsStr && h.S == w.S)) Errors.Add(where + ": event " + ev + " " + h.ToCompact() + ", TF plays " + (w.S == "" ? "nothing (\"\")" : w.ToCompact()));
                    continue;
                }
                if (!w.IsArr || w.A.Count != 3 || !w.A[0].IsStr || !w.A[1].IsNum || !w.A[2].IsNum)
                {
                    Errors.Add("tools/import/expected-behaviour.json: soundTypes event " + ev + " of " + where + " is neither \"\" nor [sound, volume, pitch]");
                    continue;
                }
                string ws = w.A[0].AsString();
                double wv = w.A[1].AsDouble(), wp = w.A[2].AsDouble();
                bool ok = h.IsObj && h["sound"] != null && h["sound"].AsString() == ws && near(num(h["volume"]), wv) && near(num(h["pitch"]), wp)
                    && h.Keys.All(k => k == "sound" || k == "volume" || k == "pitch");
                if (!ok)
                    Errors.Add(where + ": event " + ev + " " + h.ToCompact() + ", TF plays " + J.Obj().Set("sound", ws).Set("volume", wv).Set("pitch", wp).ToCompact());
            }
            return n;
        }

        /// <summary>A component of a block file in a state, as Bedrock resolves it: the root's, replaced by every permutation
        /// whose condition holds (in order).</summary>
        static J Effective(J doc, BlockInfo bi, Dictionary<string, string> state, string component)
        {
            var v = doc["components"][component];
            if (doc["permutations"] != null)
                foreach (var p in doc["permutations"].A)
                    if (p["components"][component] != null && Holds(p["condition"].AsString(), bi, state)) v = p["components"][component];
            return v;
        }

        /// <summary>A declared state value as the conditions name it ("true", "3", "north").</summary>
        static string StateText(J v)
        {
            if (v.IsStr) return v.AsString();
            string s = v.ToCompact();
            return s.Trim('"');
        }

        static string Rotation(J comps)
        {
            var t = comps != null ? comps["minecraft:transformation"] : null;
            if (t == null || t["rotation"] == null) return null;
            return string.Join(",", t["rotation"].A.Select(v => ((long)v.AsDouble()).ToString(CultureInfo.InvariantCulture)));
        }

        /// <summary>Evaluates a permutation condition as the import writes them: "a && b", "(a) || (b)", where a
        /// term is q.block_state('s') == v, q.block_state('s') (true) or !q.block_state('s') (false).</summary>
        static bool Holds(string cond, BlockInfo bi, Dictionary<string, string> state)
        {
            cond = cond.Trim();
            if (cond == "1.0") return true;
            var ors = SplitTop(cond, "||");
            if (ors.Count > 1) return ors.Any(o => Holds(Unwrap(o), bi, state));
            var ands = SplitTop(cond, "&&");
            if (ands.Count > 1) return ands.All(a => Holds(Unwrap(a), bi, state));
            cond = Unwrap(cond);
            var m = System.Text.RegularExpressions.Regex.Match(cond, @"^(!?)q\.block_state\('twilightforest:([^']+)'\)(?:\s*==\s*'?([^']*)'?)?$");
            if (!m.Success) throw new Exception("cannot evaluate condition term '" + cond + "' of " + bi.Id);
            string v;
            // a condition on a state the file does not declare (an override renamed or dropped it) is an error, not a
            // permutation that never applies
            if (!state.TryGetValue(m.Groups[2].Value, out v))
                throw new Exception("a permutation condition names twilightforest:" + m.Groups[2].Value + ", which the block does not declare ('" + cond + "')");
            if (m.Groups[3].Success && m.Groups[3].Value.Length > 0) return v == m.Groups[3].Value;
            bool b = v == "true";
            return m.Groups[1].Value == "!" ? !b : b;
        }

        static string Unwrap(string s)
        {
            s = s.Trim();
            while (s.StartsWith("(", StringComparison.Ordinal) && s.EndsWith(")", StringComparison.Ordinal) && SplitTop(s.Substring(1, s.Length - 2), "||").Count >= 1 && Balanced(s.Substring(1, s.Length - 2)))
                s = s.Substring(1, s.Length - 2).Trim();
            return s;
        }

        static bool Balanced(string s)
        {
            int d = 0;
            foreach (char c in s) { if (c == '(') d++; else if (c == ')' && --d < 0) return false; }
            return d == 0;
        }

        static List<string> SplitTop(string s, string op)
        {
            var res = new List<string>();
            int d = 0, start = 0;
            for (int i = 0; i < s.Length; i++)
            {
                if (s[i] == '(') d++;
                else if (s[i] == ')') d--;
                else if (d == 0 && string.CompareOrdinal(s, i, op, 0, op.Length) == 0) { res.Add(s.Substring(start, i - start)); start = i + op.Length; i += op.Length - 1; }
            }
            res.Add(s.Substring(start));
            return res;
        }
    }
}
