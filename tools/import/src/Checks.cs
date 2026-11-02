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
            int checkedRotations = 0;
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
            Log.Add(checkedRotations + " variant rotations checked against TF's blockstates; block states checked against tools/import/expected-states.json");
        }

        /// <summary>The behaviour values of the generated BP block files (after the overrides) and their RP sounds against
        /// the hand-written golden tools/import/expected-behaviour.json: hardness, explosion resistance and map colour (per
        /// state where TF makes them state-dependent) and sound.</summary>
        public void CheckBehaviour()
        {
            string gf = Path.Combine(Root, "tools", "import", "expected-behaviour.json");
            if (!File.Exists(gf)) { Errors.Add("tools/import/expected-behaviour.json is missing"); return; }
            var golden = ReadJson(gf)["blocks"];
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
                    var tags = BlockTagsIn(doc["components"]);
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
                    string want = BedrockSound(e["sound"].AsString());
                    if (sound != want) Errors.Add("rp/blocks.d: twilightforest:" + id + " sound " + (sound ?? "(none)") + ", TF gives " + e["sound"].AsString() + " -> " + want);
                    checkedValues++;
                }
            }
            Log.Add(checkedValues + " behaviour values of " + golden.Keys.Count() + " blocks checked against tools/import/expected-behaviour.json");
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
