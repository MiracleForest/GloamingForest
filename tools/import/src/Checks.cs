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
                    // a full state: the variant's values, the default for the others
                    var state = bi.States.ToDictionary(s => s.Java, s => s.Values[0], StringComparer.Ordinal);
                    foreach (var kv in kvs) state[kv.Key] = kv.Value;
                    var rot = Rotation(doc["components"]);
                    if (doc["permutations"] != null)
                        foreach (var p in doc["permutations"].A)
                            if (Holds(p["condition"].AsString(), bi, state))
                            {
                                var r = Rotation(p["components"]);
                                if (r != null || p["components"]["minecraft:transformation"] != null) rot = r;
                            }
                    string want = ((360 - x) % 360) + "," + ((360 - y) % 360) + ",0";
                    string have = rot ?? "0,0,0";
                    if (have != want) Errors.Add("bp/blocks/" + bi.Id + ".json: variant '" + key + "' has rotation [" + have + "], TF's blockstate gives [" + want + "]");
                    checkedRotations++;
                }
            }
            Log.Add(checkedRotations + " variant rotations checked against TF's blockstates; block states checked against tools/import/expected-states.json");
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
            if (!state.TryGetValue(m.Groups[2].Value, out v)) return false;
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
