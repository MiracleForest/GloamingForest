// GloamingForest import tool (contract "import", unit F1): state-dependent block light. TF computes a
// block's light from its state (BlockBehaviour.Properties.lightLevel lambdas in init/TFBlocks.java or in a
// block class's constructor, and getLightEmission overrides). These are small expressions over the block's
// own properties (ternaries, ==, !=, *, switch, the brazier's BrazierLight.getLight(), the candelabra's
// candle count), so the import evaluates them for every combination of the properties they read and emits
// the result as minecraft:light_emission per permutation. Facts only; no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class LightFn
    {
        public string Source;                                  // where the expression was found
        public string Expr;                                    // the expression (or the switch statement)
        public string ContextClass;                            // the TF class whose constants it names
        public List<JProp> Reads = new List<JProp>();          // the properties it reads
        public Func<Dictionary<string, string>, int> Eval;     // property name -> value => light
    }

    public static class Light
    {
        /// <summary>The state-dependent light of a block (null when it is a constant or unknown).</summary>
        public static LightFn Of(Ctx ctx, BlockInfo bi, Action<string> warn)
        {
            // 1. a getLightEmission override along the class chain (the leaf-most one that decides by itself)
            foreach (var cls in bi.Chain)
            {
                string text = Java.ClassText(ctx, cls);
                if (text == null) break;
                var m = Regex.Match(text, @"int\s+getLightEmission\s*\([^)]*\)\s*\{");
                if (m.Success)
                {
                    string body = Body(text, m.Index + m.Length - 1);
                    if (body.Contains("getAuxLightManager")) continue;   // MasonJarBlock: from its block entity, else super
                    return Compile(ctx, bi, cls, "switch", body, cls + ".getLightEmission", warn);
                }
            }
            // 2. a lightLevel lambda in a TF class constructor (SpecialMagicLogBlock), applied after the registration's
            foreach (var cls in bi.Chain)
            {
                string text = Java.ClassText(ctx, cls);
                if (text == null) break;
                string expr = LambdaOf(text);
                if (expr != null) return Compile(ctx, bi, cls, "expr", expr, cls + " constructor", warn);
            }
            // 3. the registration's lightLevel lambda (init/TFBlocks.java)
            if (bi.J.LightExpr != null) return Compile(ctx, bi, bi.Chain[0], "expr", bi.J.LightExpr, "TFBlocks." + bi.J.Const, warn);
            return null;
        }

        /// <summary>The body of a ".lightLevel(x -> EXPR)" lambda in a text, with the parameter renamed to state.</summary>
        public static string LambdaOf(string text)
        {
            var m = Regex.Match(text, @"\.lightLevel\(\s*\(?\s*(\w+)\s*\)?\s*->\s*");
            if (!m.Success) return null;
            int i = m.Index + m.Length, depth = 0, start = i;
            for (; i < text.Length; i++)
            {
                char c = text[i];
                if (c == '(') depth++;
                else if (c == ')') { if (depth == 0) break; depth--; }
            }
            string expr = text.Substring(start, i - start).Trim();
            return Regex.Replace(expr, @"\b" + Regex.Escape(m.Groups[1].Value) + @"\.getValue\(", "state.getValue(");
        }

        static string Body(string text, int open)
        {
            int depth = 0;
            for (int i = open; i < text.Length; i++)
            {
                if (text[i] == '{') depth++;
                else if (text[i] == '}' && --depth == 0) return text.Substring(open + 1, i - open - 1);
            }
            return "";
        }

        // ---- a tiny evaluator ------------------------------------------------------------------------------

        sealed class Env
        {
            public Ctx Ctx;
            public string Cls;
            public Dictionary<string, JProp> Props = new Dictionary<string, JProp>(StringComparer.Ordinal);   // expression -> property
            public Dictionary<string, List<JProp>> Lists = new Dictionary<string, List<JProp>>(StringComparer.Ordinal);
            public Action<string> Warn;
        }

        /// <summary>A value: an int, a bool, or an enum value (serialized name) of a known property.</summary>
        sealed class V
        {
            public int I; public bool B; public string S; public JProp Of; public char K;   // K: i, b, s (enum), c (an enum constant name)
            public static V Int(int i) { return new V { K = 'i', I = i }; }
            public static V Bool(bool b) { return new V { K = 'b', B = b }; }
        }

        static LightFn Compile(Ctx ctx, BlockInfo bi, string cls, string kind, string code, string source, Action<string> warn)
        {
            var env = new Env { Ctx = ctx, Cls = cls, Warn = warn };
            var fn = new LightFn { Source = source, Expr = code.Trim(), ContextClass = cls };
            // the properties it reads: getValue(X) and the candle list of getCandleCount
            foreach (Match m in Regex.Matches(code, @"getValue\(\s*([\w.]+)\s*\)"))
            {
                var p = Prop(env, bi, m.Groups[1].Value);
                if (p == null) { warn("block " + bi.Id + ": light expression of " + source + " reads unknown property " + m.Groups[1].Value); return null; }
                env.Props[m.Groups[1].Value] = p;
                if (!fn.Reads.Contains(p)) fn.Reads.Add(p);
            }
            if (code.Contains("getCandleCount("))
            {
                var list = JavaStates.Resolve(ctx, cls, "CANDLES", warn).Select(p => bi.JavaProps.FirstOrDefault(x => x.Name == p.Name)).Where(p => p != null).ToList();
                if (list.Count == 0) { warn("block " + bi.Id + ": getCandleCount without a CANDLES list"); return null; }
                env.Lists["CANDLES"] = list;
                foreach (var p in list) if (!fn.Reads.Contains(p)) fn.Reads.Add(p);
            }
            Func<Dictionary<string, string>, V> eval;
            if (kind == "switch")
            {
                // [int candleCount = getCandleCount(state);] return switch (state.getValue(P)) { case A, B -> E; default -> E; };
                var sm = Regex.Match(code, @"return\s+switch\s*\(\s*state\.getValue\(\s*([\w.]+)\s*\)\s*\)\s*\{(.*)\}\s*;", RegexOptions.Singleline);
                if (!sm.Success) { warn("block " + bi.Id + ": unsupported getLightEmission in " + source); return null; }
                var swProp = env.Props[sm.Groups[1].Value];
                var arms = new List<Tuple<List<string>, List<string>>>();   // case constants (null: default), tokens
                foreach (Match am in Regex.Matches(sm.Groups[2].Value, @"(case\s+([\w\s,]+?)|default)\s*->\s*([^;]+);"))
                    arms.Add(Tuple.Create(am.Groups[2].Success ? am.Groups[2].Value.Split(',').Select(x => x.Trim()).ToList() : null, Tokenize(am.Groups[3].Value.Trim())));
                bool counts = Regex.IsMatch(code, @"int\s+candleCount\s*=\s*getCandleCount\(\s*state\s*\)\s*;");
                eval = st =>
                {
                    string v = st[swProp.Name];
                    foreach (var arm in arms)
                        if (arm.Item1 == null || arm.Item1.Any(c => ConstValue(swProp, c) == v))
                        {
                            var locals = new Dictionary<string, V>(StringComparer.Ordinal);
                            if (counts) locals["candleCount"] = V.Int(env.Lists["CANDLES"].Count(p => st[p.Name] == "true"));
                            return new Parser(env, arm.Item2, st, locals).Parse();
                        }
                    return V.Int(0);
                };
            }
            else
            {
                var tokens = Tokenize(code);   // once per expression, not per evaluated state
                eval = st => new Parser(env, tokens, st, new Dictionary<string, V>(StringComparer.Ordinal)).Parse();
            }
            // check it once on the default state
            try
            {
                var def = bi.States.ToDictionary(s => s.Java, s => s.Values[0], StringComparer.Ordinal);
                foreach (var p in fn.Reads) if (!def.ContainsKey(p.Name)) { warn("block " + bi.Id + ": light reads " + p.Name + ", which is not a Bedrock state"); return null; }
                var r = eval(def);
                if (r.K != 'i') { warn("block " + bi.Id + ": light expression of " + source + " is not an integer"); return null; }
            }
            catch (Exception e) { warn("block " + bi.Id + ": cannot evaluate the light expression of " + source + " (" + e.Message + ")"); return null; }
            fn.Eval = st => Math.Max(0, Math.Min(15, eval(st).I));
            return fn;
        }

        /// <summary>The tokens of an expression (the parser only reads them).</summary>
        static List<string> Tokenize(string code)
        {
            return Regex.Matches(code, @"\d+|\.[A-Za-z_]\w*|[A-Za-z_][\w.]*|==|!=|[?:*+\-()]").Cast<Match>().Select(m => m.Value).ToList();
        }

        static JProp Prop(Env env, BlockInfo bi, string expr)
        {
            var r = JavaStates.Resolve(env.Ctx, env.Cls, expr, env.Warn);
            if (r.Count != 1) return null;
            return bi.JavaProps.FirstOrDefault(p => p.Name == r[0].Name);
        }

        static string ConstValue(JProp p, string constExpr)
        {
            string c = constExpr.Split('.').Last(), v;
            return p.Consts.TryGetValue(c, out v) ? v : c.ToLowerInvariant();
        }

        /// <summary>Recursive descent over: ternary, == / !=, *, +, -, literals, state.getValue(P),
        /// .getLight() of an enum value (the enum constant's light argument), locals, parentheses.</summary>
        sealed class Parser
        {
            readonly Env env;
            readonly List<string> t;
            readonly Dictionary<string, string> st;
            readonly Dictionary<string, V> locals;
            int i;

            public Parser(Env env, List<string> tokens, Dictionary<string, string> st, Dictionary<string, V> locals)
            {
                this.env = env; this.st = st; this.locals = locals;
                t = tokens;
            }

            string Peek { get { return i < t.Count ? t[i] : null; } }
            void Expect(string s) { if (Peek != s) throw new Exception("expected '" + s + "' at '" + (Peek ?? "end") + "'"); i++; }

            public V Parse() { var v = Ternary(); if (i != t.Count) throw new Exception("unexpected '" + Peek + "'"); return v; }

            V Ternary()
            {
                var c = Equality();
                if (Peek != "?") return c;
                i++;
                var a = Ternary();
                Expect(":");
                var b = Ternary();
                if (c.K != 'b') throw new Exception("condition is not a boolean");
                return c.B ? a : b;
            }

            V Equality()
            {
                var a = Sum();
                if (Peek != "==" && Peek != "!=") return a;
                bool eq = t[i++] == "==";
                var b = Sum();
                bool same;
                if (a.K == 's' && b.K == 'c') same = a.S == ConstValue(a.Of, b.S);
                else if (a.K == 'c' && b.K == 's') same = b.S == ConstValue(b.Of, a.S);
                else if (a.K == 'i' && b.K == 'i') same = a.I == b.I;
                else if (a.K == 'b' && b.K == 'b') same = a.B == b.B;
                else throw new Exception("cannot compare");
                return V.Bool(eq ? same : !same);
            }

            V Sum()
            {
                var a = Product();
                while (Peek == "+" || Peek == "-")
                {
                    bool plus = t[i++] == "+";
                    var b = Product();
                    a = V.Int(plus ? a.I + b.I : a.I - b.I);
                }
                return a;
            }

            V Product()
            {
                var a = Primary();
                while (Peek == "*") { i++; var b = Primary(); a = V.Int(a.I * b.I); }
                return a;
            }

            V Primary()
            {
                string s = Peek;
                if (s == null) throw new Exception("unexpected end");
                i++;
                if (s == "(") { var v = Ternary(); Expect(")"); return v; }
                if (char.IsDigit(s[0])) return V.Int(int.Parse(s, CultureInfo.InvariantCulture));
                if (s == "true" || s == "false") return V.Bool(s == "true");
                V local;
                if (locals.TryGetValue(s, out local)) return local;
                if (s == "state.getValue")
                {
                    Expect("(");
                    string p = t[i++];
                    Expect(")");
                    var prop = env.Props[p];
                    string val = st[prop.Name];
                    V v = prop.Type == "bool" ? V.Bool(val == "true") : prop.Type == "int" ? V.Int(int.Parse(val, CultureInfo.InvariantCulture)) : new V { K = 's', S = val, Of = prop };
                    // .getLight() of an enum value: the enum constant's "light" constructor argument
                    if (Peek == ".getLight") { i++; Expect("("); Expect(")"); return V.Int(EnumLight(prop, val)); }
                    return v;
                }
                // a qualified enum constant (FireJetVariant.FLAME)
                if (Regex.IsMatch(s, @"^[A-Z][\w]*(\.[A-Z][\w]*)*\.[A-Z][A-Z0-9_]*$")) return new V { K = 'c', S = s };
                throw new Exception("unsupported token '" + s + "'");
            }

            int EnumLight(JProp prop, string val)
            {
                // the enum of the property: find its constant with this serialized value and the light argument
                var lights = JavaStates.EnumArgs(env.Ctx, env.Cls, prop, "light", env.Warn);
                int l;
                if (lights != null && lights.TryGetValue(val, out l)) return l;
                throw new Exception("no light value for " + prop.Name + "=" + val);
            }
        }
    }
}
