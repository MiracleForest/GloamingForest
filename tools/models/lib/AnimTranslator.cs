// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// setupAnim -> Molang for the model converter (contract "geo", unit F2): a mechanical pass over an upstream
// model's prepareMobModel / setupAnim and the upstream methods they call. Bone rotations and offsets whose
// value uses only the animation parameters, constants, locals, earlier values and Mth / Math functions
// become a Bedrock base animation; everything else is listed with its Java source for the owning unit
// to translate by hand. Part of GloamingForest, LGPL-3.0-or-later.
//
// Mapping: limbSwing -> query.modified_distance_moved, limbSwingAmount -> query.modified_move_speed,
// ageInTicks -> query.life_time * 20, netHeadYaw -> query.target_y_rotation, headPitch ->
// query.target_x_rotation (the vanilla quadruped / humanoid animations use the same queries).
// Java angles are radians and absolute (they replace the PartPose rotation); Molang angles are
// degrees added to the geometry's rotation, so a rotation becomes value * 57.29578 - default.
// Molang's math.sin / math.cos take degrees.
//
// The base path. Every value is followed with the statements it carries and the cases it depends on.
// A condition that translates becomes a Molang ?: over both branches; one that does not (entity
// state) counts as false (a negated one as true), unless catalog.json's baseCase gives the state's
// value in upstream's common state (or a value upstream never changes, which folds to a constant): the base
// animation runs that branch, and the condition is a "case" entry of the todo list naming the
// channels that differ when it holds. A branch that returns early is never the base path's: the
// other one is, and what the rest of the method sets differs in that case. An assignment inside an
// expression runs before it when it always runs (else what it changes is unknown). A value that does
// not translate (or a loop / unknown call that may change it) is unknown from then on: a later
// compound assignment or read of it stays in todo too, and statements whose effect it replaced leave
// the translated list. After a vanilla super.setupAnim the vanilla parts hold "the vanilla pose plus
// a delta": += / -= add to the delta, '=' replaces the vanilla pose (listed in OverridesVanilla), reads
// and *= of the vanilla value stay untranslated; where one branch of a Molang condition sets an axis
// and the other keeps the vanilla pose, the base animation takes the one that sets it (a case).
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

        public static bool Same(MNode a, MNode b) { return ReferenceEquals(a, b) || (a != null && b != null && a.Emit() == b.Emit()); }

        public static MNode Not(MNode a)
        {
            double v;
            if (IsNum(a, out v)) return Num(v != 0 ? 0 : 1);
            if (a is MNot n) return n.X;
            return new MNot { X = a };
        }

        // Comparisons and && / || with constant folding (Java booleans are 0 / 1).
        public static MNode Cmp(string op, MNode l, MNode r)
        {
            double a, b;
            bool la = IsNum(l, out a), rb = IsNum(r, out b);
            if (la && rb)
            {
                switch (op)
                {
                    case "<": return Num(a < b ? 1 : 0);
                    case ">": return Num(a > b ? 1 : 0);
                    case "<=": return Num(a <= b ? 1 : 0);
                    case ">=": return Num(a >= b ? 1 : 0);
                    case "==": return Num(a == b ? 1 : 0);
                    case "!=": return Num(a != b ? 1 : 0);
                    case "&&": return Num(a != 0 && b != 0 ? 1 : 0);
                    case "||": return Num(a != 0 || b != 0 ? 1 : 0);
                }
            }
            if (op == "&&") { if (la) return a != 0 ? r : Num(0); if (rb) return b != 0 ? l : Num(0); }
            if (op == "||") { if (la) return a != 0 ? Num(1) : r; if (rb) return b != 0 ? Num(1) : l; }
            return new MCmp { Op = op, L = l, R = r };
        }

        public static MNode Cond(MNode c, MNode a, MNode b)
        {
            double v;
            if (IsNum(c, out v)) return v != 0 ? a : b;
            if (Same(a, b)) return a;
            return new MCond { C = c, A = a, B = b };
        }
    }

    public sealed class MNot : MNode
    {
        public MNode X;
        public override int Prec { get { return 3; } }
        public override string Emit() { return "!" + EmitIn(X, 3); }
    }

    // A value that is not a number: an enum constant ("ArmPose.EMPTY", "HumanoidArm.RIGHT") or null. It only
    // takes part in == / != against another one and never reaches Molang (Tr refuses it in arithmetic and
    // bone values). Known: the value of a base-case entry (catalog.json baseCase), whose type is known.
    public sealed class MSym : MNode
    {
        public string Name;
        public bool Known;
        public override int Prec { get { return 3; } }
        public override string Emit() { return Name; }
        public string Qualifier { get { int dot = Name.LastIndexOf('.'); return dot < 0 ? "" : Name.Substring(0, dot); } }
        public static bool Has(MNode n)
        {
            switch (n)
            {
                case MSym _: return true;
                case MCall c: return c.Args.Any(Has);
                case MSum s: return s.Terms.Any(t => Has(t.Value));
                case MProd p: return p.Factors.Any(f => Has(f.Value));
                case MCond c: return Has(c.C) || Has(c.A) || Has(c.B);
                case MCmp c: return Has(c.L) || Has(c.R);
                case MNot x: return Has(x.X);
            }
            return false;
        }
    }

    // The value entity or model state has on the base path (catalog.json "baseCase"): the common state of
    // upstream's mobs, or one upstream never changes (Fixed: comparisons with it fold to a constant, no case entry).
    public sealed class BaseCaseValue
    {
        public string Scope;   // a class of the model's chain (its upstream classes, its vanilla base, "HumanoidModel" /
                               // "QuadrupedModel" for their families) or "*"
        public string Expr;    // Java text: "entity" names the entity parameter, "this." a field or method of the
                               // model; "(*)" at the end stands for any arguments
        public object Value;   // double, bool or string (an enum constant, "null")
        public bool Fixed;
        public string Why;
        public string ValueText
        {
            get
            {
                if (Value is string s) return s;
                if (Value is bool b) return b ? "true" : "false";
                return Number.ToString("0.######", CultureInfo.InvariantCulture);
            }
        }
        public double Number { get { return Value is bool b ? (b ? 1 : 0) : Convert.ToDouble(Value, CultureInfo.InvariantCulture); } }
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
        public int EndLine;       // last line of the branches of a case entry (0: the statement's own line)
        public string Class;      // the class whose source holds Line, when not the translated model class
        // A case entry: the base animation contains the case in which the conditions it names are
        // false; Affects lists the channels (bone.prop) that differ when they hold.
        public bool Case;
        public SortedSet<string> Affects = new SortedSet<string>(StringComparer.Ordinal);
    }

    public sealed class AnimResult
    {
        public string Method;
        public List<AnimStatement> Statements = new List<AnimStatement>();
        // bone -> channel -> axis -> molang
        public SortedDictionary<string, Dictionary<string, MNode[]>> Bones = new SortedDictionary<string, Dictionary<string, MNode[]>>(StringComparer.Ordinal);
        public List<string> Notes = new List<string>();
        // "bone.prop" -> Java lines: bone axes Java sets with '=' after the vanilla super.setupAnim. They
        // replace the vanilla pose, so the vanilla Bedrock animations must not drive them.
        public SortedDictionary<string, SortedSet<int>> OverridesVanilla = new SortedDictionary<string, SortedSet<int>>(StringComparer.Ordinal);
    }

    public sealed class AnimTranslator
    {
        readonly ClassPath cp;
        readonly Interp interp;
        public AnimTranslator(ClassPath cp, Interp interp) { this.cp = cp; this.interp = interp; }

        // catalog.json "baseCase": the values entity / model state takes on the base path
        public List<BaseCaseValue> BaseCases = new List<BaseCaseValue>();

        static readonly string[] ParamQueries = { null, "query.modified_distance_moved", "query.modified_move_speed", "query.life_time", "query.target_y_rotation", "query.target_x_rotation" };
        static readonly string[] PoseProps = { "xRot", "yRot", "zRot", "x", "y", "z" };

        // The vanilla model families upstream extends: the parts their setupAnim drives.
        public static readonly string[] HumanoidFamily = { "HumanoidModel", "PlayerModel", "ZombieModel", "AbstractZombieModel", "SkeletonModel", "DrownedModel", "PiglinModel", "GiantZombieModel" };
        public static readonly string[] QuadrupedFamily = { "QuadrupedModel", "SheepModel", "CowModel", "PigModel", "PolarBearModel", "PandaModel" };
        static readonly string[] HumanoidParts = { "head", "hat", "body", "right_arm", "left_arm", "right_leg", "left_leg" };
        static readonly string[] HumanoidFields = { "head", "hat", "body", "rightArm", "leftArm", "rightLeg", "leftLeg" };
        static readonly string[] QuadrupedParts = { "head", "body", "right_hind_leg", "left_hind_leg", "right_front_leg", "left_front_leg" };
        static readonly string[] QuadrupedFields = { "head", "body", "rightHindLeg", "leftHindLeg", "rightFrontLeg", "leftFrontLeg" };

        static readonly Dictionary<string, string> VanillaAnimations = new Dictionary<string, string> {
            { "HumanoidModel", "the vanilla Bedrock humanoid animations (animation.humanoid.base_pose, animation.humanoid.move, animation.humanoid.look_at_target.default, animation.humanoid.attack.rotations, animation.humanoid.riding.*) on the renamed bones head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "PlayerModel", "the vanilla Bedrock humanoid animations on head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "ZombieModel", "the vanilla Bedrock humanoid animations plus animation.zombie.attack_bare_hand (AnimationUtils.animateZombieArms) on head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "AbstractZombieModel", "the vanilla Bedrock humanoid animations plus animation.zombie.attack_bare_hand (AnimationUtils.animateZombieArms) on head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "SkeletonModel", "the vanilla Bedrock humanoid animations plus animation.skeleton.attack and animation.humanoid.bow_and_arrow on head / body / rightArm / leftArm / rightLeg / leftLeg" },
            { "QuadrupedModel", "animation.quadruped.walk and animation.common.look_at_target (Bedrock vanilla) on head / leg0 (right hind) / leg1 (left hind) / leg2 (right front) / leg3 (left front)" },
            { "SheepModel", "animation.quadruped.walk, animation.common.look_at_target and the sheep's grazing animation (Bedrock vanilla) on head / leg0..leg3" },
            { "PigModel", "animation.quadruped.walk and animation.common.look_at_target (Bedrock vanilla) on head / leg0..leg3" },
            { "HierarchicalModel", "nothing (HierarchicalModel has no default animation)" },
            { "ListModel", "nothing (ListModel has no default animation)" },
            { "AgeableListModel", "nothing (AgeableListModel has no default animation)" },
        };

        // Vanilla helpers upstream calls from setupAnim, as Java the translator inlines: Minecraft's
        // AnimationUtils.bobModelPart / bobArms formulas (the same terms AlphaYetiModel.setupAnim writes out).
        const string VanillaHelperSource = @"package gf.vanilla; public class AnimationUtils {
            public static void bobModelPart(ModelPart modelPart, float ageInTicks, float multiplier) {
                modelPart.zRot += multiplier * (Mth.cos(ageInTicks * 0.09F) * 0.05F + 0.05F);
                modelPart.xRot += multiplier * Mth.sin(ageInTicks * 0.067F) * 0.05F;
            }
            public static void bobArms(ModelPart rightArm, ModelPart leftArm, float ageInTicks) {
                bobModelPart(rightArm, ageInTicks, 1.0F);
                bobModelPart(leftArm, ageInTicks, -1.0F);
            }
        }";
        static ClassDecl vanillaHelpers;
        static ClassDecl VanillaHelpers
        {
            get
            {
                if (vanillaHelpers == null) vanillaHelpers = Parser.ParseUnit(VanillaHelperSource, "AnimationUtils.java").Types[0];
                return vanillaHelpers;
            }
        }

        static MNode ParamNode(int i)
        {
            // ageInTicks = query.life_time (seconds) * 20
            if (i == 3) return M.Mul(new MAtom("query.life_time"), M.Num(20));
            return new MAtom(ParamQueries[i]);
        }

        // ---- value states -------------------------------------------------------------------------------

        // What a local or a bone property holds at a point of the base path.
        sealed class Val
        {
            public MNode Node;      // null: not translated (Why, At)
            public bool Vanilla;    // the Java value is the vanilla base's value plus Node
            public string Why;
            public int At;
            public HashSet<AnimStatement> From = new HashSet<AnimStatement>(); // statements whose effect it carries
            public HashSet<AnimStatement> When = new HashSet<AnimStatement>(); // case entries under which it differs
            public static Val Unknown(string why, int at) { return new Val { Why = why, At = at }; }
            public Val With(HashSet<AnimStatement> when)
            {
                var v = new Val { Node = Node, Vanilla = Vanilla, Why = Why, At = At, From = From, When = new HashSet<AnimStatement>(When) };
                v.When.UnionWith(when);
                return v;
            }
        }

        // What an expression read (statements, cases), the conditions it counted as false and the base-case
        // values (catalog.json baseCase) it took for entity state.
        sealed class Deps
        {
            public HashSet<AnimStatement> From = new HashSet<AnimStatement>();
            public HashSet<AnimStatement> When = new HashSet<AnimStatement>();
            public List<string> Assumed = new List<string>(); // Java text counted as false
            public List<string> Valued = new List<string>();  // "<Java> as <value> (<why>)"
            public bool Assumes { get { return Assumed.Count > 0 || Valued.Count > 0; } }
            public void Read(Val v) { From.UnionWith(v.From); When.UnionWith(v.When); }
            public void Add(Deps d) { From.UnionWith(d.From); When.UnionWith(d.When); Assumed.AddRange(d.Assumed); Valued.AddRange(d.Valued); }
            public string Text()
            {
                var parts = new List<string>();
                if (Assumed.Count > 0) parts.Add(string.Join(", ", Assumed.Distinct()) + " as false");
                parts.AddRange(Valued.Distinct());
                return string.Join(", ", parts);
            }
        }

        // State shared by every frame of one model.
        sealed class Shared
        {
            public AnimResult Res;
            public ClassDecl Model;
            public string Vanilla;                        // the vanilla base class (first non-upstream super class)
            public List<string> VanillaParts = new List<string>();
            public Dictionary<string, BoneOut> Bones;     // Java path -> bone
            public Dictionary<string, string> FieldMap = new Dictionary<string, string>(StringComparer.Ordinal); // field -> part path
            public HashSet<string> NamedParts = new HashSet<string>(StringComparer.Ordinal); // paths a field names
            public bool VanillaCalled;
            public int VanillaLine;
            public Dictionary<AnimStatement, string> Lost = new Dictionary<AnimStatement, string>();
            public Dictionary<string, SortedSet<int>> Overrides = new Dictionary<string, SortedSet<int>>(StringComparer.Ordinal); // key -> lines
            public List<BaseCaseValue> BaseCases = new List<BaseCaseValue>(); // the entries whose scope the model is in
            public Dictionary<BaseCaseValue, SortedSet<string>> FixedUsed = new Dictionary<BaseCaseValue, SortedSet<string>>(); // fixed value -> "[Class ]line"
        }

        // A condition whose other branch leaves the method early: what the rest of the method sets
        // differs in its case (Cur: the values when the base path passed the condition).
        sealed class Exit { public AnimStatement St; public Dictionary<string, Val> Cur; }

        // One method body on the base path (a branch of a translated condition runs in a fork).
        sealed class Ctx
        {
            public Shared S;
            public Dictionary<string, Val> Env;          // locals and parameters
            public Dictionary<string, Val> Cur;          // "<path>.<prop>" (shared with inlined callees)
            public Dictionary<string, string> Fields;    // field / parameter / local name -> part path
            public Dictionary<string, List<string>> PartLocals = new Dictionary<string, List<string>>(StringComparer.Ordinal); // ModelPart locals of unknown part -> candidates
            public HashSet<string> BoolLocals = new HashSet<string>(StringComparer.Ordinal);
            public HashSet<string> EntityNames = new HashSet<string>(StringComparer.Ordinal); // parameters / locals holding the entity
            public List<Exit> Exits = new List<Exit>();  // shared with forks; closed at the end of the method
            public HashSet<Expr> Applied = new HashSet<Expr>(); // assignments inside expressions already run (ApplyNested)
            public CompilationUnit Unit;
            public ClassDecl Owner;                      // the class whose method runs (super / this calls)
            public string Label;                         // class name for statements of another class (null: the model)
            public AnimStatement Proxy;                  // a vanilla helper reports through its call statement
            public bool Returned;
            public int Depth;
            public Ctx Fork()
            {
                var c = (Ctx)MemberwiseClone();
                c.Env = new Dictionary<string, Val>(Env, StringComparer.Ordinal);
                c.Cur = new Dictionary<string, Val>(Cur, StringComparer.Ordinal);
                c.Fields = new Dictionary<string, string>(Fields, StringComparer.Ordinal);
                c.PartLocals = new Dictionary<string, List<string>>(PartLocals, StringComparer.Ordinal);
                c.BoolLocals = new HashSet<string>(BoolLocals, StringComparer.Ordinal);
                return c;
            }
        }

        // ---- parts ----------------------------------------------------------------------------------------

        public static bool IsHumanoid(string vanilla) { return vanilla != null && Array.IndexOf(HumanoidFamily, vanilla) >= 0; }
        public static bool IsQuadruped(string vanilla) { return vanilla != null && Array.IndexOf(QuadrupedFamily, vanilla) >= 0; }

        // The first super class of the chain that is not an upstream class (HumanoidModel, QuadrupedModel, ...).
        public string VanillaBaseOf(ClassDecl cls)
        {
            string vanilla = null;
            for (var c = cls; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null) if (c.Extends != null) vanilla = c.Extends;
            return vanilla;
        }

        // The base-case entries a model is in scope of: its upstream classes, its vanilla base class and that class's
        // family ("HumanoidModel" covers ZombieModel, SkeletonModel, ...; "QuadrupedModel" SheepModel, ...).
        List<BaseCaseValue> BaseCasesFor(ClassDecl cls, string vanilla)
        {
            var names = new HashSet<string>(StringComparer.Ordinal) { "*" };
            for (var c = cls; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null) names.Add(c.Name);
            if (vanilla != null) names.Add(vanilla);
            if (IsHumanoid(vanilla)) names.Add("HumanoidModel");
            if (IsQuadruped(vanilla)) names.Add("QuadrupedModel");
            return BaseCases.Where(b => b.Scope != null && names.Contains(b.Scope)).ToList();
        }

        // Field name -> Java part path, from the constructor and the vanilla base classes.
        public Dictionary<string, string> FieldParts(ClassDecl cls)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            var chain = new List<ClassDecl>();
            for (var c = cls; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null) chain.Add(c);
            string vanilla = chain.Select(c => c.Extends).LastOrDefault(e => e != null);
            if (IsHumanoid(vanilla))
                for (int i = 0; i < HumanoidFields.Length; i++) map[HumanoidFields[i]] = HumanoidParts[i];
            if (IsQuadruped(vanilla))
                for (int i = 0; i < QuadrupedFields.Length; i++) map[QuadrupedFields[i]] = QuadrupedParts[i];
            for (int i = chain.Count - 1; i >= 0; i--)
            {
                var ctor = chain[i].Methods.FirstOrDefault(m => m.Name == "<init>" && m.Params.Count >= 1);
                if (ctor == null) continue;
                BlockStmt body;
                try { body = Parser.ParseBody(chain[i].Unit, ctor); } catch (JavaSyntaxException) { continue; }
                string rootParam = ctor.Params[0].Name;
                // the constructor's part locals (var body = root.getChild("body"); this.eye = body.getChild(...))
                var locals = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var s in Flatten(body))
                {
                    if (s is LocalVarStmt lv)
                    {
                        foreach (var v in lv.Vars)
                        {
                            var lp = v.Value != null ? ChildPath(v.Value, rootParam, map, locals) : null;
                            if (lp != null) locals[v.Key] = lp; else locals.Remove(v.Key);
                        }
                        continue;
                    }
                    var es = s as ExprStmt;
                    var a = es != null ? es.E as AssignExpr : null;
                    if (a == null || a.Op != "=") continue;
                    var fe = a.Target as FieldExpr;
                    string field = fe != null && fe.Target is ThisExpr ? fe.Name : (a.Target as NameExpr)?.Name;
                    if (field == null) continue;
                    // this.root = root: the model root ("", no bone of its own)
                    var path = a.Value is NameExpr rn && rn.Name == rootParam ? "" : ChildPath(a.Value, rootParam, map, locals);
                    if (path != null) map[field] = path;
                }
            }
            return map;
        }

        static string JoinPath(string parent, string name) { return string.IsNullOrEmpty(parent) ? name : parent + "/" + name; }

        // The statements of a constructor in order, blocks and both branches of an if included
        // (QuestRamModel assigns its parts inside `if (root.hasChild("neck"))`).
        static IEnumerable<Stmt> Flatten(Stmt s)
        {
            if (s is BlockStmt b) { foreach (var x in b.Stmts) foreach (var y in Flatten(x)) yield return y; }
            else if (s is IfStmt i)
            {
                foreach (var y in Flatten(i.Then)) yield return y;
                if (i.Else != null) foreach (var y in Flatten(i.Else)) yield return y;
            }
            else if (s != null) yield return s;
        }

        // root.getChild("a").getChild("b") / this.x.getChild("c") / local.getChild("d") -> "a/b" / "<path of x>/c" / ...
        static string ChildPath(Expr e, string rootParam, Dictionary<string, string> fields, Dictionary<string, string> locals)
        {
            var c = e as CallExpr;
            if (c == null || c.Name != "getChild" || c.Args.Count != 1 || !(c.Args[0] is LitExpr lit) || !(lit.Value is string name)) return null;
            string lp;
            if (c.Target is NameExpr ln && locals.TryGetValue(ln.Name, out lp)) return JoinPath(lp, name);
            if (c.Target is NameExpr ne && ne.Name == rootParam) return name;
            if (c.Target is FieldExpr fe && fe.Target is ThisExpr && fields.ContainsKey(fe.Name)) return JoinPath(fields[fe.Name], name);
            if (c.Target is NameExpr fn && fields.ContainsKey(fn.Name)) return JoinPath(fields[fn.Name], name);
            // HeadedModel.getHead() of the humanoid / quadruped bases
            if (c.Target is CallExpr gh && gh.Name == "getHead" && gh.Args.Count == 0 && (gh.Target == null || gh.Target is ThisExpr) && fields.ContainsKey("head"))
                return JoinPath(fields["head"], name);
            var inner = ChildPath(c.Target, rootParam, fields, locals);
            return inner != null ? JoinPath(inner, name) : null;
        }

        static bool PartOf(Expr e, Dictionary<string, string> fields, out string path)
        {
            path = null;
            if (e is FieldExpr fe && fe.Target is ThisExpr && fields.TryGetValue(fe.Name, out path)) return true;
            if (e is NameExpr ne && fields.TryGetValue(ne.Name, out path)) return true;
            if (e is CastExpr ce) return PartOf(ce.Operand, fields, out path);
            if (e is CallExpr gh && gh.Name == "getHead" && gh.Args.Count == 0 && (gh.Target == null || gh.Target is ThisExpr) && fields.TryGetValue("head", out path)) return true;
            if (e is CallExpr c && c.Name == "getChild" && c.Args.Count == 1 && c.Args[0] is LitExpr l && l.Value is string n)
            {
                string parent;
                if (PartOf(c.Target, fields, out parent)) { path = JoinPath(parent, n); return true; }
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

        static bool IsPoseProp(string prop) { return Array.IndexOf(PoseProps, prop) >= 0; }

        // Java's initial pose (PartPose; the re-parented hat keeps the head's there).
        static double DefaultOf(BoneOut b, string prop)
        {
            switch (prop)
            {
                case "xRot": return b.Pose.XRot;
                case "yRot": return b.Pose.YRot;
                case "zRot": return b.Pose.ZRot;
                case "x": return b.Pose.X;
                case "y": return b.Pose.Y;
                default: return b.Pose.Z;
            }
        }

        static Val Get(Ctx c, string key)
        {
            Val v;
            if (c.Cur.TryGetValue(key, out v)) return v;
            int dot = key.LastIndexOf('.');
            BoneOut b;
            if (!c.S.Bones.TryGetValue(key.Substring(0, dot), out b)) return Val.Unknown("no bone for " + key, 0);
            return new Val { Node = M.Num(DefaultOf(b, key.Substring(dot + 1))) };
        }

        static string PartName(Ctx c, string path) { BoneOut b; return c.S.Bones.TryGetValue(path, out b) ? b.Name : path; }
        static string Label(Ctx c, string key) { int dot = key.LastIndexOf('.'); return PartName(c, key.Substring(0, dot)) + "." + key.Substring(dot + 1); }

        // The parts an unresolved part expression may be: a ModelPart local's candidates, HumanoidModel.getArm's
        // two arms, an array element any part no field names (upstream keeps only repeated parts in arrays), else any.
        List<string> Candidates(Expr e, Ctx c)
        {
            List<string> list;
            if (e is NameExpr n && c.PartLocals.TryGetValue(n.Name, out list)) return list;
            if (e is CallExpr call && call.Name == "getArm" && (call.Target == null || call.Target is ThisExpr))
                return new[] { "right_arm", "left_arm" }.Where(p => c.S.Bones.ContainsKey(p)).ToList();
            // an array element or a part field the constructor sets in a way FieldParts does not follow:
            // a part no field names
            if (e is IndexExpr || (e is FieldExpr fe && fe.Target is ThisExpr))
                return c.S.Bones.Keys.Where(p => !c.S.NamedParts.Contains(p)).OrderBy(p => p, StringComparer.Ordinal).ToList();
            return c.S.Bones.Keys.OrderBy(p => p, StringComparer.Ordinal).ToList();
        }

        // root() (every part) or a part and its descendants; null when not resolved.
        List<string> PartsUnder(Expr e, Ctx c)
        {
            if (e is CallExpr r && r.Name == "root" && r.Args.Count == 0 && (r.Target == null || r.Target is ThisExpr)) return c.S.Bones.Keys.ToList();
            string path;
            if (PartOf(e, c.Fields, out path)) return c.S.Bones.Keys.Where(p => path.Length == 0 || p == path || p.StartsWith(path + "/", StringComparison.Ordinal)).ToList();
            return null;
        }

        static bool LooksPure(string name)
        {
            return name.StartsWith("get", StringComparison.Ordinal) || name.StartsWith("is", StringComparison.Ordinal) || name.StartsWith("has", StringComparison.Ordinal)
                || name.StartsWith("can", StringComparison.Ordinal) || name == "root" || name == "rotlerpRad" || name == "quadraticArmUpdate";
        }

        MethodDecl FindMethod(ClassDecl from, string name, int argc = -1)
        {
            for (var c = from; c != null; c = c.Extends != null ? cp.Find(c.Extends, c) : null)
            {
                var m = c.Methods.FirstOrDefault(x => x.Name == name && x.BodyStart >= 0 && (argc < 0 || x.Params.Count == argc));
                if (m != null) return m;
            }
            return null;
        }

        // ---- the translation ------------------------------------------------------------------------------

        // prepareMobModel then setupAnim (Java's order every frame), sharing what they assign.
        public AnimResult Translate(ClassDecl cls, GeometryOut geo)
        {
            var res = new AnimResult { Method = "prepareMobModel + setupAnim" };
            var s = new Shared { Res = res, Model = cls, Vanilla = VanillaBaseOf(cls), Bones = new Dictionary<string, BoneOut>(StringComparer.Ordinal), FieldMap = FieldParts(cls) };
            s.BaseCases = BaseCasesFor(cls, s.Vanilla);
            foreach (var b in geo.Bones) s.Bones[b.JavaPath] = b;
            foreach (var p in s.FieldMap.Values) s.NamedParts.Add(p);
            var vparts = IsHumanoid(s.Vanilla) ? HumanoidParts : IsQuadruped(s.Vanilla) ? QuadrupedParts : new string[0];
            s.VanillaParts = vparts.Where(p => s.Bones.ContainsKey(p)).ToList();
            var cur = new Dictionary<string, Val>(StringComparer.Ordinal);
            int found = 0;
            bool ownSetupAnim = false;
            foreach (var methodName in new[] { "prepareMobModel", "setupAnim" })
            {
                var m = FindMethod(cls, methodName);
                if (m == null) continue;
                found++;
                if (methodName == "setupAnim") ownSetupAnim = true;
                if (m.Owner != cls) res.Notes.Add(methodName + " is inherited from " + m.Owner.Name);
                BlockStmt body;
                try { body = Parser.ParseBody(m.Owner.Unit, m); }
                catch (JavaSyntaxException ex) { res.Notes.Add("could not parse " + methodName + ": " + ex.Message); continue; }
                var c = new Ctx
                {
                    S = s, Env = new Dictionary<string, Val>(StringComparer.Ordinal), Cur = cur, Fields = new Dictionary<string, string>(s.FieldMap, StringComparer.Ordinal),
                    Unit = m.Owner.Unit, Owner = m.Owner, Label = m.Owner != cls ? m.Owner.Name : null,
                };
                for (int i = 1; i < m.Params.Count && i < ParamQueries.Length; i++)
                {
                    c.Fields.Remove(m.Params[i].Name);
                    // prepareMobModel(entity, limbSwing, limbSwingAmount, partialTick): Molang's queries are already smooth
                    if (methodName == "prepareMobModel" && i == 3) { c.Env[m.Params[i].Name] = new Val { Node = M.Num(0) }; continue; }
                    c.Env[m.Params[i].Name] = new Val { Node = ParamNode(i) };
                }
                // entity.tickCount (+ partialTick) is the age in ticks
                if (m.Params.Count > 0)
                {
                    c.Env[m.Params[0].Name + ".tickCount"] = new Val { Node = ParamNode(3) };
                    c.EntityNames.Add(m.Params[0].Name);
                }
                Exec(body, c);
                CloseExits(c);
            }
            if (found == 0) res.Notes.Add("no setupAnim / prepareMobModel in " + cls.Name + " or its upstream super classes");
            if (!ownSetupAnim)
            {
                // the vanilla setupAnim runs after prepareMobModel and sets the vanilla parts
                var vc = new Ctx { S = s, Cur = cur };
                foreach (var p in s.VanillaParts)
                    foreach (var prop in PoseProps)
                    {
                        Val old;
                        if (cur.TryGetValue(p + "." + prop, out old)) Lose(vc, old, Label(vc, p + "." + prop) + ": the vanilla " + s.Vanilla + ".setupAnim sets it afterwards");
                        cur[p + "." + prop] = new Val { Node = M.Num(0), Vanilla = true };
                    }
            }
            string hint = null;
            if (s.Vanilla != null && VanillaAnimations.TryGetValue(s.Vanilla, out hint))
            {
                if (!ownSetupAnim) res.Notes.Add("vanilla base " + s.Vanilla + " animates every part (no setupAnim in " + cls.Name + " or its upstream super classes): " + hint);
                else if (s.VanillaCalled)
                    res.Notes.Add("vanilla base " + s.Vanilla + ": super.setupAnim (line " + s.VanillaLine + ") runs " + hint + "; the base animation adds to it"
                        + (s.Overrides.Count > 0 ? ". overrides_vanilla lists the bone axes Java sets with '=' after that call: they replace the vanilla pose, so the vanilla animations must not drive them (in the cases whose lines it gives)" : ""));
                else if (s.VanillaParts.Count > 0) res.Notes.Add("setupAnim replaces " + s.Vanilla + ".setupAnim (no super call): play no vanilla animation on these bones (the base class would play " + hint + ")");
                else res.Notes.Add("vanilla base " + s.Vanilla + ": " + hint);
            }
            Finish(res, s, cur, geo);
            return res;
        }

        // Moves lost statements to todo, emits the channels and fills in the channels each case affects.
        void Finish(AnimResult res, Shared s, Dictionary<string, Val> cur, GeometryOut geo)
        {
            var live = new HashSet<AnimStatement>();
            foreach (var kv in cur) if (kv.Value.Node != null) live.UnionWith(kv.Value.From);
            foreach (var st in res.Statements)
            {
                string why;
                if (st.Translated && !live.Contains(st) && s.Lost.TryGetValue(st, out why))
                {
                    st.Translated = false;
                    st.Reason = "its effect is not in the base animation: " + why;
                }
            }
            var byName = geo.Bones.ToDictionary(b => b.Name, b => b);
            var ctx = new Ctx { S = s, Cur = cur };
            foreach (var bone in geo.Bones)
            {
                BoneOut parent = null;
                bool reparented = bone.Parent != null && byName.TryGetValue(bone.Parent, out parent) && !bone.JavaPath.StartsWith(parent.JavaPath + "/", StringComparison.Ordinal);
                foreach (var prop in PoseProps)
                {
                    string key = bone.JavaPath + "." + prop;
                    Val v;
                    bool has = cur.TryGetValue(key, out v);
                    if (has && v.Node != null) foreach (var w in v.When) w.Affects.Add(bone.Name + "." + prop);
                    string channel = prop.EndsWith("Rot", StringComparison.Ordinal) ? "rotation" : "position";
                    int axis = prop.StartsWith("x", StringComparison.Ordinal) ? 0 : prop.StartsWith("y", StringComparison.Ordinal) ? 1 : 2;
                    double bind = axis == 0 ? bone.Rot.X : axis == 1 ? bone.Rot.Y : bone.Rot.Z;
                    MNode rel;
                    if (reparented)
                    {
                        // a bone the converter re-parented (the hat under the head) already follows its new
                        // parent: its own channel is the difference to the parent's (Java poses both in model
                        // space, from the same pivot and initial rotation)
                        string pkey = parent.JavaPath + "." + prop;
                        Val pv;
                        bool phas = cur.TryGetValue(pkey, out pv);
                        // (nothing to draw: no cube, or only empty ones like the Alpha Yeti's hat)
                        if ((!has && !phas) || bone.Cubes.All(cb => cb.W == 0 && cb.H == 0 && cb.D == 0)) continue;
                        if (!has) v = Get(ctx, key);
                        if (!phas) pv = Get(ctx, pkey);
                        if (v.Node == null || pv.Node == null) continue;
                        if (v.Vanilla != pv.Vanilla)
                        {
                            res.Notes.Add(bone.Name + "." + prop + ": the vanilla pose drives only one of " + bone.Name + " / " + parent.Name + " there; its channel is left out");
                            continue;
                        }
                        var diff = M.Same(v.Node, pv.Node) ? M.Num(0) : M.Add(v.Node, pv.Node, -1);
                        if (channel == "rotation") rel = M.Add(M.Mul(diff, M.Num(180.0 / Math.PI)), M.Num(GeometryOut.Deg(bind)), -1);
                        else rel = axis == 1 ? M.Neg(diff) : diff;
                    }
                    else
                    {
                        if (!has || v.Node == null) continue;
                        if (channel == "rotation")
                        {
                            rel = M.Mul(v.Node, M.Num(180.0 / Math.PI));
                            if (!v.Vanilla) rel = M.Add(rel, M.Num(GeometryOut.Deg(bind)), -1); // Molang adds to the geometry's rotation
                        }
                        else
                        {
                            rel = v.Vanilla ? v.Node : M.Add(v.Node, M.Num(DefaultOf(bone, prop)), -1);
                            if (axis == 1) rel = M.Neg(rel); // Java y points down
                        }
                    }
                    // (a constant below the files' 4 decimals: float rounding between the mesh and setupAnim)
                    double num;
                    if (M.IsNum(rel, out num) && Math.Abs(num) < 5e-5) continue;
                    Dictionary<string, MNode[]> ch;
                    if (!res.Bones.TryGetValue(bone.Name, out ch)) res.Bones[bone.Name] = ch = new Dictionary<string, MNode[]>();
                    MNode[] axes;
                    if (!ch.TryGetValue(channel, out axes)) ch[channel] = axes = new MNode[3];
                    axes[axis] = rel;
                }
            }
            foreach (var kv in s.Overrides) res.OverridesVanilla[Label(ctx, kv.Key)] = kv.Value;
            foreach (var kv in s.FixedUsed)
                res.Notes.Add("catalog baseCase: " + kv.Key.Expr + " is " + kv.Key.ValueText + " on every frame (" + kv.Key.Why + "); the base animation uses that value at line"
                              + (kv.Value.Count > 1 ? "s " : " ") + string.Join(", ", kv.Value) + " (no case entry)");
            if (res.Statements.Any(x => x.Case))
                res.Notes.Add("the base animation is the case in which every condition that does not translate is false (a negated one true), entity state catalog.json's baseCase lists takes the value it has in upstream's common state, and a branch that returns early is not taken; the todo entries with 'affects' name those conditions and the channels that differ when they hold");
        }

        // The end of a method: what it set after a condition whose other branch returns early differs in that case.
        static void CloseExits(Ctx c)
        {
            if (c.Exits.Count == 0) return;
            var changed = new List<KeyValuePair<Exit, List<string>>>();
            foreach (var ex in c.Exits)
            {
                var keys = new List<string>();
                foreach (var kv in c.Cur)
                {
                    Val old;
                    if (!ex.Cur.TryGetValue(kv.Key, out old) || !ReferenceEquals(old, kv.Value)) keys.Add(kv.Key);
                }
                changed.Add(new KeyValuePair<Exit, List<string>>(ex, keys));
            }
            foreach (var kv in changed)
            {
                var tags = new HashSet<AnimStatement> { kv.Key.St };
                foreach (var k in kv.Value) TagKey(c, k, tags);
            }
            c.Exits.Clear();
        }

        // ---- statements -------------------------------------------------------------------------------------

        AnimStatement NewSt(Ctx c, Stmt s)
        {
            if (c.Proxy != null) return c.Proxy;
            var st = new AnimStatement { Java = SourceText(c.Unit, s), Line = s.Line, Class = c.Label, Translated = true };
            c.S.Res.Statements.Add(st);
            return st;
        }

        static void Fail(AnimStatement st, string why)
        {
            st.Translated = false;
            if (st.Reason == null) st.Reason = why;
            else if (!st.Reason.Contains(why)) st.Reason += "; " + why;
        }

        // The statement relies on conditions counted as false or on base-case values: a case entry.
        static void Assume(AnimStatement st, Deps d)
        {
            if (!d.Assumes) return;
            st.Case = true;
            Fail(st, "the base animation counts " + d.Text());
            d.When.Add(st);
        }

        // Assignments and ++ / -- inside an expression (a condition, an initializer, an argument, a value).
        // One that runs whenever the expression does (not behind && / || / ?: or in a lambda) and whose target
        // the expression does not read elsewhere runs first, like a statement: the expression then reads its
        // result. Any other makes what it changes unknown afterwards.
        void ApplyNested(Expr e, Ctx c, AnimStatement st, string where)
        {
            if (e == null) return;
            var run = new List<Expr>();
            var poison = new List<Expr>();
            CollectNested(e, true, run, poison);
            run.RemoveAll(x => c.Applied.Contains(x));      // already run (an argument that is also a value: setPos)
            poison.RemoveAll(x => c.Applied.Contains(x));
            // calls inside the expression that change parts (an upstream method, a part method) are not run: unknown
            var called = new HashSet<string>(StringComparer.Ordinal);
            Touched(new[] { e }, c, called, new HashSet<string>(StringComparer.Ordinal), 0, true);
            called.RemoveWhere(k => !c.S.Bones.ContainsKey(k.Substring(0, k.LastIndexOf('.'))));
            if (called.Count > 0)
            {
                Fail(st, "a call inside " + where + " may change " + string.Join(", ", called.OrderBy(k => k, StringComparer.Ordinal).Select(k => Label(c, k))) + " (not translated)");
                foreach (var k in called.OrderBy(k => k, StringComparer.Ordinal)) SetKey(c, k, Val.Unknown(Label(c, k) + " may be changed by a call inside an expression at line " + e.Line, e.Line));
            }
            if (run.Count == 0 && poison.Count == 0) return;
            foreach (var a in run.ToList())
            {
                // Java evaluates left to right: a read of the target elsewhere in the expression may see the old value
                var target = a is AssignExpr ae ? ae.Target : ((UnaryExpr)a).Operand;
                string t = JavaText(target);
                if (Walk(e).Count(x => (x is FieldExpr || x is NameExpr) && JavaText(x) == t) > 1) { run.Remove(a); poison.Add(a); }
            }
            foreach (var a in run)
            {
                ExecEffect(a, c, st);
                c.Applied.Add(a);
            }
            if (poison.Count > 0)
            {
                Fail(st, "an assignment inside " + where + " (" + JavaText(poison[0]) + ") that does not always run or whose target the expression also reads: what it changes is unknown afterwards");
                PoisonTouched(poison, c, "is changed inside an expression at line " + e.Line, e.Line);
            }
        }

        // The assignments inside e in evaluation order: run (evaluated whenever e is) or poison (behind && / || /
        // ?:, in a lambda, a switch expression or an object creation).
        static void CollectNested(Expr e, bool always, List<Expr> run, List<Expr> poison)
        {
            switch (e)
            {
                case null: return;
                case AssignExpr _: (always ? run : poison).Add(e); return; // its own target and value: Assign
                case UnaryExpr u:
                    if (u.Op == "++" || u.Op == "--") { (always ? run : poison).Add(e); return; }
                    CollectNested(u.Operand, always, run, poison);
                    return;
                case BinaryExpr b:
                    CollectNested(b.Left, always, run, poison);
                    CollectNested(b.Right, always && b.Op != "&&" && b.Op != "||", run, poison);
                    return;
                case CondExpr ce:
                    CollectNested(ce.Cond, always, run, poison);
                    CollectNested(ce.Then, false, run, poison);
                    CollectNested(ce.Else, false, run, poison);
                    return;
                case CastExpr ca: CollectNested(ca.Operand, always, run, poison); return;
                case InstanceOfExpr io: CollectNested(io.Operand, always, run, poison); return;
                case FieldExpr f: CollectNested(f.Target, always, run, poison); return;
                case IndexExpr ix: CollectNested(ix.Target, always, run, poison); CollectNested(ix.Index, always, run, poison); return;
                case CallExpr call:
                    CollectNested(call.Target, always, run, poison);
                    foreach (var a in call.Args) CollectNested(a, always, run, poison);
                    return;
                case LitExpr _: case NameExpr _: case ThisExpr _: case SuperExpr _: case ClassLitExpr _: case MethodRefExpr _: return;
            }
            // lambdas, switch expressions, object and array creation
            foreach (var x in Walk(e)) if (!ReferenceEquals(x, e) && (x is AssignExpr || (x is UnaryExpr u2 && (u2.Op == "++" || u2.Op == "--")))) poison.Add(x);
        }

        void Exec(Stmt s, Ctx c)
        {
            if (s == null || s is EmptyStmt) return;
            if (s is BlockStmt blk) { foreach (var x in blk.Stmts) Exec(x, c); return; }
            if (c.Returned) { Fail(NewSt(c, s), "not reached on the base path (after a return)"); return; }
            switch (s)
            {
                case LocalVarStmt lv: ExecLocal(lv, c); return;
                case ExprStmt es: ExecEffect(es.E, c, NewSt(c, s)); return;
                case IfStmt i: ExecIf(i, c); return;
                case ReturnStmt r:
                    if (r.Value != null) Fail(NewSt(c, s), "return value");
                    c.Returned = true;
                    return;
            }
            // loops, switches, try: everything they may change is unknown afterwards
            var st = NewSt(c, s);
            string kind = s is ForStmt || s is ForEachStmt || s is WhileStmt ? "a loop" : s is SwitchStmt ? "a switch" : "a " + s.GetType().Name.Replace("Stmt", "").ToLowerInvariant() + " statement";
            Fail(st, "inside " + kind + " (not translated)");
            PoisonTouched(Exprs(s), c, "changed by " + kind + " at line " + s.Line, s.Line);
        }

        void ExecLocal(LocalVarStmt lv, Ctx c)
        {
            var st = NewSt(c, lv);
            string type = lv.Type != null ? lv.Type.Simple : null;
            bool isBool = type == "boolean" && lv.Type.Dims == 0;
            foreach (var v in lv.Vars)
            {
                c.Fields.Remove(v.Key);
                c.PartLocals.Remove(v.Key);
                c.EntityNames.Remove(v.Key);
                if (isBool) c.BoolLocals.Add(v.Key); else c.BoolLocals.Remove(v.Key);
                ApplyNested(v.Value, c, st, "the initializer of '" + v.Key + "'");
                if (type == "ModelPart" && lv.Type.Dims == 0)
                {
                    // a part local: an alias of the part, or one of the candidates of an unknown one
                    string path;
                    c.Env.Remove(v.Key);
                    if (v.Value != null && PartOf(v.Value, c.Fields, out path)) c.Fields[v.Key] = path;
                    else if (v.Value is CallExpr ga && ga.Name == "getArm" && (ga.Target == null || ga.Target is ThisExpr) && c.S.Bones.ContainsKey("right_arm"))
                    {
                        // HumanoidModel.getArm(arm): the arm the argument names (a base-case value), else the right arm
                        var arm = new Deps();
                        string armWhy;
                        var side = ga.Args.Count == 1 ? Tr(ga.Args[0], c, arm, out armWhy) as MSym : null;
                        string part = side != null && side.Name == "HumanoidArm.LEFT" ? "left_arm" : side != null && side.Name == "HumanoidArm.RIGHT" ? "right_arm" : null;
                        if (part == null || !c.S.Bones.ContainsKey(part))
                        {
                            part = "right_arm";
                            arm = new Deps();
                            arm.Assumed.Add(JavaText(v.Value) + " == this.leftArm");
                        }
                        c.Fields[v.Key] = part;
                        Assume(st, arm);
                        // what the alias changes differs in the cases the arm depends on
                        var tags = new HashSet<AnimStatement>(arm.When);
                        if (tags.Count > 0)
                            foreach (var p in new[] { "right_arm", "left_arm" }) foreach (var prop in PoseProps) TagKey(c, p + "." + prop, tags);
                    }
                    else c.PartLocals[v.Key] = Candidates(v.Value, c);
                    continue;
                }
                if (v.Value == null) { c.Env[v.Key] = Val.Unknown("local '" + v.Key + "' has no value yet", lv.Line); continue; }
                if (v.Value is NameExpr en && c.EntityNames.Contains(en.Name))
                {
                    // another name for the entity
                    Val tick;
                    c.EntityNames.Add(v.Key);
                    c.Env.Remove(v.Key);
                    if (c.Env.TryGetValue(en.Name + ".tickCount", out tick)) c.Env[v.Key + ".tickCount"] = tick;
                    continue;
                }
                var d = new Deps();
                string why = null;
                MNode n = isBool ? TrBool(v.Value, c, d) : Tr(v.Value, c, d, out why);
                if (n == null)
                {
                    Fail(st, "local '" + v.Key + "': " + why);
                    c.Env[v.Key] = Val.Unknown("local '" + v.Key + "' (line " + lv.Line + ") is not translated", lv.Line);
                    continue;
                }
                Assume(st, d);
                d.From.Add(st);
                c.Env[v.Key] = new Val { Node = n, From = d.From, When = d.When };
            }
        }

        void ExecEffect(Expr e, Ctx c, AnimStatement st)
        {
            switch (e)
            {
                case AssignExpr a:
                    if (a.Value is AssignExpr inner)
                    {
                        // a = b = v: b first, then a from b
                        ExecEffect(inner, c, st);
                        Assign(a.Target, a.Op, inner.Target, c, st, a.Line);
                        return;
                    }
                    Assign(a.Target, a.Op, a.Value, c, st, a.Line);
                    return;
                case UnaryExpr u:
                    if (u.Op == "++" || u.Op == "--") { Assign(u.Operand, u.Op == "++" ? "+=" : "-=", new LitExpr { Value = 1, Line = u.Line }, c, st, u.Line); return; }
                    break;
                case CallExpr call: ExecCall(call, c, st); return;
            }
            Fail(st, "expression statement");
            PoisonTouched(new[] { e }, c, "changed at line " + e.Line, e.Line);
        }

        // target op= value on the base path.
        void Assign(Expr target, string op, Expr value, Ctx c, AnimStatement st, int line)
        {
            ApplyNested(value, c, st, "the assigned value");
            if (target is FieldExpr gloaminge) ApplyNested(gloaminge.Target, c, st, "the assigned part");
            if (target is IndexExpr tix) ApplyNested(tix, c, st, "the assigned element");
            if (target is NameExpr ln && c.Env.ContainsKey(ln.Name))
            {
                // a local or a parameter
                var d = new Deps();
                string why = null;
                MNode rhs = c.BoolLocals.Contains(ln.Name) && op == "=" ? TrBool(value, c, d) : Tr(value, c, d, out why);
                var nv = Combine(c.Env[ln.Name], op, rhs, d, why, "local '" + ln.Name + "'", st, line, c);
                if (nv.Node == null) Fail(st, nv.Why);
                c.Env[ln.Name] = nv;
                return;
            }
            string path, prop;
            if (BoneProp(target, c.Fields, out path, out prop))
            {
                if (!IsPoseProp(prop)) { Fail(st, "property '" + prop + "' (visibility / scale): use part_visibility or a scale channel"); return; }
                if (path.Length == 0) { Fail(st, "the model root has no bone in the geometry: turn / move the whole entity (or a wrapper bone)"); return; }
                if (!c.S.Bones.ContainsKey(path)) { Fail(st, "no bone for part '" + path + "'"); return; }
                string vpath, vprop;
                Val src;
                if (op == "=" && BoneProp(value, c.Fields, out vpath, out vprop) && IsPoseProp(vprop) && c.S.Bones.ContainsKey(vpath)
                    && (src = Get(c, vpath + "." + vprop)).Vanilla && src.Node != null)
                {
                    // a = b where b follows the vanilla pose: a follows it the same way, when the vanilla
                    // animations drive a too (else the vanilla part of the value is not in any animation)
                    SetKey(c, path + "." + prop, VanillaCopy(c, src, path + "." + prop, st, line));
                    if (c.Cur[path + "." + prop].Node == null) Fail(st, c.Cur[path + "." + prop].Why);
                    return;
                }
                var d = new Deps();
                string why = null;
                MNode rhs = Tr(value, c, d, out why);
                if (rhs is MSym sym) { why = sym.Name + " is not a number"; rhs = null; }
                AssignKey(c, path + "." + prop, op, rhs, d, why, st, line);
                return;
            }
            if (target is FieldExpr upsrc && IsPoseProp(upsrc.Name))
            {
                Fail(st, "part '" + JavaText(upsrc.Target) + "' is not resolved (an array element or a part chosen at run time)");
                foreach (var p in Candidates(upsrc.Target, c))
                    SetKey(c, p + "." + upsrc.Name, Val.Unknown(PartName(c, p) + "." + upsrc.Name + " may be changed through '" + JavaText(upsrc.Target) + "' at line " + line, line));
                return;
            }
            if (target is FieldExpr vis && (vis.Name == "visible" || vis.Name == "skipDraw" || vis.Name.EndsWith("Scale", StringComparison.Ordinal)))
            {
                Fail(st, "property '" + vis.Name + "' (visibility / scale) of part '" + JavaText(vis.Target) + "' (not resolved): use part_visibility or a scale channel");
                return;
            }
            Fail(st, "not a bone assignment (" + JavaText(target) + ": entity or model state)");
        }

        void AssignKey(Ctx c, string key, string op, MNode rhs, Deps d, string why, AnimStatement st, int line)
        {
            NoteOverride(c, key, op, line);
            var nv = Combine(Get(c, key), op, rhs, d, why, Label(c, key), st, line, c);
            if (nv.Node == null) Fail(st, nv.Why);
            SetKey(c, key, nv);
        }

        static void NoteOverride(Ctx c, string key, string op, int line)
        {
            if (op != "=") return;
            int dot = key.LastIndexOf('.');
            if (!(Get(c, key).Vanilla || (c.S.VanillaCalled && c.S.VanillaParts.Contains(key.Substring(0, dot))))) return;
            SortedSet<int> lines;
            if (!c.S.Overrides.TryGetValue(key, out lines)) c.S.Overrides[key] = lines = new SortedSet<int>();
            lines.Add(line);
        }

        // old op= rhs (rhs null: not translated, why).
        static Val Combine(Val old, string op, MNode rhs, Deps d, string why, string label, AnimStatement st, int line, Ctx c)
        {
            if (op != "=" && old.Node == null) return Val.Unknown("previous value at line " + old.At + " not translated", old.At);
            if (rhs == null) return Val.Unknown(label + " (line " + line + ") is not translated: " + why, line);
            if (op != "=" && (old.Node is MSym || rhs is MSym)) return Val.Unknown(label + " (line " + line + "): operator " + op + " on " + (old.Node is MSym ? old.Node : rhs).Emit() + ", not a number", line);
            Assume(st, d);
            var from = new HashSet<AnimStatement>(d.From) { st };
            var when = new HashSet<AnimStatement>(d.When);
            if (op == "=") return new Val { Node = rhs, From = from, When = when };
            from.UnionWith(old.From);
            when.UnionWith(old.When);
            MNode val;
            switch (op)
            {
                case "+=": val = M.Add(old.Node, rhs); break;
                case "-=": val = M.Add(old.Node, rhs, -1); break;
                case "*=":
                case "/=":
                    if (old.Vanilla) return Val.Unknown(label + " (line " + line + ") needs the value the vanilla " + c.S.Vanilla + ".setupAnim gives", line);
                    if (op == "/=" && IsZero(rhs)) return Val.Unknown(label + " (line " + line + "): division by zero", line);
                    val = op == "*=" ? M.Mul(old.Node, rhs) : M.Mul(old.Node, rhs, true);
                    break;
                default: return Val.Unknown(label + " (line " + line + "): operator " + op, line);
            }
            return new Val { Node = val, Vanilla = old.Vanilla, From = from, When = when };
        }

        // Sets a bone property; a value that does not translate takes away the statements the old one carried.
        static void SetKey(Ctx c, string key, Val nv)
        {
            Val old;
            if (nv.Node == null && c.Cur.TryGetValue(key, out old)) Lose(c, old, Label(c, key) + ": " + nv.Why);
            c.Cur[key] = nv;
        }

        static void Lose(Ctx c, Val v, string why)
        {
            if (v.Node == null) return;
            foreach (var f in v.From) if (!c.S.Lost.ContainsKey(f)) c.S.Lost[f] = why;
        }

        static bool IsZero(MNode n) { double v; return M.IsNum(n, out v) && v == 0; }

        void ExecIf(IfStmt i, Ctx c)
        {
            var st = NewSt(c, i);
            // an assignment inside the condition: what it changes is unknown afterwards
            ApplyNested(i.Cond, c, st, "the condition");
            var d = new Deps();
            MNode cond = TrBool(i.Cond, c, d);
            double cv;
            bool constant = M.IsNum(cond, out cv);
            // a condition that is constant without counting anything as false: the other branch never runs
            if (constant && !d.Assumes && d.When.Count == 0) { Exec(cv != 0 ? i.Then : i.Else, c); return; }
            // A branch that always leaves the method early: the base path takes the other one, whatever the
            // condition counted as false (else `if (!entity.isAlive()) return;` would end the base path).
            bool thenExits = AlwaysJumps(i.Then), elseExits = AlwaysJumps(i.Else);
            string exits = null;
            if (thenExits != elseExits)
            {
                bool takeThen = elseExits;
                if (!constant || (cv != 0) != takeThen)
                {
                    d.Assumed.Clear();
                    d.Valued.Clear();
                    d.Assumed.Add(takeThen ? "!(" + JavaText(i.Cond) + ")" : JavaText(i.Cond));
                    cond = M.Num(takeThen ? 1 : 0);
                    cv = takeThen ? 1 : 0;
                    constant = true;
                }
                exits = "the " + (takeThen ? "else" : "if") + " branch returns early";
            }
            else if (!constant && thenExits)
            {
                // both branches return: no ?: over them
                d.Assumed.Add(JavaText(i.Cond));
                cond = M.Num(0);
                cv = 0;
                constant = true;
            }
            if (constant) { ExecCase(i, c, st, d, cv != 0, exits); return; }
            // a Molang condition: both branches, merged with ?:
            Assume(st, d);
            d.From.Add(st);
            var before = new Dictionary<string, Val>(c.Cur, StringComparer.Ordinal);
            var ct = c.Fork();
            Exec(i.Then, ct);
            var ce = c.Fork();
            if (i.Else != null) Exec(i.Else, ce);
            if (ct.Returned != ce.Returned)
            {
                // one branch returned through a constant condition inside it: the base path continues with
                // the other one, and the condition is a case for everything after it
                var keep = ct.Returned ? ce : ct;
                st.Case = true;
                Fail(st, "the base animation counts " + (ct.Returned ? JavaText(i.Cond) : "!(" + JavaText(i.Cond) + ")") + " as false (the "
                         + (ct.Returned ? "if" : "else") + " branch returns early)");
                d.When.Add(st);
                foreach (var kv in keep.Cur.ToList()) { Val old; if (!c.Cur.TryGetValue(kv.Key, out old) || !ReferenceEquals(old, kv.Value)) SetKey(c, kv.Key, kv.Value); }
                foreach (var kv in keep.Env.ToList()) if (c.Env.ContainsKey(kv.Key)) c.Env[kv.Key] = kv.Value;
                c.Exits.Add(new Exit { St = st, Cur = before });
                TagTouched(Exprs(i.Then).Concat(Exprs(i.Else)), c, new HashSet<AnimStatement>(d.When));
                return;
            }
            if (ct.Returned) c.Returned = true;
            foreach (var key in ct.Cur.Keys.Union(ce.Cur.Keys).ToList())
            {
                Val t, e;
                ct.Cur.TryGetValue(key, out t);
                ce.Cur.TryGetValue(key, out e);
                if (ReferenceEquals(t, e)) continue;
                if (t == null) t = Get(ct, key);
                if (e == null) e = Get(ce, key);
                SetKey(c, key, Merge(cond, t, e, d, Label(c, key), i, st, c));
            }
            foreach (var name in c.Env.Keys.ToList())
            {
                Val t, e;
                if (!ct.Env.TryGetValue(name, out t)) t = Val.Unknown("local '" + name + "' is not a number after line " + i.Line, i.Line);
                if (!ce.Env.TryGetValue(name, out e)) e = Val.Unknown("local '" + name + "' is not a number after line " + i.Line, i.Line);
                if (ReferenceEquals(t, e)) continue;
                c.Env[name] = Merge(cond, t, e, d, "local '" + name + "'", i, st, c);
            }
        }

        // The base animation runs one branch of a condition that does not translate (or is constant through
        // the cases); the other is a case for the owning unit.
        void ExecCase(IfStmt i, Ctx c, AnimStatement st, Deps d, bool takeThen, string exits)
        {
            Stmt taken = takeThen ? i.Then : i.Else, other = takeThen ? i.Else : i.Then;
            Assume(st, d);
            st.Case = true;
            if (!d.Assumes)
                Fail(st, "the condition is constant on the base path (it uses values of the cases at lines " + string.Join(", ", d.When.Where(w => w != st).Select(w => w.Line).Distinct().OrderBy(x => x)) + ")");
            Fail(st, "so it runs " + (taken == null ? "neither branch" : "the " + (takeThen ? "if" : "else") + " branch")
                     + (other != null ? " and leaves out lines " + other.Line + "-" + LastLine(other) : ""));
            // the other branch may leave the method: then everything the rest of the method sets differs too
            if (Jumps(other))
            {
                Fail(st, (exits ?? "the " + (takeThen ? "else" : "if") + " branch may return early") + ", so what the rest of the method sets differs in this case too");
                c.Exits.Add(new Exit { St = st, Cur = new Dictionary<string, Val>(c.Cur, StringComparer.Ordinal) });
            }
            st.EndLine = Math.Max(LastLine(i.Then), LastLine(i.Else));
            var tags = new HashSet<AnimStatement>(d.When) { st };
            Exec(taken, c);
            // what either branch changes differs when the condition holds (what the condition itself changes
            // runs on every path: ApplyNested ran or invalidated it before the condition was read)
            TagTouched(Exprs(i.Then).Concat(Exprs(i.Else)), c, tags);
            if (other != null) foreach (var e in Exprs(other)) foreach (var x in Walk(e)) NoteBranchOverride(x, c);
        }

        // Both branches of a Molang condition (cond ? then : else).
        static Val Merge(MNode cond, Val t, Val e, Deps d, string label, IfStmt i, AnimStatement st, Ctx c)
        {
            if (t.Node == null) { Lose(c, e, label + ": " + t.Why); return t; }
            if (e.Node == null) { Lose(c, t, label + ": " + e.Why); return e; }
            if (t.Vanilla != e.Vanilla)
            {
                // One branch sets the axis, the other keeps the vanilla pose, which a Molang ?: cannot pick:
                // the base animation takes the branch that sets it (overrides_vanilla lists the axis, so the
                // vanilla animations do not drive it) and the condition is a case for the other branch.
                bool thenSets = !t.Vanilla;
                Val keep = thenSets ? t : e, drop = thenSets ? e : t;
                st.Case = true;
                Fail(st, label + ": the base animation takes the " + (thenSets ? "if" : "else") + " branch, which sets it"
                         + (thenSets && HoldsAfterFirstFrame(cond) ? " (" + cond.Emit() + " holds on every frame after the first)" : "")
                         + "; the " + (thenSets ? "else" : "if") + " branch keeps the vanilla pose there");
                Lose(c, drop, label + ": the base animation takes the " + (thenSets ? "if" : "else") + " branch of line " + i.Line);
                var k = new Val { Node = keep.Node, From = new HashSet<AnimStatement>(keep.From), When = new HashSet<AnimStatement>(keep.When) };
                k.From.UnionWith(d.From);
                k.When.UnionWith(d.When);
                k.When.Add(st);
                return k;
            }
            var v = new Val { Node = M.Cond(cond, t.Node, e.Node), Vanilla = t.Vanilla, From = new HashSet<AnimStatement>(t.From), When = new HashSet<AnimStatement>(t.When) };
            v.From.UnionWith(e.From); v.From.UnionWith(d.From);
            v.When.UnionWith(e.When); v.When.UnionWith(d.When);
            return v;
        }

        // query.life_time [* k] > 0 (entity.tickCount > 0): true on every frame but the first.
        static bool HoldsAfterFirstFrame(MNode cond)
        {
            var cmp = cond as MCmp;
            double r, k;
            if (cmp == null || cmp.Op != ">" || !M.IsNum(cmp.R, out r) || r != 0) return false;
            if (cmp.L is MAtom a) return a.Text == "query.life_time";
            var p = cmp.L as MProd;
            return p != null && p.Factors.Count == 2 && !p.Factors[0].Key && !p.Factors[1].Key && p.Factors[0].Value is MAtom pa && pa.Text == "query.life_time"
                   && M.IsNum(p.Factors[1].Value, out k) && k > 0;
        }

        // '=' to a vanilla-driven bone axis inside a branch the base animation leaves out.
        static void NoteBranchOverride(Expr e, Ctx c)
        {
            string path, prop;
            if (e is AssignExpr a)
            {
                if (BoneProp(a.Target, c.Fields, out path, out prop) && IsPoseProp(prop) && c.S.Bones.ContainsKey(path)) NoteOverride(c, path + "." + prop, a.Op, a.Line);
                return;
            }
            // the part calls that set a pose: setPos / setRotation / copyFrom / resetPose
            var call = e as CallExpr;
            if (call == null || !PartOf(call.Target, c.Fields, out path) || !c.S.Bones.ContainsKey(path)) return;
            string[] props = call.Name == "setPos" ? new[] { "x", "y", "z" } : call.Name == "setRotation" ? new[] { "xRot", "yRot", "zRot" }
                           : call.Name == "copyFrom" || call.Name == "resetPose" ? PoseProps : null;
            if (props != null) foreach (var p in props) NoteOverride(c, path + "." + p, "=", call.Line);
        }

        // A copy of a vanilla-driven value onto key: it follows the vanilla pose only where the vanilla
        // animations drive the target bone too.
        static Val VanillaCopy(Ctx c, Val src, string key, AnimStatement st, int line)
        {
            if (!c.S.VanillaParts.Contains(key.Substring(0, key.LastIndexOf('.'))))
                return Val.Unknown(Label(c, key) + " (line " + line + ") copies the vanilla " + c.S.Vanilla + ".setupAnim pose onto a bone the vanilla animations do not drive", line);
            return new Val { Node = src.Node, Vanilla = true, From = new HashSet<AnimStatement>(src.From) { st }, When = src.When };
        }

        void ExecCall(CallExpr call, Ctx c, AnimStatement st)
        {
            string name = call.Name;
            foreach (var a in call.Args) ApplyNested(a, c, st, "an argument of " + name + "()");
            if (call.Target is SuperExpr)
            {
                var sup = c.Owner.Extends != null ? cp.Find(c.Owner.Extends, c.Owner) : null;
                var md = sup != null ? FindMethod(sup, name, call.Args.Count) : null;
                if (md != null) { Inline(md, call, c, st, false); return; }
                if (name == "setupAnim" && c.S.VanillaParts.Count > 0)
                {
                    // the vanilla pose: every value of a vanilla part after it is relative to it
                    c.S.VanillaCalled = true;
                    c.S.VanillaLine = call.Line;
                    foreach (var p in c.S.VanillaParts) foreach (var prop in PoseProps) c.Cur[p + "." + prop] = new Val { Node = M.Num(0), Vanilla = true };
                }
                Fail(st, "the vanilla " + (c.S.Vanilla ?? "super class") + "." + name + " (see the notes)");
                if (name == "setupAnim")
                {
                    // arguments the model changed before the call: the vanilla Bedrock animations read the
                    // unchanged queries, so the owning unit has to feed them the changed values
                    string[] names = { "entity", "limbSwing", "limbSwingAmount", "ageInTicks", "netHeadYaw", "headPitch" };
                    for (int k = 1; k < call.Args.Count && k < names.Length; k++)
                    {
                        string why;
                        var arg = Tr(call.Args[k], c, new Deps(), out why);
                        if (arg != null && M.Same(arg, ParamNode(k))) continue;
                        Fail(st, "it passes " + names[k] + " = " + (arg != null ? arg.Emit() : JavaText(call.Args[k]) + " (" + why + ")")
                                 + " where the vanilla animations read " + ParamNode(k).Emit());
                    }
                }
                return;
            }
            if (call.Target == null || call.Target is ThisExpr)
            {
                var md = FindMethod(c.Owner, name, call.Args.Count);
                if (md != null) { Inline(md, call, c, st, ReferenceEquals(c.Owner, VanillaHelpers)); return; }
                if ((name == "poseRightArm" || name == "poseLeftArm") && call.Args.Count == 1 && IsHumanoid(c.S.Vanilla))
                {
                    // the vanilla HumanoidModel.poseRightArm / poseLeftArm: ArmPose.EMPTY (no item) sets yRot = 0
                    bool right = name == "poseRightArm";
                    string arm = right ? "right_arm" : "left_arm";
                    var d = new Deps();
                    string why;
                    var pose = Tr(new FieldExpr { Target = new ThisExpr { Line = call.Line }, Name = right ? "rightArmPose" : "leftArmPose", Line = call.Line }, c, d, out why) as MSym;
                    if (pose != null && pose.Name != "ArmPose.EMPTY")
                    {
                        // another arm pose (a base-case value): the vanilla pose method is not translated
                        Fail(st, "the vanilla HumanoidModel." + name + " with " + pose.Name + " (not translated)");
                        PoisonTouched(new Expr[] { call }, c, name + "() at line " + call.Line, call.Line);
                        return;
                    }
                    if (pose == null)
                    {
                        d = new Deps();
                        d.Assumed.Add("this." + (right ? "right" : "left") + "ArmPose != ArmPose.EMPTY (with EMPTY the vanilla HumanoidModel." + name + " sets " + (right ? "rightArm" : "leftArm") + ".yRot = 0)");
                    }
                    Assume(st, d);
                    if (c.S.Bones.ContainsKey(arm)) AssignKey(c, arm + ".yRot", "=", M.Num(0), d, null, st, call.Line);
                    if (st.Case)
                    {
                        var tags = new HashSet<AnimStatement> { st };
                        foreach (var p in new[] { "right_arm", "left_arm" }) foreach (var prop in PoseProps) TagKey(c, p + "." + prop, tags);
                    }
                    return;
                }
                Fail(st, "call " + name + "() (a vanilla method)");
                PoisonTouched(new Expr[] { call }, c, name + "() at line " + call.Line, call.Line);
                return;
            }
            if (call.Target is NameExpr un && un.Name == "AnimationUtils")
            {
                var md = VanillaHelpers.Methods.FirstOrDefault(x => x.Name == name && x.Params.Count == call.Args.Count);
                if (md != null) { Inline(md, call, c, st, true); return; }
            }
            string path;
            if (PartOf(call.Target, c.Fields, out path) && c.S.Bones.ContainsKey(path))
            {
                var part = call.Target;
                switch (name)
                {
                    case "copyFrom":
                    {
                        string src;
                        if (call.Args.Count == 1 && PartOf(call.Args[0], c.Fields, out src) && c.S.Bones.ContainsKey(src))
                        {
                            // every property of the source: its value so far or its initial pose
                            foreach (var prop in PoseProps)
                            {
                                var v = Get(c, src + "." + prop);
                                if (v.Node == null)
                                {
                                    Fail(st, Label(c, src + "." + prop) + " is not translated (line " + v.At + ")");
                                    SetKey(c, path + "." + prop, Val.Unknown(Label(c, path + "." + prop) + " copies an untranslated value (line " + call.Line + ")", call.Line));
                                    continue;
                                }
                                NoteOverride(c, path + "." + prop, v.Vanilla ? "copy" : "=", call.Line);
                                var copied = v.Vanilla ? VanillaCopy(c, v, path + "." + prop, st, call.Line)
                                                       : new Val { Node = v.Node, From = new HashSet<AnimStatement>(v.From) { st }, When = v.When };
                                if (copied.Node == null) Fail(st, copied.Why);
                                SetKey(c, path + "." + prop, copied);
                            }
                            return;
                        }
                        break;
                    }
                    case "setPos":
                    case "setRotation":
                        if (call.Args.Count == 3)
                        {
                            var props = name == "setPos" ? new[] { "x", "y", "z" } : new[] { "xRot", "yRot", "zRot" };
                            for (int k = 0; k < 3; k++) Assign(new FieldExpr { Target = part, Name = props[k], Line = call.Line }, "=", call.Args[k], c, st, call.Line);
                            return;
                        }
                        break;
                    case "resetPose":
                        if (call.Args.Count == 0) { ResetPose(c, new List<string> { path }, st, call.Line); return; }
                        break;
                }
                Fail(st, "call " + name + "() on part '" + JavaText(part) + "'");
                PoisonTouched(new Expr[] { call }, c, name + "() at line " + call.Line, call.Line);
                return;
            }
            // root().getAllParts().forEach(ModelPart::resetPose)
            if (name == "forEach" && call.Args.Count == 1 && call.Args[0] is MethodRefExpr mr && mr.Name == "resetPose" && call.Target is CallExpr gp && gp.Name == "getAllParts")
            {
                var under = PartsUnder(gp.Target, c);
                if (under != null) { ResetPose(c, under, st, call.Line); return; }
            }
            Fail(st, "call " + JavaText(call.Target) + "." + name + "()");
            PoisonTouched(new Expr[] { call }, c, name + "() at line " + call.Line, call.Line);
        }

        static void ResetPose(Ctx c, List<string> paths, AnimStatement st, int line)
        {
            foreach (var p in paths)
            {
                BoneOut b;
                if (!c.S.Bones.TryGetValue(p, out b)) continue;
                foreach (var prop in PoseProps)
                {
                    NoteOverride(c, p + "." + prop, "=", line);
                    SetKey(c, p + "." + prop, new Val { Node = M.Num(DefaultOf(b, prop)), From = new HashSet<AnimStatement> { st } });
                }
            }
        }

        // Runs an upstream method (or a vanilla helper) with its arguments: part arguments become aliases.
        void Inline(MethodDecl md, CallExpr call, Ctx c, AnimStatement st, bool proxy)
        {
            if (c.Depth >= 8)
            {
                Fail(st, "calls nested too deep");
                PoisonTouched(new Expr[] { call }, c, md.Name + "() at line " + call.Line, call.Line);
                return;
            }
            BlockStmt body;
            try { body = Parser.ParseBody(md.Owner.Unit, md); }
            catch (JavaSyntaxException ex)
            {
                Fail(st, "could not parse " + md.Owner.Name + "." + md.Name + ": " + ex.Message);
                PoisonTouched(new Expr[] { call }, c, md.Name + "() at line " + call.Line, call.Line);
                return;
            }
            var f = new Ctx
            {
                S = c.S, Env = new Dictionary<string, Val>(StringComparer.Ordinal), Cur = c.Cur,
                Fields = md.IsStatic ? new Dictionary<string, string>(StringComparer.Ordinal) : new Dictionary<string, string>(c.S.FieldMap, StringComparer.Ordinal),
                Unit = md.Owner.Unit, Owner = md.Owner, Label = proxy ? c.Label : (md.Owner != c.S.Model ? md.Owner.Name : null),
                Proxy = proxy ? st : c.Proxy, Depth = c.Depth + 1,
            };
            for (int k = 0; k < md.Params.Count; k++)
            {
                var p = md.Params[k];
                var a = k < call.Args.Count ? call.Args[k] : null;
                string pp;
                f.Fields.Remove(p.Name);
                if (a != null && PartOf(a, c.Fields, out pp)) { f.Fields[p.Name] = pp; continue; }
                if (p.Type != null && p.Type.Simple == "ModelPart") { f.PartLocals[p.Name] = Candidates(a, c); continue; }
                Val tv;
                if (a is NameExpr an && c.Env.TryGetValue(an.Name + ".tickCount", out tv)) f.Env[p.Name + ".tickCount"] = tv;
                if (a is NameExpr ae && c.EntityNames.Contains(ae.Name)) { f.EntityNames.Add(p.Name); continue; }
                bool isBool = p.Type != null && p.Type.Simple == "boolean";
                if (isBool) f.BoolLocals.Add(p.Name);
                if (a == null) { f.Env[p.Name] = Val.Unknown("argument '" + p.Name + "' of " + md.Name + "() is missing", call.Line); continue; }
                var d = new Deps();
                string why = null;
                MNode n = isBool ? TrBool(a, c, d) : Tr(a, c, d, out why);
                if (n == null) { f.Env[p.Name] = Val.Unknown("argument '" + p.Name + "' of " + md.Name + "() (line " + call.Line + ") is not translated: " + why, call.Line); continue; }
                Assume(st, d);
                d.From.Add(st);
                f.Env[p.Name] = new Val { Node = n, From = d.From, When = d.When };
            }
            Exec(body, f);
            CloseExits(f);
        }

        // ---- what statements may change ------------------------------------------------------------------

        // The bone properties and locals the expressions may change: assignments, ++ / --, calls on or with
        // parts, the upstream methods they call and vanilla methods with side effects.
        void Touched(IEnumerable<Expr> roots, Ctx c, HashSet<string> keys, HashSet<string> locals, int depth, bool callsOnly = false)
        {
            foreach (var root in roots)
                foreach (var e in Walk(root))
                {
                    Expr t = null;
                    if (e is AssignExpr a) t = a.Target;
                    else if (e is UnaryExpr u && (u.Op == "++" || u.Op == "--")) t = u.Operand;
                    if (t != null && callsOnly) continue;
                    if (t != null)
                    {
                        string path, prop;
                        if (t is NameExpr n) { if (!c.Fields.ContainsKey(n.Name)) locals.Add(n.Name); }
                        else if (BoneProp(t, c.Fields, out path, out prop)) { if (IsPoseProp(prop)) keys.Add(path + "." + prop); }
                        else if (t is FieldExpr upsrc && IsPoseProp(upsrc.Name)) foreach (var p in Candidates(upsrc.Target, c)) keys.Add(p + "." + upsrc.Name);
                        continue;
                    }
                    var call = e as CallExpr;
                    if (call == null) continue;
                    var parts = new List<string>();
                    string pp;
                    if (PartOf(call.Target, c.Fields, out pp)) parts.Add(pp);
                    foreach (var arg in call.Args) if (PartOf(arg, c.Fields, out pp)) parts.Add(pp);
                    if (call.Name == "forEach" && call.Target is CallExpr gp && gp.Name == "getAllParts") parts.AddRange(PartsUnder(gp.Target, c) ?? c.S.Bones.Keys.ToList());
                    if (call.Name.StartsWith("get", StringComparison.Ordinal) && call.Name != "getAllParts") parts.Clear(); // getChild / getters read
                    foreach (var p in parts) foreach (var prop in PoseProps) keys.Add(p + "." + prop);
                    if (!(call.Target == null || call.Target is ThisExpr || call.Target is SuperExpr)) continue;
                    MethodDecl md;
                    if (call.Target is SuperExpr)
                    {
                        var sup = c.Owner.Extends != null ? cp.Find(c.Owner.Extends, c.Owner) : null;
                        md = sup != null ? FindMethod(sup, call.Name, call.Args.Count) : null;
                    }
                    else md = FindMethod(c.Owner, call.Name, call.Args.Count);
                    if (md != null)
                    {
                        BlockStmt body = null;
                        if (depth < 6) { try { body = Parser.ParseBody(md.Owner.Unit, md); } catch (JavaSyntaxException) { } }
                        if (body == null) { AllKeys(c, keys); continue; }
                        var f = c.Fork();
                        f.Owner = md.Owner;
                        f.Fields = md.IsStatic ? new Dictionary<string, string>(StringComparer.Ordinal) : new Dictionary<string, string>(c.S.FieldMap, StringComparer.Ordinal);
                        for (int k = 0; k < md.Params.Count && k < call.Args.Count; k++)
                        {
                            f.Fields.Remove(md.Params[k].Name);
                            if (PartOf(call.Args[k], c.Fields, out pp)) f.Fields[md.Params[k].Name] = pp;
                        }
                        Touched(Exprs(body), f, keys, new HashSet<string>(StringComparer.Ordinal), depth + 1);
                    }
                    else if (call.Target is SuperExpr) { if (call.Name == "setupAnim") foreach (var p in c.S.VanillaParts) foreach (var prop in PoseProps) keys.Add(p + "." + prop); }
                    else if (call.Name == "poseRightArm" || call.Name == "poseLeftArm" || call.Name == "setupAttackAnimation")
                    {
                        // the vanilla HumanoidModel arm poses move both arms; the attack swing also turns the body
                        var moved = call.Name == "setupAttackAnimation" ? new[] { "right_arm", "left_arm", "body" } : new[] { "right_arm", "left_arm" };
                        foreach (var p in moved) if (c.S.Bones.ContainsKey(p)) foreach (var prop in PoseProps) keys.Add(p + "." + prop);
                    }
                    else if (!LooksPure(call.Name))
                    {
                        if (c.S.VanillaParts.Count > 0) { foreach (var p in c.S.VanillaParts) foreach (var prop in PoseProps) keys.Add(p + "." + prop); }
                        else AllKeys(c, keys);
                    }
                }
        }

        static void AllKeys(Ctx c, HashSet<string> keys) { foreach (var p in c.S.Bones.Keys) foreach (var prop in PoseProps) keys.Add(p + "." + prop); }

        // What the expressions may change is not known afterwards.
        void PoisonTouched(IEnumerable<Expr> roots, Ctx c, string why, int line)
        {
            var keys = new HashSet<string>(StringComparer.Ordinal);
            var locals = new HashSet<string>(StringComparer.Ordinal);
            Touched(roots, c, keys, locals, 0);
            foreach (var k in keys.OrderBy(k => k, StringComparer.Ordinal))
                if (c.S.Bones.ContainsKey(k.Substring(0, k.LastIndexOf('.')))) SetKey(c, k, Val.Unknown(Label(c, k) + " " + why, line));
            foreach (var n in locals)
            {
                Val v;
                if (!c.Env.TryGetValue(n, out v)) continue;
                Lose(c, v, "local '" + n + "' " + why);
                c.Env[n] = Val.Unknown("local '" + n + "' " + why, line);
            }
        }

        // What the expressions may change differs in the cases of `tags`.
        void TagTouched(IEnumerable<Expr> roots, Ctx c, HashSet<AnimStatement> tags)
        {
            var keys = new HashSet<string>(StringComparer.Ordinal);
            var locals = new HashSet<string>(StringComparer.Ordinal);
            Touched(roots, c, keys, locals, 0);
            foreach (var k in keys) TagKey(c, k, tags);
            foreach (var n in locals)
            {
                Val v;
                if (c.Env.TryGetValue(n, out v) && v.Node != null) c.Env[n] = v.With(tags);
            }
        }

        static void TagKey(Ctx c, string key, HashSet<AnimStatement> tags)
        {
            if (!c.S.Bones.ContainsKey(key.Substring(0, key.LastIndexOf('.')))) return;
            var v = Get(c, key);
            if (v.Node == null) return;
            c.Cur[key] = v.With(tags);
        }

        // ---- syntax helpers -------------------------------------------------------------------------------

        // The top-level expressions of a statement and of every statement inside it.
        static List<Expr> Exprs(Stmt s) { var list = new List<Expr>(); CollectExprs(s, list); return list; }

        static void CollectExprs(Stmt s, List<Expr> list)
        {
            switch (s)
            {
                case null: return;
                case ExprStmt es: list.Add(es.E); return;
                case LocalVarStmt lv: foreach (var v in lv.Vars) if (v.Value != null) list.Add(v.Value); return;
                case BlockStmt b: foreach (var x in b.Stmts) CollectExprs(x, list); return;
                case IfStmt i: list.Add(i.Cond); CollectExprs(i.Then, list); CollectExprs(i.Else, list); return;
                case ForStmt f:
                    foreach (var x in f.Init) CollectExprs(x, list);
                    if (f.Cond != null) list.Add(f.Cond);
                    list.AddRange(f.Update);
                    CollectExprs(f.Body, list);
                    return;
                case ForEachStmt fe: list.Add(fe.Iterable); CollectExprs(fe.Body, list); return;
                case WhileStmt w: list.Add(w.Cond); CollectExprs(w.Body, list); return;
                case SwitchStmt sw:
                    list.Add(sw.Selector);
                    foreach (var cs in sw.Cases)
                    {
                        foreach (var x in cs.Body) CollectExprs(x, list);
                        CollectExprs(cs.ArrowStmt, list);
                        if (cs.ArrowExpr != null) list.Add(cs.ArrowExpr);
                    }
                    return;
                case ReturnStmt r: if (r.Value != null) list.Add(r.Value); return;
                case YieldStmt y: if (y.Value != null) list.Add(y.Value); return;
                case ThrowStmt t: if (t.Value != null) list.Add(t.Value); return;
                case TryStmt t: CollectExprs(t.Body, list); CollectExprs(t.Finally, list); return;
            }
        }

        // An expression and every expression inside it (lambda bodies included).
        static List<Expr> Walk(Expr e) { var list = new List<Expr>(); WalkInto(e, list); return list; }

        static void WalkInto(Expr e, List<Expr> l)
        {
            if (e == null) return;
            l.Add(e);
            switch (e)
            {
                case FieldExpr f: WalkInto(f.Target, l); break;
                case CallExpr c: WalkInto(c.Target, l); foreach (var a in c.Args) WalkInto(a, l); break;
                case NewExpr n:
                    foreach (var a in n.Args) WalkInto(a, l);
                    if (n.Dims != null) foreach (var a in n.Dims) WalkInto(a, l);
                    WalkInto(n.Init, l);
                    break;
                case ArrayInitExpr ai: foreach (var a in ai.Items) WalkInto(a, l); break;
                case IndexExpr ix: WalkInto(ix.Target, l); WalkInto(ix.Index, l); break;
                case UnaryExpr u: WalkInto(u.Operand, l); break;
                case BinaryExpr b: WalkInto(b.Left, l); WalkInto(b.Right, l); break;
                case AssignExpr a: WalkInto(a.Target, l); WalkInto(a.Value, l); break;
                case CondExpr ce: WalkInto(ce.Cond, l); WalkInto(ce.Then, l); WalkInto(ce.Else, l); break;
                case CastExpr ca: WalkInto(ca.Operand, l); break;
                case InstanceOfExpr io: WalkInto(io.Operand, l); break;
                case LambdaExpr la:
                    WalkInto(la.Body, l);
                    if (la.Block != null) foreach (var x in Exprs(la.Block)) WalkInto(x, l);
                    break;
                case MethodRefExpr mr: WalkInto(mr.Target, l); break;
                case SwitchExpr sw:
                    WalkInto(sw.Selector, l);
                    foreach (var cs in sw.Cases)
                    {
                        foreach (var x in cs.Body) foreach (var y in Exprs(x)) WalkInto(y, l);
                        if (cs.ArrowStmt != null) foreach (var y in Exprs(cs.ArrowStmt)) WalkInto(y, l);
                        WalkInto(cs.ArrowExpr, l);
                    }
                    break;
            }
        }

        // May the statement leave the method (return, throw; a break / continue / yield not inside a loop or
        // switch of its own, which the base path never runs into)?
        static bool Jumps(Stmt s, bool nested = false)
        {
            switch (s)
            {
                case null: return false;
                case ReturnStmt _: case ThrowStmt _: return true;
                case BreakStmt _: case ContinueStmt _: case YieldStmt _: return !nested;
                case BlockStmt b: return b.Stmts.Any(x => Jumps(x, nested));
                case IfStmt i: return Jumps(i.Then, nested) || Jumps(i.Else, nested);
                case TryStmt t: return Jumps(t.Body, nested) || Jumps(t.Finally, nested);
                case SwitchStmt sw: return sw.Cases.Any(cs => cs.Body.Any(x => Jumps(x, true)) || Jumps(cs.ArrowStmt, true));
                case ForStmt f: return Jumps(f.Body, true);
                case ForEachStmt fe: return Jumps(fe.Body, true);
                case WhileStmt w: return Jumps(w.Body, true);
            }
            return false;
        }

        // Does the statement leave the method on every path (a return / throw at its top level, or an if
        // whose branches both do)?
        static bool AlwaysJumps(Stmt s)
        {
            switch (s)
            {
                case null: return false;
                case ReturnStmt _: case ThrowStmt _: return true;
                case BlockStmt b: return b.Stmts.Any(AlwaysJumps);
                case IfStmt i: return AlwaysJumps(i.Then) && AlwaysJumps(i.Else);
                case TryStmt t: return AlwaysJumps(t.Body) || AlwaysJumps(t.Finally);
            }
            return false;
        }

        static int LastLine(Stmt s)
        {
            if (s == null) return 0;
            int max = s.Line;
            switch (s)
            {
                case BlockStmt b: foreach (var x in b.Stmts) max = Math.Max(max, LastLine(x)); break;
                case IfStmt i: max = Math.Max(max, Math.Max(LastLine(i.Then), LastLine(i.Else))); break;
                case ForStmt f: max = Math.Max(max, LastLine(f.Body)); break;
                case ForEachStmt fe: max = Math.Max(max, LastLine(fe.Body)); break;
                case WhileStmt w: max = Math.Max(max, LastLine(w.Body)); break;
                case TryStmt t: max = Math.Max(max, Math.Max(LastLine(t.Body), LastLine(t.Finally))); break;
                case SwitchStmt sw: foreach (var cs in sw.Cases) { foreach (var x in cs.Body) max = Math.Max(max, LastLine(x)); max = Math.Max(max, LastLine(cs.ArrowStmt)); } break;
            }
            return max;
        }

        // Java source of an expression (for messages).
        public static string JavaText(Expr e)
        {
            switch (e)
            {
                case null: return "";
                case LitExpr l:
                    if (l.Value == null) return "null";
                    if (l.Value is string s) return "\"" + s + "\"";
                    if (l.Value is bool b) return b ? "true" : "false";
                    if (l.Value is float f) return f.ToString("R", CultureInfo.InvariantCulture) + "F";
                    return Convert.ToString(l.Value, CultureInfo.InvariantCulture);
                case NameExpr n: return n.Name;
                case ThisExpr _: return "this";
                case SuperExpr _: return "super";
                case FieldExpr fe: return JavaText(fe.Target) + "." + fe.Name;
                case CallExpr c: return (c.Target != null ? JavaText(c.Target) + "." : "") + c.Name + "(" + string.Join(", ", c.Args.Select(JavaText)) + ")";
                case UnaryExpr u: return u.Postfix ? JavaText(u.Operand) + u.Op : u.Op + Paren(u.Operand);
                case BinaryExpr bi: return Paren(bi.Left) + " " + bi.Op + " " + Paren(bi.Right);
                case AssignExpr a: return JavaText(a.Target) + " " + a.Op + " " + JavaText(a.Value);
                case CondExpr ce: return Paren(ce.Cond) + " ? " + Paren(ce.Then) + " : " + Paren(ce.Else);
                case CastExpr ca: return "(" + ca.Type.Name + ") " + Paren(ca.Operand);
                case IndexExpr ix: return JavaText(ix.Target) + "[" + JavaText(ix.Index) + "]";
                case InstanceOfExpr io: return Paren(io.Operand) + " instanceof " + io.Type.Name + (io.Bind != null ? " " + io.Bind : "");
                case MethodRefExpr mr: return JavaText(mr.Target) + "::" + mr.Name;
                case ClassLitExpr cl: return cl.Type.Name + ".class";
            }
            return e.GetType().Name.Replace("Expr", "").ToLowerInvariant() + " expression";
        }

        static string Paren(Expr e) { return e is BinaryExpr || e is CondExpr || e is AssignExpr || e is InstanceOfExpr ? "(" + JavaText(e) + ")" : JavaText(e); }

        // ---- expressions ----------------------------------------------------------------------------------

        // A Java expression with the given variables as Molang (null and the reason when it does not translate).
        public MNode TranslateExpression(string javaSource, Dictionary<string, MNode> env, out string why)
        {
            var e = Parser.ParseExpression(javaSource);
            var s = new Shared { Res = new AnimResult(), Bones = new Dictionary<string, BoneOut>(StringComparer.Ordinal) };
            var c = new Ctx
            {
                S = s, Env = env.ToDictionary(kv => kv.Key, kv => new Val { Node = kv.Value }, StringComparer.Ordinal),
                Cur = new Dictionary<string, Val>(StringComparer.Ordinal), Fields = new Dictionary<string, string>(StringComparer.Ordinal),
            };
            var d = new Deps();
            var n = Tr(e, c, d, out why);
            if (n != null && d.Assumes) { why = "counts " + d.Text(); return null; }
            if (n is MSym) { why = n.Emit() + " is not a number"; return null; }
            return n;
        }

        // A boolean: what does not translate counts as false (recorded in d.Assumed).
        MNode TrBool(Expr e, Ctx c, Deps d)
        {
            if (e is UnaryExpr u && u.Op == "!") return M.Not(TrBool(u.Operand, c, d));
            if (e is BinaryExpr b && (b.Op == "&&" || b.Op == "||")) return M.Cmp(b.Op, TrBool(b.Left, c, d), TrBool(b.Right, c, d));
            if (e is CastExpr ce) return TrBool(ce.Operand, c, d);
            var sub = new Deps();
            string why;
            var n = Tr(e, c, sub, out why);
            if (n != null) { d.Add(sub); return n; }
            // a != b is !(a == b): the comparison that counts as false is the ==
            if (e is BinaryExpr ne && ne.Op == "!=") { d.Assumed.Add(Paren(ne.Left) + " == " + Paren(ne.Right)); return M.Num(1); }
            d.Assumed.Add(JavaText(e));
            return M.Num(0);
        }

        MNode Tr(Expr e, Ctx c, Deps d, out string why)
        {
            why = null;
            var known = BaseValue(e, c, d);
            if (known != null) return known;
            var n = TrCore(e, c, d, out why);
            if (n != null && !(n is MSym) && MSym.Has(n))
            {
                why = "an enum constant or null (" + JavaText(e) + ") in arithmetic";
                return null;
            }
            return n;
        }

        // ---- base-case values (catalog.json baseCase) ---------------------------------------------------

        // The value a base-case entry gives the expression (null: none). A fixed one is a constant; another
        // one is counted as an assumption (the statement becomes a case entry).
        MNode BaseValue(Expr e, Ctx c, Deps d)
        {
            if (c.S.BaseCases.Count == 0 || !(e is NameExpr || e is FieldExpr || e is CallExpr)) return null;
            if (e is NameExpr n && (c.Env.ContainsKey(n.Name) || c.EntityNames.Contains(n.Name))) return null;
            string key = Canon(e, c);
            var bc = FindBase(c, key);
            if (bc == null) return null;
            if (bc.Fixed)
            {
                SortedSet<string> lines;
                if (!c.S.FixedUsed.TryGetValue(bc, out lines)) c.S.FixedUsed[bc] = lines = new SortedSet<string>(Comparer<string>.Create(CompareLines));
                lines.Add((c.Label != null ? c.Label + " " : "") + e.Line);
            }
            else d.Valued.Add(JavaText(e) + " as " + bc.ValueText + " (" + bc.Why + ")");
            if (bc.Value is string s) return new MSym { Name = s, Known = true };
            return M.Num(bc.Number);
        }

        static int CompareLines(string a, string b)
        {
            // "123" before "Class 45"; numbers in numeric order
            int ia, ib;
            bool na = int.TryParse(a, NumberStyles.Integer, CultureInfo.InvariantCulture, out ia), nb = int.TryParse(b, NumberStyles.Integer, CultureInfo.InvariantCulture, out ib);
            if (na && nb) return ia.CompareTo(ib);
            if (na != nb) return na ? -1 : 1;
            return string.CompareOrdinal(a, b);
        }

        static BaseCaseValue FindBase(Ctx c, string key)
        {
            foreach (var b in c.S.BaseCases)
            {
                if (b.Expr == key) return b;
                if (b.Expr.EndsWith("(*)", StringComparison.Ordinal) && key.EndsWith(")", StringComparison.Ordinal)
                    && key.StartsWith(b.Expr.Substring(0, b.Expr.Length - 2), StringComparison.Ordinal)) return b;
            }
            return null;
        }

        // Java text in the form base-case entries use: the entity is "entity", fields and methods of the
        // model are "this.<name>", type names stay.
        static string Canon(Expr e, Ctx c)
        {
            switch (e)
            {
                case NameExpr n:
                    if (c.EntityNames.Contains(n.Name)) return "entity";
                    if (c.Env.ContainsKey(n.Name) || (n.Name.Length > 0 && char.IsUpper(n.Name[0]))) return n.Name;
                    return "this." + n.Name;
                case ThisExpr _: return "this";
                case FieldExpr f: return Canon(f.Target, c) + "." + f.Name;
                case CallExpr call: return (call.Target == null ? "this" : Canon(call.Target, c)) + "." + call.Name + "(" + string.Join(", ", call.Args.Select(a => Canon(a, c))) + ")";
                case CastExpr ca: return Canon(ca.Operand, c);
            }
            return JavaText(e);
        }

        // Type.CONSTANT (HumanoidArm.RIGHT, HumanoidModel.ArmPose.EMPTY -> ArmPose.EMPTY): an enum constant.
        static MSym EnumConstant(FieldExpr f, Ctx c)
        {
            if (f.Name.Length == 0 || !f.Name.All(ch => char.IsUpper(ch) || char.IsDigit(ch) || ch == '_')) return null;
            string type = f.Target is NameExpr tn ? tn.Name : f.Target is FieldExpr upsrc ? upsrc.Name : null;
            if (type == null || type.Length == 0 || !char.IsUpper(type[0]) || type == "Mth" || type == "Math") return null;
            if (f.Target is NameExpr tn2 && (c.Env.ContainsKey(tn2.Name) || c.Fields.ContainsKey(tn2.Name))) return null;
            return new MSym { Name = type + "." + f.Name };
        }

        MNode TrCore(Expr e, Ctx c, Deps d, out string why)
        {
            why = null;
            switch (e)
            {
                case LitExpr l:
                    if (Interp.IsNumeric(l.Value)) return M.Num(Interp.ToDouble(l.Value));
                    if (l.Value is bool lb) return M.Num(lb ? 1 : 0);
                    if (l.Value == null) return new MSym { Name = "null" };
                    why = "literal " + Interp.Describe(l.Value);
                    return null;
                case NameExpr n:
                {
                    Val v;
                    if (c.Env.TryGetValue(n.Name, out v))
                    {
                        if (v.Node == null) { why = v.Why; return null; }
                        d.Read(v);
                        return v.Node;
                    }
                    why = "'" + n.Name + "' (entity state or a field)";
                    return null;
                }
                case FieldExpr f:
                {
                    Val known;
                    if (f.Target is NameExpr en && c.Env.TryGetValue(en.Name + "." + f.Name, out known))
                    {
                        if (known.Node == null) { why = known.Why; return null; }
                        d.Read(known);
                        return known.Node;
                    }
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
                    var ec = EnumConstant(f, c);
                    if (ec != null) return ec;
                    string path, prop;
                    if (BoneProp(f, c.Fields, out path, out prop) && IsPoseProp(prop) && c.S.Bones.ContainsKey(path))
                    {
                        string key = path + "." + prop;
                        var v = Get(c, key);
                        if (v.Node == null) { why = Label(c, key) + " is not translated (line " + v.At + ")"; return null; }
                        if (v.Vanilla) { why = Label(c, key) + " holds the value the vanilla " + c.S.Vanilla + ".setupAnim gives (not known here)"; return null; }
                        d.Read(v);
                        return v.Node;
                    }
                    why = "field " + JavaText(f);
                    return null;
                }
                case CastExpr ca:
                {
                    var x = Tr(ca.Operand, c, d, out why);
                    if (x == null) return null;
                    string t = ca.Type != null ? ca.Type.Name : null;
                    if (t == "int" || t == "long" || t == "short" || t == "byte")
                    {
                        // Java truncates toward zero
                        double v;
                        if (M.IsNum(x, out v)) return M.Num(Math.Truncate(v));
                        return M.Call("math.trunc", x);
                    }
                    return x;
                }
                case AssignExpr a:
                    // run before the expression (ApplyNested): its value is the target's new value
                    if (c.Applied.Contains(a)) return Tr(a.Target, c, d, out why);
                    why = "an assignment inside an expression";
                    return null;
                case UnaryExpr u:
                {
                    if ((u.Op == "++" || u.Op == "--") && c.Applied.Contains(u))
                    {
                        // ++x / --x: the new value; x++ / x--: the old one
                        var x = Tr(u.Operand, c, d, out why);
                        if (x == null) return null;
                        return u.Postfix ? M.Add(x, M.Num(1), u.Op == "++" ? -1 : 1) : x;
                    }
                    if (u.Op == "-") { var x = Tr(u.Operand, c, d, out why); return x != null ? M.Neg(x) : null; }
                    if (u.Op == "+") return Tr(u.Operand, c, d, out why);
                    if (u.Op == "!") { var x = Tr(u.Operand, c, d, out why); return x != null ? M.Not(x) : null; }
                    why = "operator " + u.Op;
                    return null;
                }
                case BinaryExpr bi:
                {
                    if (bi.Op == "&&" || bi.Op == "||") return TrBool(bi, c, d);
                    var l = Tr(bi.Left, c, d, out why);
                    if (l == null) return null;
                    var r = Tr(bi.Right, c, d, out why);
                    if (r == null) return null;
                    if (l is MSym || r is MSym)
                    {
                        // enum constants and null: == / != fold when one side is a base-case value of that type
                        var ls = l as MSym;
                        var rs = r as MSym;
                        if ((bi.Op == "==" || bi.Op == "!=") && ls != null && rs != null && (ls.Known || rs.Known)
                            && (ls.Name == "null" || rs.Name == "null" || ls.Qualifier == rs.Qualifier))
                            return M.Num((ls.Name == rs.Name) == (bi.Op == "==") ? 1 : 0);
                        why = "operator " + bi.Op + " on " + JavaText(bi) + " (an enum constant or null whose value is not known)";
                        return null;
                    }
                    switch (bi.Op)
                    {
                        case "+": return M.Add(l, r);
                        case "-": return M.Add(l, r, -1);
                        case "*": return M.Mul(l, r);
                        case "/":
                            if (IsZero(r)) { why = "division by zero"; return null; }
                            return M.Mul(l, r, true);
                        case "%":
                        {
                            double a, b;
                            if (M.IsNum(l, out a) && M.IsNum(r, out b) && b != 0) return M.Num(a % b); // both truncate toward zero
                            return M.Call("math.mod", l, r);
                        }
                        case "<": case ">": case "<=": case ">=": case "==": case "!=": return M.Cmp(bi.Op, l, r);
                    }
                    why = "operator " + bi.Op;
                    return null;
                }
                case CondExpr ce:
                {
                    var cond = TrBool(ce.Cond, c, d);
                    double cv;
                    if (M.IsNum(cond, out cv)) return Tr(cv != 0 ? ce.Then : ce.Else, c, d, out why);
                    var a = Tr(ce.Then, c, d, out why);
                    if (a == null) return null;
                    var b = Tr(ce.Else, c, d, out why);
                    if (b == null) return null;
                    return M.Cond(cond, a, b);
                }
                case CallExpr call:
                {
                    var cn = call.Target as NameExpr;
                    string cls = cn != null ? cn.Name : null;
                    if (cls == "Mth" || cls == "Math")
                    {
                        var args = new List<MNode>();
                        foreach (var x in call.Args) { var t = Tr(x, c, d, out why); if (t == null) return null; args.Add(t); }
                        if (args.All(x => x is MNum))
                        {
                            // constants: Java's own result (Mth's sine table, float rounding)
                            var lit = new CallExpr { Target = call.Target, Name = call.Name, Line = call.Line };
                            foreach (var x in args) lit.Args.Add(new LitExpr { Value = cls == "Mth" ? (object)(float)((MNum)x).V : ((MNum)x).V, Line = call.Line });
                            try { var v = interp.Eval(lit, new Frame(null)); return M.Num(Interp.ToDouble(v)); } catch (Exception) { }
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
                            case "pow": return M.Call("math.pow", args[0], args[1]);
                            case "lerp": return M.Call("math.lerp", args[1], args[2], args[0]);
                            case "toRadians": return M.Mul(args[0], M.Num(Math.PI / 180.0));
                            case "toDegrees": return M.Mul(args[0], M.Num(180.0 / Math.PI));
                            case "atan2": return M.Mul(M.Call("math.atan2", args[0], args[1]), M.Num(Math.PI / 180.0));
                            case "square": return M.Mul(args[0], args[0]);
                            case "frac": return M.Add(args[0], M.Call("math.floor", args[0]), -1);
                            case "triangleWave":
                            {
                                // Mth.triangleWave(x, p) = (|x % p - p / 2| - p / 4) / (p / 4)
                                var p = args[1];
                                var q = M.Mul(p, M.Num(0.25));
                                return M.Mul(M.Add(M.Call("math.abs", M.Add(M.Call("math.mod", args[0], p), M.Mul(p, M.Num(0.5)), -1)), q, -1), q, true);
                            }
                        }
                    }
                    why = "call " + (call.Target != null ? JavaText(call.Target) + "." : "") + call.Name + "()";
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
