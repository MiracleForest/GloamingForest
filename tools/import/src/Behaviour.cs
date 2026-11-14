// GloamingForest import tool (contract "import", unit F1): state-dependent block behaviour values. A few upstream
// block classes override getExplosionResistance / getDestroyProgress with a value that depends only on the
// block's own state (VanishingBlock: an inactive block resists 6000; LockedVanishingBlock: a locked one
// 6000000; SkullChestBlock: Float.MAX_VALUE; TrophyPedestalBlock: unbreakable while inactive). The import
// evaluates them for every combination of the states they read, like the light (Light.cs), and emits
// minecraft:destructible_by_explosion / minecraft:destructible_by_mining per permutation. Overrides that
// depend on the player or the held item (shears on arctic fur and berry bushes, the aurora brick's
// advancement, the stronghold shield's side) are runtime behaviour of the blocks' owners; the import lists
// them in its log. Facts only; no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class StateValue
    {
        public string Source;                                    // the override(s) the value comes from
        public List<JProp> Reads = new List<JProp>();            // the properties it reads
        public Func<Dictionary<string, string>, double> Eval;    // property name -> value => the value
    }

    public static class Behaviour
    {
        /// <summary>An explosion resistance that stands for "never destroyed by an explosion" (Float.MAX_VALUE).</summary>
        public const double Indestructible = double.PositiveInfinity;
        /// <summary>A destroy progress of -1: the block cannot be mined in that state.</summary>
        public const double Unminable = -1;

        const string Term = @"super\.\w+\([^()]*\)|Float\.MAX_VALUE|-?[\d.]+[FfDd]?";
        static readonly Regex ReturnExpr = new Regex(
            @"^\s*return\s+(?:(!?)\s*state\.getValue\(\s*([\w.]+)\s*\)\s*\?\s*(" + Term + @")\s*:\s*(" + Term + @")|(" + Term + @"))\s*;\s*$",
            RegexOptions.Compiled | RegexOptions.Singleline);

        /// <summary>The block's explosion resistance by state (getExplosionResistance overrides along the class chain,
        /// super = the registration's explosionResistance), or null when no upstream class overrides it.</summary>
        public static StateValue Explosion(Ctx ctx, BlockInfo bi, Action<string> warn, ICollection<string> runtime)
        {
            return Of(ctx, bi, "getExplosionResistance", bi.J.Resistance, warn, runtime);
        }

        /// <summary>The block's mining value by state: the registration's hardness, or Unminable where a
        /// getDestroyProgress override returns -1 for the state; null when no upstream class overrides it by state.</summary>
        public static StateValue Mining(Ctx ctx, BlockInfo bi, Action<string> warn, ICollection<string> runtime)
        {
            return Of(ctx, bi, "getDestroyProgress", bi.J.Unbreakable ? Unminable : bi.J.Hardness, warn, runtime);
        }

        static StateValue Of(Ctx ctx, BlockInfo bi, string method, double registered, Action<string> warn, ICollection<string> runtime)
        {
            var res = new StateValue();
            var sources = new List<string>();
            var fn = Chain(ctx, bi, method, 0, registered, res, sources, warn, runtime);
            if (fn == null || sources.Count == 0) return null;
            res.Source = string.Join(" < ", sources);
            res.Eval = fn;
            // check it on the default state
            var def = bi.States.ToDictionary(s => s.Java, s => s.Values[0], StringComparer.Ordinal);
            foreach (var p in res.Reads)
                if (!def.ContainsKey(p.Name)) { warn("block " + bi.Id + ": " + res.Source + " reads " + p.Name + ", which is not a Bedrock state"); return null; }
            return res;
        }

        /// <summary>The value function of the override at bi.Chain[from] or above (null: a runtime override, skipped).</summary>
        static Func<Dictionary<string, string>, double> Chain(Ctx ctx, BlockInfo bi, string method, int from, double registered,
            StateValue res, List<string> sources, Action<string> warn, ICollection<string> runtime)
        {
            for (int i = from; i < bi.Chain.Count; i++)
            {
                string cls = bi.Chain[i];
                string text = Java.ClassText(ctx, cls);
                if (text == null) break;   // a vanilla class: Minecraft's own value
                var m = Regex.Match(text, @"float\s+" + method + @"\s*\([^)]*\)\s*\{");
                if (!m.Success) continue;
                string body = Light.Body(text, m.Index + m.Length - 1);
                var rm = ReturnExpr.Match(body);
                if (!rm.Success)
                {
                    // the player, the held item, advancements: decided at run time by the block's owner
                    runtime.Add(bi.Id + ": " + cls + "." + method);
                    return null;
                }
                // getDestroyProgress returns progress per tick, not a hardness: only -1 (cannot be mined in that state) and
                // super (the registration's hardness) are block values; another constant is the owner's runtime behaviour
                if (method == "getDestroyProgress")
                {
                    var consts = new[] { rm.Groups[3], rm.Groups[4], rm.Groups[5] }.Where(g => g.Success).Select(g => g.Value.Trim())
                        .Where(t => !t.StartsWith("super.", StringComparison.Ordinal) && !IsMinusOne(t)).ToList();
                    if (consts.Count > 0)
                    {
                        warn("block " + bi.Id + ": " + cls + ".getDestroyProgress returns the progress " + string.Join(", ", consts) + " per tick (not a hardness): left to the owner");
                        runtime.Add(bi.Id + ": " + cls + "." + method);
                        return null;
                    }
                }
                sources.Add(cls + "." + method);
                Func<string, Func<Dictionary<string, string>, double>> term = t =>
                {
                    t = t.Trim();
                    if (t.StartsWith("super.", StringComparison.Ordinal))
                    {
                        int before = runtime.Count;
                        var up = Chain(ctx, bi, method, i + 1, registered, res, sources, warn, runtime);
                        // a superclass that decides at run time: the registration's value stands in for it, said loudly
                        if (up == null && runtime.Count > before) warn("block " + bi.Id + ": " + cls + "." + method + " calls a runtime super override; the registration value is used for it");
                        return up ?? (st => registered);
                    }
                    if (t == "Float.MAX_VALUE") return st => Indestructible;
                    double v = double.Parse(t.TrimEnd('F', 'f', 'D', 'd'), CultureInfo.InvariantCulture);
                    return st => v;
                };
                if (rm.Groups[5].Success) return term(rm.Groups[5].Value);
                var r = JavaStates.Resolve(ctx, cls, rm.Groups[2].Value, warn);
                var prop = r.Count == 1 ? bi.JavaProps.FirstOrDefault(p => p.Name == r[0].Name) : null;
                if (prop == null || prop.Type != "bool") { warn("block " + bi.Id + ": " + cls + "." + method + " reads " + rm.Groups[2].Value + ", not a boolean property of the block"); return null; }
                if (!res.Reads.Contains(prop)) res.Reads.Add(prop);
                bool negate = rm.Groups[1].Value == "!";
                var a = term(rm.Groups[3].Value);
                var b = term(rm.Groups[4].Value);
                string name = prop.Name;
                return st => (st[name] == "true") != negate ? a(st) : b(st);
            }
            return null;
        }

        static bool IsMinusOne(string t)
        {
            double v;
            return double.TryParse(t.TrimEnd('F', 'f', 'D', 'd'), NumberStyles.Float, CultureInfo.InvariantCulture, out v) && v == -1;
        }
    }
}
