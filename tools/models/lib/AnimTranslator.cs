// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// setupAnim -> Molang for the model converter (contract "geo", unit F2): a first, mechanical pass
// over a TF model's setupAnim / prepareMobModel. Assignments of bone rotations and offsets whose
// right-hand side uses only the animation parameters, constants, locals and Mth / Math functions
// become a Bedrock base animation; everything else (entity state, loops, conditions on the entity)
// is listed with its Java source for the owning unit to translate by hand. Part of GloamingForest,
// LGPL-3.0-or-later.
//
// Mapping: limbSwing -> query.modified_distance_moved, limbSwingAmount -> query.modified_move_speed,
// ageInTicks -> query.life_time * 20, netHeadYaw -> query.target_y_rotation, headPitch ->
// query.target_x_rotation (the vanilla quadruped / humanoid animations use the same queries).
// Java angles are radians and absolute (they replace the PartPose rotation); Molang angles are
// degrees added to the geometry's rotation, so a rotation becomes value * 57.29578 - default.
// Molang's math.sin / math.cos take degrees.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace Gf.Models
{
    // A Molang expression tree with constant folding.
    public abstract class MNode
    {
        public abstract int Prec { get; } // 3 atom, 2 product, 1 sum, 0 conditional
        public abstract string Emit();
        public static string EmitIn(MNode n, int prec) { var s = n.Emit(); return n.Prec < prec ? "(" + s + ")" : s; }
    }

    public sealed class MNum : MNode
    {
        public double V;
        public MNum(double v) { V = v; }
        public override int Prec { get { return V < 0 ? 1 : 3; } }
        public override string Emit()
        {
            double r = Math.Round(V, 6);
            if (Math.Abs(r) >= 1) r = Math.Round(V, 4);
            if (r == 0) r = 0;
            return r.ToString("0.######", CultureInfo.InvariantCulture);
        }
    }

    public sealed class MAtom : MNode
    {
        public string Text;
        public MAtom(string t) { Text = t; }
        public override int Prec { get { return 3; } }
        public override string Emit() { return Text; }
    }

    public sealed class MCall : MNode
    {
        public string Fn; public List<MNode> Args;
        public override int Prec { get { return 3; } }
        public override string Emit() { return Fn + "(" + string.Join(", ", Args.Select(a => a.Emit())) + ")"; }
    }

    public sealed class MSum : MNode
    {
        public List<KeyValuePair<int, MNode>> Terms = new List<KeyValuePair<int, MNode>>(); // sign, term
        public override int Prec { get { return 1; } }
        public override string Emit()
        {
            var sb = new StringBuilder();
            for (int i = 0; i < Terms.Count; i++)
            {
                var t = Terms[i];
                string s = EmitIn(t.Value, 2);
                if (i == 0) sb.Append(t.Key < 0 ? "-" + EmitIn(t.Value, 3) : s);
                else sb.Append(t.Key < 0 ? " - " + EmitIn(t.Value, 2) : " + " + s);
            }
            return sb.ToString();
        }
    }

    public sealed class MProd : MNode
    {
        public List<KeyValuePair<bool, MNode>> Factors = new List<KeyValuePair<bool, MNode>>(); // divide?, factor
        public override int Prec { get { return 2; } }
        public override string Emit()
        {
            var sb = new StringBuilder();
            for (int i = 0; i < Factors.Count; i++)
            {
                var f = Factors[i];
                if (i > 0) sb.Append(f.Key ? " / " : " * ");
                else if (f.Key) sb.Append("1 / ");
                sb.Append(EmitIn(f.Value, 3));
            }
            return sb.ToString();
        }
    }

    public sealed class MCond : MNode
    {
        public MNode C, A, B;
        public override int Prec { get { return 0; } }
        public override string Emit() { return EmitIn(C, 1) + " ? " + EmitIn(A, 1) + " : " + EmitIn(B, 1); }
    }

    public sealed class MCmp : MNode
    {
        public string Op; public MNode L, R;
        public override int Prec { get { return 0; } }
        public override string Emit() { return EmitIn(L, 1) + " " + Op + " " + EmitIn(R, 1); }
    }

    public static class M
    {
        public static MNode Num(double v) { return new MNum(v); }
        public static bool IsNum(MNode n, out double v) { var m = n as MNum; v = m != null ? m.V : 0; return m != null; }

        public static MNode Add(MNode a, MNode b, int signB = 1)
        {
            var s = new MSum();
            Append(s, a, 1); Append(s, b, signB);
            double c = 0; var rest = new List<KeyValuePair<int, MNode>>();
            foreach (var t in s.Terms) { double v; if (IsNum(t.Value, out v)) c += t.Key * v; else rest.Add(t); }
            if (Math.Abs(c) > 1e-9) rest.Add(new KeyValuePair<int, MNode>(c < 0 ? -1 : 1, Num(Math.Abs(c))));
            if (rest.Count == 0) return Num(0);
            if (rest.Count == 1 && rest[0].Key > 0) return rest[0].Value;
            if (rest.Count == 1) return Mul(Num(-1), rest[0].Value);
            s.Terms = rest;
            return s;
        }

        static void Append(MSum s, MNode n, int sign)
        {
            if (n is MSum ms) { foreach (var t in ms.Terms) s.Terms.Add(new KeyValuePair<int, MNode>(t.Key * sign, t.Value)); }
            else s.Terms.Add(new KeyValuePair<int, MNode>(sign, n));
        }

        public static MNode Mul(MNode a, MNode b, bool divB = false)
        {
            // distribute a constant over a sum: (x * c1 + c2) * k -> x * (c1 k) + c2 k
            double k;
            if (IsNum(b, out k) && a is MSum sa && k != 0) return Distribute(sa, divB ? 1 / k : k);
            if (!divB && IsNum(a, out k) && b is MSum sb) return Distribute(sb, k);
            var p = new MProd();
            AppendF(p, a, false); AppendF(p, b, divB);
            double c = 1; var rest = new List<KeyValuePair<bool, MNode>>();
            foreach (var f in p.Factors)
            {
                double v;
                if (IsNum(f.Value, out v)) { if (f.Key) { if (v == 0) return Num(double.NaN); c /= v; } else c *= v; }
                else rest.Add(f);
            }
            if (c == 0) return Num(0);
            if (rest.Count == 0) return Num(c);
            double rc = Math.Round(c, 6);
            if (Math.Abs(rc - 1) < 1e-6) c = 1;
            if (Math.Abs(rc + 1) < 1e-6) c = -1;
            if (c == -1)
            {
                var inner = rest.Count == 1 && !rest[0].Key ? rest[0].Value : new MProd { Factors = rest };
                var neg = new MSum(); neg.Terms.Add(new KeyValuePair<int, MNode>(-1, inner));
                return neg;
            }
            if (c != 1) rest.Add(new KeyValuePair<bool, MNode>(false, Num(c)));
            if (rest.Count == 1 && !rest[0].Key) return rest[0].Value;
            p.Factors = rest;
            return p;
        }

        static void AppendF(MProd p, MNode n, bool div)
        {
            if (n is MProd mp && !div) { foreach (var f in mp.Factors) p.Factors.Add(f); }
            else if (n is MProd mp2 && div) { foreach (var f in mp2.Factors) p.Factors.Add(new KeyValuePair<bool, MNode>(!f.Key, f.Value)); }
            else p.Factors.Add(new KeyValuePair<bool, MNode>(div, n));
        }

        static MNode Distribute(MSum s, double k)
        {
            MNode acc = Num(0);
            foreach (var t in s.Terms) acc = Add(acc, Mul(t.Value, Num(k)), t.Key);
            return acc;
        }

        public static MNode Neg(MNode a) { double v; if (IsNum(a, out v)) return Num(-v); return Mul(Num(-1), a); }
        public static MNode Call(string fn, params MNode[] args) { return new MCall { Fn = fn, Args = args.ToList() }; }
    }

    public sealed class AnimStatement
    {
        public string Java;       // source text
        public string Bone;       // target bone (null when not a bone assignment)
        public string Channel;    // rotation / position
        public int Axis;
        public bool Translated;
        public string Reason;
        public int Line;
    }

    public sealed class AnimResult
    {
        public string Method;
        public List<AnimStatement> Statements = new List<AnimStatement>();
        // bone -> channel -> axis -> molang
        public SortedDictionary<string, Dictionary<string, MNode[]>> Bones = new SortedDictionary<string, Dictionary<string, MNode[]>>(StringComparer.Ordinal);
        public List<string> Notes = new List<string>();
    }

    public sealed class AnimTranslator
    {
        readonly ClassPath cp;
        readonly Interp interp;
        public AnimTranslator(ClassPath cp, Interp interp) { this.cp = cp; this.interp = interp; }

        static readonly string[] ParamQueries = { null, "query.modified_distance_moved", "query.modified_move_speed", "query.life_time", "query.target_y_rotation", "query.target_x_rotation" };

        static readonly Dictionary<string, string> VanillaAnimations = new Dictionary<string, string> {
            { "HumanoidModel", "the vanilla Bedrock humanoid animations (animation.humanoid.base_pose, animation.humanoid.move, animation.humanoid.look_at_target.default, animation.humanoid.attack.rotations, animation.humanoid.riding.*) on the renamed bones head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "PlayerModel", "the vanilla Bedrock humanoid animations on head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "QuadrupedModel", "animation.quadruped.walk and animation.common.look_at_target (Bedrock vanilla) on head / leg0 (right hind) / leg1 (left hind) / leg2 (right front) / leg3 (left front)" },
            { "HierarchicalModel", "nothing (HierarchicalModel has no default animation)" },
            { "ListModel", "nothing (ListModel has no default animation)" },
        };

        static MNode ParamNode(int i)
        {
            // ageInTicks = query.life_time (seconds) * 20
            if (i == 3) return M.Mul(new MAtom("query.life_time"), M.Num(20));
            return new MAtom(ParamQueries[i]);
        }

        // Field name -> Java part path, from the constructor and the vanilla base classes.
        public Dictionary<string, string> FieldParts(ClassDecl cls)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            var chain = new List<ClassDecl>();
            for (var c = cls; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null) chain.Add(c);
            string vanilla = chain.Select(c => c.Extends).LastOrDefault(e => e != null);
            if (vanilla == "HumanoidModel" || vanilla == "PlayerModel")
                foreach (var kv in new[] { "head", "hat", "body", "rightArm", "leftArm", "rightLeg", "leftLeg" }.Zip(new[] { "head", "hat", "body", "right_arm", "left_arm", "right_leg", "left_leg" }, (f, p) => new KeyValuePair<string, string>(f, p)))
                    map[kv.Key] = kv.Value;
            if (vanilla == "QuadrupedModel")
                foreach (var kv in new[] { "head", "body", "rightHindLeg", "leftHindLeg", "rightFrontLeg", "leftFrontLeg" }.Zip(new[] { "head", "body", "right_hind_leg", "left_hind_leg", "right_front_leg", "left_front_leg" }, (f, p) => new KeyValuePair<string, string>(f, p)))
                    map[kv.Key] = kv.Value;
            for (int i = chain.Count - 1; i >= 0; i--)
            {
                var ctor = chain[i].Methods.FirstOrDefault(m => m.Name == "<init>" && m.Params.Count >= 1);
                if (ctor == null) continue;
                BlockStmt body;
                try { body = Parser.ParseBody(chain[i].Unit, ctor); } catch (JavaSyntaxException) { continue; }
                string rootParam = ctor.Params[0].Name;
                foreach (var s in body.Stmts)
                {
                    var es = s as ExprStmt;
                    var a = es != null ? es.E as AssignExpr : null;
                    if (a == null || a.Op != "=") continue;
                    var fe = a.Target as FieldExpr;
                    string field = fe != null && fe.Target is ThisExpr ? fe.Name : (a.Target as NameExpr)?.Name;
                    if (field == null) continue;
                    var path = ChildPath(a.Value, rootParam, map);
                    if (path != null) map[field] = path;
                }
            }
            return map;
        }

        // root.getChild("a").getChild("b") / this.x.getChild("c") -> "a/b" / "<path of x>/c"
        static string ChildPath(Expr e, string rootParam, Dictionary<string, string> fields)
        {
            var c = e as CallExpr;
            if (c == null || c.Name != "getChild" || c.Args.Count != 1 || !(c.Args[0] is LitExpr lit) || !(lit.Value is string name)) return null;
            if (c.Target is NameExpr ne && ne.Name == rootParam) return name;
            if (c.Target is FieldExpr fe && fe.Target is ThisExpr && fields.ContainsKey(fe.Name)) return fields[fe.Name] + "/" + name;
            if (c.Target is NameExpr fn && fields.ContainsKey(fn.Name)) return fields[fn.Name] + "/" + name;
            var inner = ChildPath(c.Target, rootParam, fields);
            return inner != null ? inner + "/" + name : null;
        }

        // prepareMobModel then setupAnim (Java's order every frame), sharing what they assign.
        public AnimResult Translate(ClassDecl cls, GeometryOut geo)
        {
            var res = new AnimResult { Method = "prepareMobModel + setupAnim" };
            var fields = FieldParts(cls);
            var boneOfPath = geo.Bones.ToDictionary(b => b.JavaPath, b => b);
            // values assigned so far: "<path>.<prop>" -> absolute Java value
            var current = new Dictionary<string, MNode>(StringComparer.Ordinal);
            int found = 0;
            foreach (var methodName in new[] { "prepareMobModel", "setupAnim" })
            {
                MethodDecl m = null;
                for (var c = cls; c != null && m == null; c = c.Extends != null ? cp.Find(c.Extends, c) : null)
                    m = c.Methods.FirstOrDefault(x => x.Name == methodName && x.BodyStart >= 0);
                if (m == null) continue;
                found++;
                if (m.Owner != cls) res.Notes.Add(methodName + " is inherited from " + m.Owner.Name);
                BlockStmt body;
                try { body = Parser.ParseBody(m.Owner.Unit, m); }
                catch (JavaSyntaxException ex) { res.Notes.Add("could not parse " + methodName + ": " + ex.Message); continue; }
                var env = new Dictionary<string, MNode>(StringComparer.Ordinal);
                for (int i = 1; i < m.Params.Count && i < ParamQueries.Length; i++)
                {
                    // prepareMobModel(entity, limbSwing, limbSwingAmount, partialTick): Molang's queries are already smooth
                    if (methodName == "prepareMobModel" && i == 3) { env[m.Params[i].Name] = M.Num(0); continue; }
                    env[m.Params[i].Name] = ParamNode(i);
                }
                // entity.tickCount (+ partialTick) is the age in ticks
                if (m.Params.Count > 0) env[m.Params[0].Name + ".tickCount"] = ParamNode(3);
                foreach (var s in body.Stmts) Visit(s, res, env, current, fields, boneOfPath, m.Owner.Unit, cls, top: true);
            }
            if (found == 0) res.Notes.Add("no setupAnim / prepareMobModel in " + cls.Name + " or its TF super classes");
            string vanilla = null;
            for (var c = cls; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null) if (c.Extends != null) vanilla = c.Extends;
            string hint;
            if (vanilla != null && VanillaAnimations.TryGetValue(vanilla, out hint))
                res.Notes.Add("vanilla base " + vanilla + (found == 0 ? " animates every part" : " (super calls)") + ": " + hint);

            foreach (var kv in current)
            {
                int dot = kv.Key.LastIndexOf('.');
                string path = kv.Key.Substring(0, dot), prop = kv.Key.Substring(dot + 1);
                BoneOut bone;
                if (!boneOfPath.TryGetValue(path, out bone)) continue;
                string channel; int axis; double def; MNode value = kv.Value;
                switch (prop)
                {
                    case "xRot": channel = "rotation"; axis = 0; def = bone.Rot.X; break;
                    case "yRot": channel = "rotation"; axis = 1; def = bone.Rot.Y; break;
                    case "zRot": channel = "rotation"; axis = 2; def = bone.Rot.Z; break;
                    case "x": channel = "position"; axis = 0; def = bone.Pose.X; break;
                    case "y": channel = "position"; axis = 1; def = bone.Pose.Y; break;
                    case "z": channel = "position"; axis = 2; def = bone.Pose.Z; break;
                    default: continue;
                }
                // a bone the converter re-parented (the hat under the head) already turns with its new
                // parent: its own channel is the difference to the parent's (nothing when Java copies it)
                var newParent = bone.Parent != null ? geo.Bones.FirstOrDefault(b => b.Name == bone.Parent) : null;
                if (channel == "rotation" && newParent != null && !bone.JavaPath.StartsWith(newParent.JavaPath + "/", StringComparison.Ordinal))
                {
                    var parentValue = CurrentValue(newParent.JavaPath + "." + prop, newParent, prop, current);
                    if (ReferenceEquals(parentValue, value)) continue;
                    value = M.Add(value, parentValue, -1);
                }
                MNode rel;
                if (channel == "rotation") rel = M.Add(M.Mul(value, M.Num(180.0 / Math.PI)), M.Num(GeometryOut.Deg(def)), -1);
                else
                {
                    rel = M.Add(value, M.Num(def), -1);
                    if (axis == 1) rel = M.Neg(rel); // Java y points down
                }
                double num;
                if (M.IsNum(rel, out num) && Math.Abs(num) < 1e-6) continue;
                Dictionary<string, MNode[]> ch;
                if (!res.Bones.TryGetValue(bone.Name, out ch)) res.Bones[bone.Name] = ch = new Dictionary<string, MNode[]>();
                MNode[] axes;
                if (!ch.TryGetValue(channel, out axes)) ch[channel] = axes = new MNode[3];
                axes[axis] = rel;
            }
            return res;
        }

        void Visit(Stmt s, AnimResult res, Dictionary<string, MNode> env, Dictionary<string, MNode> current, Dictionary<string, string> fields,
                   Dictionary<string, BoneOut> bones, CompilationUnit unit, ClassDecl cls, bool top)
        {
            string src = SourceText(unit, s);
            if (s is EmptyStmt) return;
            if (s is LocalVarStmt lv && top)
            {
                foreach (var v in lv.Vars)
                {
                    string why = null;
                    var e = v.Value != null ? Tr(v.Value, env, current, fields, bones, out why) : null;
                    if (e != null) env[v.Key] = e;
                    else { env.Remove(v.Key); res.Statements.Add(new AnimStatement { Java = src, Translated = false, Reason = "local '" + v.Key + "': " + (v.Value == null ? "no initializer" : why), Line = s.Line }); }
                }
                return;
            }
            // a local assigned again: follow it when the new value translates, forget it otherwise
            if (s is ExprStmt ls && top && ls.E is AssignExpr la && la.Target is NameExpr ln)
            {
                string why = null;
                MNode rhs = Tr(la.Value, env, current, fields, bones, out why), old, val = null;
                if (rhs != null)
                {
                    if (la.Op == "=") val = rhs;
                    else if (env.TryGetValue(ln.Name, out old))
                    {
                        switch (la.Op)
                        {
                            case "+=": val = M.Add(old, rhs); break;
                            case "-=": val = M.Add(old, rhs, -1); break;
                            case "*=": val = M.Mul(old, rhs); break;
                            case "/=": val = IsZero(rhs) ? null : M.Mul(old, rhs, true); break;
                        }
                    }
                    else why = "'" + ln.Name + "' (entity state or a field)";
                }
                if (val != null) { env[ln.Name] = val; return; }
                env.Remove(ln.Name);
                res.Statements.Add(new AnimStatement { Java = src, Line = s.Line, Reason = "local '" + ln.Name + "': " + (why ?? "operator " + la.Op) });
                return;
            }
            if (s is ExprStmt es && es.E is AssignExpr a && top)
            {
                string path, prop;
                if (BoneProp(a.Target, fields, out path, out prop))
                {
                    var st = new AnimStatement { Java = src, Line = s.Line };
                    BoneOut b;
                    st.Bone = bones.TryGetValue(path, out b) ? b.Name : path;
                    st.Channel = prop.EndsWith("Rot") ? "rotation" : "position";
                    st.Axis = prop.StartsWith("x") ? 0 : prop.StartsWith("y") ? 1 : 2;
                    if (!(prop == "xRot" || prop == "yRot" || prop == "zRot" || prop == "x" || prop == "y" || prop == "z"))
                    {
                        st.Reason = "property '" + prop + "' (visibility / scale): use part_visibility or a scale channel";
                        res.Statements.Add(st);
                        return;
                    }
                    if (b == null) { st.Reason = "no bone for part '" + path + "'"; res.Statements.Add(st); return; }
                    string why;
                    var rhs = Tr(a.Value, env, current, fields, bones, out why);
                    if (rhs == null) { st.Reason = why; res.Statements.Add(st); current.Remove(path + "." + prop); return; }
                    string key = path + "." + prop;
                    MNode old = CurrentValue(key, b, prop, current);
                    MNode val;
                    switch (a.Op)
                    {
                        case "=": val = rhs; break;
                        case "+=": val = M.Add(old, rhs); break;
                        case "-=": val = M.Add(old, rhs, -1); break;
                        case "*=": val = M.Mul(old, rhs); break;
                        case "/=":
                            if (IsZero(rhs)) { st.Reason = "division by zero"; res.Statements.Add(st); current.Remove(key); return; }
                            val = M.Mul(old, rhs, true);
                            break;
                        default: st.Reason = "operator " + a.Op; res.Statements.Add(st); current.Remove(key); return;
                    }
                    current[key] = val;
                    st.Translated = true;
                    res.Statements.Add(st);
                    return;
                }
            }
            if (s is ExprStmt cs && cs.E is CallExpr call)
            {
                if (call.Target is SuperExpr)
                {
                    res.Statements.Add(new AnimStatement { Java = src, Line = s.Line, Reason = "the super class's " + call.Name + " (see the notes for the vanilla base)" });
                    return;
                }
                if (call.Name == "copyFrom" && call.Args.Count == 1)
                {
                    string tp, sp, dummy;
                    if (PartOf(call.Target, fields, out tp) && PartOf(call.Args[0], fields, out sp))
                    {
                        foreach (var prop in new[] { "xRot", "yRot", "zRot", "x", "y", "z" })
                        {
                            MNode v;
                            if (current.TryGetValue(sp + "." + prop, out v)) current[tp + "." + prop] = v;
                        }
                        dummy = null;
                        res.Statements.Add(new AnimStatement { Java = src, Line = s.Line, Translated = true, Bone = tp, Reason = dummy });
                        return;
                    }
                }
            }
            res.Statements.Add(new AnimStatement { Java = src, Line = s.Line, Reason = top ? "not a bone assignment" : "inside a block / loop / condition" });
            // assignments inside untranslated control flow invalidate what we know about their targets
            Invalidate(s, current, fields);
            foreach (var name in MutatedLocals(s)) env.Remove(name);
        }

        static bool IsZero(MNode n) { double v; return M.IsNum(n, out v) && v == 0; }

        // Locals a statement assigns or increments (at any depth).
        static IEnumerable<string> MutatedLocals(Stmt s)
        {
            foreach (var e in Expressions(s))
            {
                if (e is AssignExpr a && a.Target is NameExpr n) yield return n.Name;
                if (e is UnaryExpr u && (u.Op == "++" || u.Op == "--") && u.Operand is NameExpr un) yield return un.Name;
            }
        }

        static IEnumerable<Expr> Expressions(Stmt s)
        {
            switch (s)
            {
                case ExprStmt es: yield return es.E; break;
                case BlockStmt b: foreach (var x in b.Stmts) foreach (var y in Expressions(x)) yield return y; break;
                case IfStmt i:
                    foreach (var y in Expressions(i.Then)) yield return y;
                    if (i.Else != null) foreach (var y in Expressions(i.Else)) yield return y;
                    break;
                case ForStmt f:
                    foreach (var x in f.Init) foreach (var y in Expressions(x)) yield return y;
                    foreach (var u in f.Update) yield return u;
                    foreach (var y in Expressions(f.Body)) yield return y;
                    break;
                case ForEachStmt fe: foreach (var y in Expressions(fe.Body)) yield return y; break;
                case WhileStmt w: foreach (var y in Expressions(w.Body)) yield return y; break;
                case SwitchStmt sw:
                    foreach (var c in sw.Cases)
                    {
                        foreach (var st in c.Body) foreach (var y in Expressions(st)) yield return y;
                        if (c.ArrowStmt != null) foreach (var y in Expressions(c.ArrowStmt)) yield return y;
                        if (c.ArrowExpr != null) yield return c.ArrowExpr;
                    }
                    break;
            }
        }

        static MNode CurrentValue(string key, BoneOut b, string prop, Dictionary<string, MNode> current)
        {
            MNode v;
            if (current.TryGetValue(key, out v)) return v;
            switch (prop)
            {
                case "xRot": return M.Num(b.Rot.X);
                case "yRot": return M.Num(b.Rot.Y);
                case "zRot": return M.Num(b.Rot.Z);
                case "x": return M.Num(b.Pose.X);
                case "y": return M.Num(b.Pose.Y);
                default: return M.Num(b.Pose.Z);
            }
        }

        void Invalidate(Stmt s, Dictionary<string, MNode> current, Dictionary<string, string> fields)
        {
            foreach (var a in Assignments(s))
            {
                string path, prop;
                if (BoneProp(a.Target, fields, out path, out prop)) current.Remove(path + "." + prop);
            }
        }

        static IEnumerable<AssignExpr> Assignments(Stmt s)
        {
            switch (s)
            {
                case ExprStmt es: if (es.E is AssignExpr a) yield return a; break;
                case BlockStmt b: foreach (var x in b.Stmts) foreach (var y in Assignments(x)) yield return y; break;
                case IfStmt i:
                    foreach (var y in Assignments(i.Then)) yield return y;
                    if (i.Else != null) foreach (var y in Assignments(i.Else)) yield return y;
                    break;
                case ForStmt f: foreach (var y in Assignments(f.Body)) yield return y; break;
                case ForEachStmt fe: foreach (var y in Assignments(fe.Body)) yield return y; break;
                case WhileStmt w: foreach (var y in Assignments(w.Body)) yield return y; break;
                case SwitchStmt sw:
                    foreach (var c in sw.Cases)
                    {
                        foreach (var st in c.Body) foreach (var y in Assignments(st)) yield return y;
                        if (c.ArrowStmt != null) foreach (var y in Assignments(c.ArrowStmt)) yield return y;
                        if (c.ArrowExpr is AssignExpr ae) yield return ae;
                    }
                    break;
            }
        }

        static bool PartOf(Expr e, Dictionary<string, string> fields, out string path)
        {
            path = null;
            if (e is FieldExpr fe && fe.Target is ThisExpr && fields.TryGetValue(fe.Name, out path)) return true;
            if (e is NameExpr ne && fields.TryGetValue(ne.Name, out path)) return true;
            if (e is CallExpr c && c.Name == "getChild" && c.Args.Count == 1 && c.Args[0] is LitExpr l && l.Value is string n)
            {
                string parent;
                if (PartOf(c.Target, fields, out parent)) { path = parent + "/" + n; return true; }
            }
            return false;
        }

        static bool BoneProp(Expr target, Dictionary<string, string> fields, out string path, out string prop)
        {
            path = prop = null;
            var fe = target as FieldExpr;
            if (fe == null) return false;
            prop = fe.Name;
            return PartOf(fe.Target, fields, out path);
        }

        MNode Tr(Expr e, Dictionary<string, MNode> env, Dictionary<string, MNode> current, Dictionary<string, string> fields, Dictionary<string, BoneOut> bones, out string why)
        {
            why = null;
            switch (e)
            {
                case LitExpr l:
                    if (Interp.IsNumeric(l.Value)) return M.Num(Interp.ToDouble(l.Value));
                    if (l.Value is bool lb) return M.Num(lb ? 1 : 0);
                    why = "literal " + Interp.Describe(l.Value);
                    return null;
                case NameExpr n:
                {
                    MNode v;
                    if (env.TryGetValue(n.Name, out v)) return v;
                    why = "'" + n.Name + "' (entity state or a field)";
                    return null;
                }
                case FieldExpr f:
                {
                    MNode known;
                    if (f.Target is NameExpr en && env.TryGetValue(en.Name + "." + f.Name, out known)) return known;
                    // EntityModel's own state: attack progress, baby, riding
                    if (f.Target is ThisExpr)
                    {
                        switch (f.Name)
                        {
                            case "attackTime": return new MAtom("variable.attack_time");
                            case "young": return new MAtom("query.is_baby");
                            case "riding": return new MAtom("query.is_riding");
                        }
                    }
                    if (f.Target is NameExpr cn && (cn.Name == "Mth" || cn.Name == "Math"))
                    {
                        try { var v = interp.Eval(f, new Frame(null)); return M.Num(Interp.ToDouble(v)); }
                        catch (Exception) { why = cn.Name + "." + f.Name; return null; }
                    }
                    string path, prop;
                    if (BoneProp(f, fields, out path, out prop))
                    {
                        MNode cv;
                        if (current.TryGetValue(path + "." + prop, out cv)) return cv;
                        BoneOut bo;
                        if (bones.TryGetValue(path, out bo)) return CurrentValue(path + "." + prop, bo, prop, current);
                    }
                    why = "field " + f.Name;
                    return null;
                }
                case CastExpr c: return Tr(c.Operand, env, current, fields, bones, out why);
                case UnaryExpr u:
                {
                    if (u.Op == "-") { var x = Tr(u.Operand, env, current, fields, bones, out why); return x != null ? M.Neg(x) : null; }
                    if (u.Op == "+") return Tr(u.Operand, env, current, fields, bones, out why);
                    why = "operator " + u.Op;
                    return null;
                }
                case BinaryExpr bi:
                {
                    var l = Tr(bi.Left, env, current, fields, bones, out why);
                    if (l == null) return null;
                    var r = Tr(bi.Right, env, current, fields, bones, out why);
                    if (r == null) return null;
                    switch (bi.Op)
                    {
                        case "+": return M.Add(l, r);
                        case "-": return M.Add(l, r, -1);
                        case "*": return M.Mul(l, r);
                        case "/":
                            if (IsZero(r)) { why = "division by zero"; return null; }
                            return M.Mul(l, r, true);
                        case "%": return M.Call("math.mod", l, r);
                        case "<": case ">": case "<=": case ">=": case "==": case "!=": return new MCmp { Op = bi.Op, L = l, R = r };
                        case "&&": return new MCmp { Op = "&&", L = l, R = r };
                        case "||": return new MCmp { Op = "||", L = l, R = r };
                    }
                    why = "operator " + bi.Op;
                    return null;
                }
                case CondExpr ce:
                {
                    var c = Tr(ce.Cond, env, current, fields, bones, out why);
                    if (c == null) return null;
                    var a = Tr(ce.Then, env, current, fields, bones, out why);
                    if (a == null) return null;
                    var b = Tr(ce.Else, env, current, fields, bones, out why);
                    if (b == null) return null;
                    return new MCond { C = c, A = a, B = b };
                }
                case CallExpr call:
                {
                    var cn = call.Target as NameExpr;
                    string cls = cn != null ? cn.Name : null;
                    if (cls == "Mth" || cls == "Math")
                    {
                        var args = new List<MNode>();
                        foreach (var x in call.Args) { var t = Tr(x, env, current, fields, bones, out why); if (t == null) return null; args.Add(t); }
                        bool allNum = args.All(x => x is MNum);
                        if (allNum)
                        {
                            try { var v = interp.Eval(call, new Frame(null)); return M.Num(Interp.ToDouble(v)); } catch (Exception) { }
                        }
                        switch (call.Name)
                        {
                            case "sin": return M.Call("math.sin", M.Mul(args[0], M.Num(180.0 / Math.PI)));
                            case "cos": return M.Call("math.cos", M.Mul(args[0], M.Num(180.0 / Math.PI)));
                            case "abs": return M.Call("math.abs", args[0]);
                            case "sqrt": return M.Call("math.sqrt", args[0]);
                            case "min": return M.Call("math.min", args[0], args[1]);
                            case "max": return M.Call("math.max", args[0], args[1]);
                            case "clamp": return M.Call("math.clamp", args[0], args[1], args[2]);
                            case "floor": return M.Call("math.floor", args[0]);
                            case "ceil": return M.Call("math.ceil", args[0]);
                            case "lerp": return M.Call("math.lerp", args[1], args[2], args[0]);
                            case "toRadians": return M.Mul(args[0], M.Num(Math.PI / 180.0));
                            case "toDegrees": return M.Mul(args[0], M.Num(180.0 / Math.PI));
                            case "atan2": return M.Mul(M.Call("math.atan2", args[0], args[1]), M.Num(Math.PI / 180.0));
                        }
                    }
                    why = "call " + (cls != null ? cls + "." : "") + call.Name + "()";
                    return null;
                }
            }
            why = e.GetType().Name.Replace("Expr", "").ToLowerInvariant() + " expression";
            return null;
        }

        static string SourceText(CompilationUnit unit, Stmt s)
        {
            // the statement's line(s) from the source file
            var lines = unit.Source.Split('\n');
            int i = Math.Max(0, Math.Min(lines.Length - 1, s.Line - 1));
            var sb = new StringBuilder(lines[i].Trim());
            // continue multi-line statements up to the ';' or '{'
            for (int k = i + 1; k < lines.Length && !sb.ToString().TrimEnd().EndsWith(";") && !sb.ToString().TrimEnd().EndsWith("{") && !sb.ToString().TrimEnd().EndsWith("}") && k < i + 8; k++)
                sb.Append(' ').Append(lines[k].Trim());
            return sb.ToString();
        }

        public static JObj AnimationJson(string id, AnimResult r)
        {
            var bones = new JObj();
            foreach (var kv in r.Bones)
            {
                var b = new JObj();
                foreach (var ch in new[] { "rotation", "position" })
                {
                    MNode[] axes;
                    if (!kv.Value.TryGetValue(ch, out axes)) continue;
                    var arr = new List<object>();
                    foreach (var a in axes)
                    {
                        double v;
                        if (a == null) arr.Add(0.0);
                        else if (M.IsNum(a, out v)) arr.Add(Math.Round(v, 4));
                        else arr.Add(a.Emit());
                    }
                    b.Add(ch, arr);
                }
                bones.Add(kv.Key, b);
            }
            var anim = new JObj().Add("loop", true).Add("bones", bones);
            return new JObj().Add("format_version", "1.8.0").Add("animations", new JObj().Add(id, anim));
        }
    }
}
