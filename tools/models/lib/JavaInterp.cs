// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Interpreter for the model converter (contract "geo", unit F2): runs The Gloaming Forest's
// LayerDefinition factories (createBodyLayer / create / checkForPack / addPieces ...) on a small
// Java runtime with Java's numeric semantics (int / long / float / double promotion, float
// arithmetic, Mth's sine table, LegacyRandomSource) and builtins for Minecraft's model builder API
// (MeshDefinition, PartDefinition, CubeListBuilder, PartPose, CubeDeformation, LayerDefinition)
// plus the vanilla meshes upstream extends (HumanoidModel.createMesh, QuadrupedModel.createBodyMesh).
// Part of GloamingForest, LGPL-3.0-or-later.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace Gf.Models
{
    public sealed class JavaEvalException : Exception
    {
        public JavaEvalException(string message) : base(message) { }
    }

    // ---- model builder objects (net.minecraft.client.model.geom.builders) -----------------------------

    public sealed class CubeDef
    {
        public string Comment;
        public float U, V;                 // texture offset
        public float X, Y, Z;              // origin, relative to the part's pivot (Java model space, y down)
        public float W, H, D;              // dimensions
        public float GrowX, GrowY, GrowZ;  // CubeDeformation
        public bool Mirror;
        public float TexScaleU = 1, TexScaleV = 1;
        public HashSet<string> VisibleFaces; // null = all six
    }

    public sealed class PartPoseObj
    {
        public float X, Y, Z, XRot, YRot, ZRot;
        public static readonly PartPoseObj Zero = new PartPoseObj();
    }

    public sealed class CubeDeformationObj
    {
        public float X, Y, Z;
        public CubeDeformationObj(float x, float y, float z) { X = x; Y = y; Z = z; }
    }

    public sealed class PartDef
    {
        public List<CubeDef> Cubes = new List<CubeDef>();
        public PartPoseObj Pose = PartPoseObj.Zero;
        // insertion-ordered children (Java keeps a HashMap; the order does not matter to the model)
        public List<KeyValuePair<string, PartDef>> Children = new List<KeyValuePair<string, PartDef>>();

        public PartDef Get(string name)
        {
            foreach (var kv in Children) if (kv.Key == name) return kv.Value;
            return null;
        }

        public PartDef AddOrReplace(string name, List<CubeDef> cubes, PartPoseObj pose)
        {
            var part = new PartDef { Cubes = cubes, Pose = pose };
            for (int i = 0; i < Children.Count; i++)
            {
                if (Children[i].Key == name)
                {
                    // PartDefinition.addOrReplaceChild keeps the replaced part's children
                    part.Children.AddRange(Children[i].Value.Children);
                    Children[i] = new KeyValuePair<string, PartDef>(name, part);
                    return part;
                }
            }
            Children.Add(new KeyValuePair<string, PartDef>(name, part));
            return part;
        }
    }

    public sealed class MeshDef { public PartDef Root = new PartDef(); }

    public sealed class LayerDef
    {
        public MeshDef Mesh;
        public int TexWidth, TexHeight;
    }

    public sealed class CubeListBuilderObj
    {
        public List<CubeDef> Cubes = new List<CubeDef>();
        public int U, V;
        public bool Mirror;
    }

    public sealed class ModelLayerLocationObj
    {
        public string Model, Layer;
        public string Name { get { var path = Model.Substring(Model.IndexOf(':') + 1); return Layer == "main" ? path : path + "_" + Layer; } }
        public override string ToString() { return Model + "#" + Layer; }
    }

    public sealed class Vector3fObj { public float X, Y, Z; }

    public sealed class DirectionObj
    {
        public string Name; public int Ordinal, StepX, StepY, StepZ;
        public static readonly DirectionObj[] Values = {
            new DirectionObj { Name = "down", Ordinal = 0, StepY = -1 },
            new DirectionObj { Name = "up", Ordinal = 1, StepY = 1 },
            new DirectionObj { Name = "north", Ordinal = 2, StepZ = -1 },
            new DirectionObj { Name = "south", Ordinal = 3, StepZ = 1 },
            new DirectionObj { Name = "west", Ordinal = 4, StepX = -1 },
            new DirectionObj { Name = "east", Ordinal = 5, StepX = 1 },
        };
        public override string ToString() { return Name.ToUpperInvariant(); }
    }

    // net.minecraft.world.level.levelgen.LegacyRandomSource (java.util.Random's LCG)
    public sealed class JRandom
    {
        long seed;
        public JRandom(long s) { SetSeed(s); }
        public void SetSeed(long s) { seed = (s ^ 0x5DEECE66DL) & ((1L << 48) - 1); }
        public int Next(int bits)
        {
            seed = unchecked(seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
            return (int)(seed >> (48 - bits));
        }
        public int NextInt() { return Next(32); }
        public int NextInt(int bound)
        {
            if (bound <= 0) throw new JavaEvalException("bound must be positive");
            if ((bound & (bound - 1)) == 0) return (int)((long)bound * (long)Next(31) >> 31);
            int i, j;
            do { i = Next(31); j = i % bound; } while (unchecked(i - j + (bound - 1)) < 0);
            return j;
        }
        public float NextFloat() { return Next(24) * 5.9604645E-8F; }
        public double NextDouble() { return (double)(((long)Next(26) << 27) + Next(27)) * 1.110223E-16F; }
        public bool NextBoolean() { return Next(1) != 0; }
    }

    public sealed class JArray
    {
        public object[] Items; public string ElemType;
        public JArray(int n, string elemType) { Items = new object[n]; ElemType = elemType; }
    }

    public sealed class JList
    {
        public List<object> Items = new List<object>();
    }

    // A value that names a class (for static member access): an upstream class or a builtin.
    public sealed class ClassRef
    {
        public ClassDecl User;
        public string Builtin;
        public override string ToString() { return User != null ? User.Name : Builtin; }
    }

    public sealed class Callable
    {
        public LambdaExpr Lambda; public Frame Closure;
        public ClassRef MethodClass; public string MethodName; public object BoundTarget;
    }

    public sealed class OpaqueObj
    {
        public string Type;
        public OpaqueObj(string type) { Type = type; }
        public override string ToString() { return "<" + Type + ">"; }
    }

    // ---- frames ----------------------------------------------------------------------------------------

    public sealed class Frame
    {
        public ClassDecl Class;
        public Frame Parent; // lexical parent (lambdas)
        public List<Dictionary<string, Slot>> Scopes = new List<Dictionary<string, Slot>>();
        public Frame(ClassDecl cls) { Class = cls; Scopes.Add(new Dictionary<string, Slot>()); }
        public void Push() { Scopes.Add(new Dictionary<string, Slot>()); }
        public void Pop() { Scopes.RemoveAt(Scopes.Count - 1); }
        public Slot Find(string name)
        {
            for (int i = Scopes.Count - 1; i >= 0; i--) { Slot s; if (Scopes[i].TryGetValue(name, out s)) return s; }
            return Parent != null ? Parent.Find(name) : null;
        }
        public void Declare(string name, string type, object value) { Scopes[Scopes.Count - 1][name] = new Slot { Type = type, Value = value }; }
    }

    public sealed class Slot { public string Type; public object Value; }

    enum Flow { Normal, Break, Continue, Return, Yield }

    struct Completion
    {
        public Flow Flow; public object Value;
        public static readonly Completion Normal = new Completion { Flow = Flow.Normal };
    }

    // ---- class path ------------------------------------------------------------------------------------

    public sealed class ClassPath
    {
        public readonly string Root; // .../src/main/java
        readonly Dictionary<string, List<string>> files = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        readonly Dictionary<string, CompilationUnit> units = new Dictionary<string, CompilationUnit>(StringComparer.OrdinalIgnoreCase);

        public ClassPath(string root)
        {
            Root = Path.GetFullPath(root);
            foreach (var f in Directory.EnumerateFiles(Root, "*.java", SearchOption.AllDirectories))
            {
                var name = Path.GetFileNameWithoutExtension(f);
                List<string> l;
                if (!files.TryGetValue(name, out l)) files[name] = l = new List<string>();
                l.Add(f);
            }
            foreach (var l in files.Values) l.Sort(StringComparer.Ordinal);
        }

        public CompilationUnit Load(string file)
        {
            CompilationUnit cu;
            if (!units.TryGetValue(file, out cu))
            {
                cu = Parser.ParseUnit(File.ReadAllText(file), RelativePath(file));
                units[file] = cu;
            }
            return cu;
        }

        public string RelativePath(string file)
        {
            var rel = Path.GetRelativePath(Path.GetFullPath(Path.Combine(Root, "../../..")), file);
            return rel.Replace('\\', '/');
        }

        // The top-level upstream class with this simple name, preferring the imports / package of `context`.
        public ClassDecl Find(string simple, ClassDecl context)
        {
            // nested classes of the context and its outer classes
            for (var c = context; c != null; c = c.Outer)
            {
                foreach (var n in c.Nested) if (n.Name == simple) return n;
                if (c.Name == simple) return c;
            }
            List<string> candidates;
            if (!files.TryGetValue(simple, out candidates)) return null;
            string pick = null;
            if (candidates.Count == 1) pick = candidates[0];
            else if (context != null)
            {
                var unit = context.Unit;
                foreach (var imp in unit.Imports)
                {
                    if (imp.EndsWith("." + simple))
                    {
                        var rel = imp.Replace('.', Path.DirectorySeparatorChar) + ".java";
                        pick = candidates.FirstOrDefault(f => f.EndsWith(rel, StringComparison.Ordinal));
                        if (pick != null) break;
                    }
                }
                if (pick == null && unit.Package != null)
                {
                    var rel = unit.Package.Replace('.', Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar + simple + ".java";
                    pick = candidates.FirstOrDefault(f => f.EndsWith(rel, StringComparison.Ordinal));
                }
            }
            if (pick == null) return null;
            var cu = Load(pick);
            return cu.Types.FirstOrDefault(t => t.Name == simple);
        }

        public string FileOf(string simple)
        {
            List<string> l;
            return files.TryGetValue(simple, out l) && l.Count == 1 ? l[0] : null;
        }
    }

    // ---- interpreter -----------------------------------------------------------------------------------

    public sealed class Interp
    {
        public readonly ClassPath ClassPath;
        public long UnseededRandomSeed = 0x67666D6F64656CL; // RandomSource.create() without a seed: fixed for reproducible output
        readonly Dictionary<FieldDecl, object> statics = new Dictionary<FieldDecl, object>();
        readonly HashSet<FieldDecl> initializing = new HashSet<FieldDecl>();
        int depth;
        public readonly List<string> Notes = new List<string>();

        static readonly HashSet<string> Builtins = new HashSet<string> {
            "Mth", "Math", "PartPose", "CubeListBuilder", "CubeDeformation", "MeshDefinition", "LayerDefinition", "LayerDefinitions",
            "HumanoidModel", "QuadrupedModel", "PigModel", "SheepModel", "SkeletonModel", "PlayerModel", "RandomSource", "Direction", "Vector3f",
            "JappaPackReloadListener", "GloamingForestMod", "Float", "Integer", "Double", "Long", "String", "List", "ImmutableList", "Arrays",
            "EnumSet", "Set", "ModelLayerLocation", "ResourceLocation", "Collections", "Boolean", "System", "Objects"
        };

        public Interp(ClassPath cp) { ClassPath = cp; }

        JavaEvalException Err(Node at, ClassDecl cls, string msg)
        {
            string where = cls != null && cls.Unit != null ? cls.Unit.Path : "?";
            return new JavaEvalException(where + ":" + (at != null ? at.Line : 0) + ": " + msg);
        }

        // ---- entry points ------------------------------------------------------------------------------

        public object CallStatic(ClassDecl cls, string name, List<object> args)
        {
            return InvokeUser(cls, name, args, null, null);
        }

        public object Invoke(object callable, List<object> args)
        {
            var c = callable as Callable;
            if (c == null) throw new JavaEvalException("not callable: " + Describe(callable));
            if (c.Lambda != null)
            {
                var f = new Frame(c.Closure.Class) { Parent = c.Closure };
                for (int i = 0; i < c.Lambda.Params.Count; i++) f.Declare(c.Lambda.Params[i], null, i < args.Count ? args[i] : null);
                if (c.Lambda.Body != null) return Eval(c.Lambda.Body, f);
                var r = ExecBlock(c.Lambda.Block, f);
                return r.Flow == Flow.Return ? r.Value : null;
            }
            if (c.MethodClass != null)
            {
                if (c.MethodClass.User != null) return InvokeUser(c.MethodClass.User, c.MethodName, args, null, null);
                return CallBuiltinStatic(c.MethodClass.Builtin, c.MethodName, args, null, null);
            }
            throw new JavaEvalException("bound method references are not supported");
        }

        public object GetStatic(ClassDecl cls, string field)
        {
            for (var c = cls; c != null; c = SuperOf(c))
            {
                var f = c.Fields.FirstOrDefault(x => x.Name == field && x.IsStatic);
                if (f != null) return StaticValue(c, f);
            }
            throw new JavaEvalException(cls.Name + "." + field + ": no such static field");
        }

        object StaticValue(ClassDecl cls, FieldDecl f)
        {
            object v;
            if (statics.TryGetValue(f, out v)) return v;
            if (!initializing.Add(f)) throw new JavaEvalException("cyclic static initialization of " + cls.Name + "." + f.Name);
            try
            {
                var init = Parser.ParseFieldInit(cls.Unit, f);
                var frame = new Frame(cls);
                v = init == null ? DefaultValue(f.Type) : (init is ArrayInitExpr ai ? MakeArray(ai, f.Type, frame) : Coerce(Eval(init, frame), f.Type));
                statics[f] = v;
                return v;
            }
            finally { initializing.Remove(f); }
        }

        ClassDecl SuperOf(ClassDecl c)
        {
            if (c.Extends == null || Builtins.Contains(c.Extends)) return null;
            return ClassPath.Find(c.Extends, c);
        }

        string BuiltinSuperOf(ClassDecl c)
        {
            for (var x = c; x != null; x = SuperOf(x))
            {
                if (x.Extends != null && Builtins.Contains(x.Extends)) return x.Extends;
            }
            return null;
        }

        // ---- method invocation -------------------------------------------------------------------------

        object InvokeUser(ClassDecl cls, string name, List<object> args, Node at, ClassDecl caller)
        {
            for (var c = cls; c != null; c = SuperOf(c))
            {
                var m = PickMethod(c, name, args);
                if (m != null) return Run(m, args);
            }
            // outer classes (unqualified calls from nested classes)
            for (var o = cls.Outer; o != null; o = o.Outer)
            {
                var m = PickMethod(o, name, args);
                if (m != null) return Run(m, args);
            }
            var b = BuiltinSuperOf(cls);
            if (b != null) return CallBuiltinStatic(b, name, args, at, cls);
            throw Err(at, caller ?? cls, cls.Name + "." + name + "(" + args.Count + " args): no such method");
        }

        MethodDecl PickMethod(ClassDecl c, string name, List<object> args)
        {
            MethodDecl best = null; int bestScore = -1;
            foreach (var m in c.Methods)
            {
                if (m.Name != name || m.BodyStart < 0) continue;
                bool varargs = m.Params.Count > 0 && m.Params[m.Params.Count - 1].VarArgs;
                if (!varargs && m.Params.Count != args.Count) continue;
                if (varargs && args.Count < m.Params.Count - 1) continue;
                int score = 0;
                bool ok = true;
                for (int i = 0; i < Math.Min(args.Count, m.Params.Count); i++)
                {
                    int s = Compat(args[i], m.Params[i].Type);
                    if (s < 0) { ok = false; break; }
                    score += s;
                }
                if (ok && score > bestScore) { best = m; bestScore = score; }
            }
            return best;
        }

        static int Compat(object v, TypeRef t)
        {
            string n = t.Dims > 0 ? "array" : t.Name;
            switch (n)
            {
                case "int": return v is int ? 3 : (v is char || v is short || v is byte) ? 2 : -1;
                case "long": return v is long ? 3 : (v is int) ? 2 : -1;
                case "float": return v is float ? 3 : (v is int || v is long) ? 2 : -1;
                case "double": return v is double ? 3 : (v is float || v is int || v is long) ? 2 : -1;
                case "boolean": return v is bool ? 3 : -1;
                case "String": return v is string || v == null ? 3 : -1;
                case "array": return v is JArray || v == null ? 2 : -1;
                default: return (v is int || v is float || v is double || v is long || v is bool) ? 0 : 1;
            }
        }

        object Run(MethodDecl m, List<object> args)
        {
            if (++depth > 200) throw new JavaEvalException("recursion too deep in " + m.Name);
            try
            {
                var body = Parser.ParseBody(m.Owner.Unit, m);
                var f = new Frame(m.Owner);
                for (int i = 0; i < m.Params.Count; i++)
                {
                    var prm = m.Params[i];
                    if (prm.VarArgs)
                    {
                        var arr = new JArray(Math.Max(0, args.Count - i), prm.Type.Name);
                        for (int k = i; k < args.Count; k++) arr.Items[k - i] = args[k];
                        f.Declare(prm.Name, "array", arr);
                        break;
                    }
                    f.Declare(prm.Name, TypeKey(prm.Type), Coerce(args[i], prm.Type));
                }
                var r = ExecBlock(body, f);
                return r.Flow == Flow.Return ? (m.ReturnType != null ? Coerce(r.Value, m.ReturnType) : r.Value) : null;
            }
            finally { depth--; }
        }

        // ---- statements --------------------------------------------------------------------------------

        Completion ExecBlock(BlockStmt b, Frame f)
        {
            f.Push();
            try
            {
                foreach (var s in b.Stmts)
                {
                    var c = Exec(s, f);
                    if (c.Flow != Flow.Normal) return c;
                }
                return Completion.Normal;
            }
            finally { f.Pop(); }
        }

        Completion Exec(Stmt s, Frame f)
        {
            switch (s)
            {
                case BlockStmt b: return ExecBlock(b, f);
                case EmptyStmt _: return Completion.Normal;
                case ExprStmt es: Eval(es.E, f); return Completion.Normal;
                case LocalVarStmt lv:
                    foreach (var kv in lv.Vars)
                    {
                        object v;
                        if (kv.Value == null) v = DefaultValue(lv.Type);
                        else if (kv.Value is ArrayInitExpr ai) v = MakeArray(ai, lv.Type, f);
                        else v = Coerce(Eval(kv.Value, f), lv.Type);
                        f.Declare(kv.Key, TypeKey(lv.Type), v);
                    }
                    return Completion.Normal;
                case IfStmt ifs:
                    if (Truth(Eval(ifs.Cond, f), ifs)) return Exec(ifs.Then, f);
                    return ifs.Else != null ? Exec(ifs.Else, f) : Completion.Normal;
                case ForStmt fs:
                {
                    f.Push();
                    try
                    {
                        foreach (var i in fs.Init) Exec(i, f);
                        int guard = 0;
                        while (fs.Cond == null || Truth(Eval(fs.Cond, f), fs))
                        {
                            if (++guard > 100000) throw Err(fs, f.Class, "loop does not terminate");
                            var c = Exec(fs.Body, f);
                            if (c.Flow == Flow.Break) break;
                            if (c.Flow == Flow.Return || c.Flow == Flow.Yield) return c;
                            foreach (var u in fs.Update) Eval(u, f);
                        }
                        return Completion.Normal;
                    }
                    finally { f.Pop(); }
                }
                case ForEachStmt fe:
                {
                    var it = Eval(fe.Iterable, f);
                    IEnumerable<object> items;
                    if (it is JArray ja) items = ja.Items;
                    else if (it is JList jl) items = jl.Items.ToArray();
                    else throw Err(fe, f.Class, "cannot iterate " + Describe(it));
                    foreach (var item in items)
                    {
                        f.Push();
                        try
                        {
                            f.Declare(fe.Var, TypeKey(fe.Type), Coerce(item, fe.Type));
                            var c = Exec(fe.Body, f);
                            if (c.Flow == Flow.Break) break;
                            if (c.Flow == Flow.Return || c.Flow == Flow.Yield) return c;
                        }
                        finally { f.Pop(); }
                    }
                    return Completion.Normal;
                }
                case WhileStmt ws:
                {
                    int guard = 0;
                    if (ws.Do)
                    {
                        do
                        {
                            if (++guard > 100000) throw Err(ws, f.Class, "loop does not terminate");
                            var c = Exec(ws.Body, f);
                            if (c.Flow == Flow.Break) break;
                            if (c.Flow == Flow.Return || c.Flow == Flow.Yield) return c;
                        } while (Truth(Eval(ws.Cond, f), ws));
                        return Completion.Normal;
                    }
                    while (Truth(Eval(ws.Cond, f), ws))
                    {
                        if (++guard > 100000) throw Err(ws, f.Class, "loop does not terminate");
                        var c = Exec(ws.Body, f);
                        if (c.Flow == Flow.Break) break;
                        if (c.Flow == Flow.Return || c.Flow == Flow.Yield) return c;
                    }
                    return Completion.Normal;
                }
                case ReturnStmt rs: return new Completion { Flow = Flow.Return, Value = rs.Value != null ? Eval(rs.Value, f) : null };
                case BreakStmt _: return new Completion { Flow = Flow.Break };
                case ContinueStmt _: return new Completion { Flow = Flow.Continue };
                case YieldStmt ys: return new Completion { Flow = Flow.Yield, Value = Eval(ys.Value, f) };
                case ThrowStmt ts: throw Err(ts, f.Class, "the model code throws: " + Describe(Eval(ts.Value, f)));
                case TryStmt tr:
                {
                    try { return ExecBlock(tr.Body, f); }
                    finally { if (tr.Finally != null) ExecBlock(tr.Finally, f); }
                }
                case SwitchStmt sw:
                {
                    var sel = Eval(sw.Selector, f);
                    int start = FindCase(sw.Cases, sel, f);
                    if (start < 0) return Completion.Normal;
                    if (sw.Cases[start].Arrow)
                    {
                        var sc = sw.Cases[start];
                        if (sc.ArrowExpr != null) { Eval(sc.ArrowExpr, f); return Completion.Normal; }
                        var c = Exec(sc.ArrowStmt, f);
                        return c.Flow == Flow.Break ? Completion.Normal : c;
                    }
                    f.Push();
                    try
                    {
                        for (int i = start; i < sw.Cases.Count; i++)
                        {
                            foreach (var st in sw.Cases[i].Body)
                            {
                                var c = Exec(st, f);
                                if (c.Flow == Flow.Break) return Completion.Normal;
                                if (c.Flow != Flow.Normal) return c;
                            }
                        }
                        return Completion.Normal;
                    }
                    finally { f.Pop(); }
                }
            }
            throw Err(s, f.Class, "unsupported statement " + s.GetType().Name);
        }

        int FindCase(List<SwitchCase> cases, object sel, Frame f)
        {
            int def = -1;
            for (int i = 0; i < cases.Count; i++)
            {
                var sc = cases[i];
                if (sc.Labels.Count == 0) { def = i; continue; }
                foreach (var l in sc.Labels)
                {
                    object lv;
                    if (l is NameExpr ne && (sel is DirectionObj || sel is OpaqueObj)) lv = ne.Name;
                    else lv = Eval(l, f);
                    if (CaseEquals(sel, lv)) return i;
                }
            }
            return def;
        }

        static bool CaseEquals(object sel, object label)
        {
            if (sel is DirectionObj d) return label is string s ? string.Equals(s, d.Name, StringComparison.OrdinalIgnoreCase) : ReferenceEquals(sel, label);
            // an upstream enum constant ("BossVariant.NAGA") against a case label ("NAGA")
            if (sel is OpaqueObj o) return label is string ls2 ? o.Type == ls2 || o.Type.EndsWith("." + ls2, StringComparison.Ordinal) : label is OpaqueObj lo && lo.Type == o.Type;
            if (sel is string ss) return label is string ls && ss == ls;
            if (IsNumeric(sel) && IsNumeric(label)) return Convert.ToInt64(sel) == Convert.ToInt64(label);
            return Equals(sel, label);
        }

        // ---- expressions -------------------------------------------------------------------------------

        public object Eval(Expr e, Frame f)
        {
            switch (e)
            {
                case LitExpr l: return l.Value;
                case NameExpr n: return ResolveName(n, f);
                case ThisExpr t: throw Err(t, f.Class, "'this' in a static context");
                case FieldExpr fe: return EvalField(fe, f);
                case CallExpr c: return EvalCall(c, f);
                case NewExpr ne: return EvalNew(ne, f);
                case IndexExpr ie:
                {
                    var arr = Eval(ie.Target, f) as JArray;
                    if (arr == null) throw Err(ie, f.Class, "indexing a non-array");
                    return arr.Items[ToInt(Eval(ie.Index, f))];
                }
                case UnaryExpr u: return EvalUnary(u, f);
                case BinaryExpr b: return EvalBinary(b, f);
                case AssignExpr a: return EvalAssign(a, f);
                case CondExpr c: return Truth(Eval(c.Cond, f), c) ? Eval(c.Then, f) : Eval(c.Else, f);
                case CastExpr c: return CastTo(Eval(c.Operand, f), c.Type);
                case InstanceOfExpr io:
                {
                    var v = Eval(io.Operand, f);
                    bool r = v != null && TypeMatches(v, io.Type.Simple);
                    if (r && io.Bind != null) f.Declare(io.Bind, null, v);
                    return r;
                }
                case LambdaExpr le: return new Callable { Lambda = le, Closure = f };
                case MethodRefExpr mr:
                {
                    var target = Eval(mr.Target, f);
                    if (target is ClassRef cr) return new Callable { MethodClass = cr, MethodName = mr.Name };
                    return new Callable { BoundTarget = target, MethodName = mr.Name };
                }
                case SwitchExpr sw:
                {
                    var sel = Eval(sw.Selector, f);
                    int idx = FindCase(sw.Cases, sel, f);
                    if (idx < 0) throw Err(sw, f.Class, "no switch case matches " + Describe(sel));
                    var sc = sw.Cases[idx];
                    if (sc.ArrowExpr != null) return Eval(sc.ArrowExpr, f);
                    if (sc.ArrowStmt != null)
                    {
                        var c = Exec(sc.ArrowStmt, f);
                        if (c.Flow == Flow.Yield) return c.Value;
                        throw Err(sw, f.Class, "switch expression case without yield");
                    }
                    for (int i = idx; i < sw.Cases.Count; i++)
                        foreach (var st in sw.Cases[i].Body)
                        {
                            var c = Exec(st, f);
                            if (c.Flow == Flow.Yield) return c.Value;
                        }
                    throw Err(sw, f.Class, "switch expression without yield");
                }
                case ArrayInitExpr ai: return MakeArray(ai, new TypeRef { Name = "Object", Dims = 1 }, f);
                case ClassLitExpr cl: return new ClassRef { Builtin = cl.Type.Name };
                case SuperExpr s: throw Err(s, f.Class, "'super' in a static context");
            }
            throw Err(e, f.Class, "unsupported expression " + e.GetType().Name);
        }

        static bool TypeMatches(object v, string type)
        {
            switch (type)
            {
                case "PartDefinition": return v is PartDef;
                case "MeshDefinition": return v is MeshDef;
                case "String": return v is string;
                case "Integer": return v is int;
                case "Float": return v is float;
                default: return false;
            }
        }

        object ResolveName(NameExpr n, Frame f)
        {
            var slot = f.Find(n.Name);
            if (slot != null) return slot.Value;
            // static fields of the class, its super classes and outer classes
            for (var c = f.Class; c != null; c = c.Outer)
            {
                for (var k = c; k != null; k = SuperOf(k))
                {
                    var fd = k.Fields.FirstOrDefault(x => x.Name == n.Name);
                    if (fd != null)
                    {
                        if (!fd.IsStatic) throw Err(n, f.Class, "instance field '" + n.Name + "' in a static context");
                        return StaticValue(k, fd);
                    }
                }
            }
            var cr = ResolveClass(n.Name, f.Class);
            if (cr != null) return cr;
            throw Err(n, f.Class, "unknown name '" + n.Name + "'");
        }

        ClassRef ResolveClass(string name, ClassDecl context)
        {
            if (Builtins.Contains(name)) return new ClassRef { Builtin = name };
            var user = ClassPath.Find(name, context);
            if (user != null) return new ClassRef { User = user };
            return null;
        }

        object EvalField(FieldExpr fe, Frame f)
        {
            // dotted package names: gloamingforest.client.X
            if (fe.Target is NameExpr pn && f.Find(pn.Name) == null && ResolveClass(pn.Name, f.Class) == null && !HasStaticField(f.Class, pn.Name))
            {
                var cr0 = ResolveClass(fe.Name, f.Class);
                if (cr0 != null && char.IsLower(pn.Name[0])) return cr0;
            }
            var target = Eval(fe.Target, f);
            if (target is ClassRef cr)
            {
                if (cr.User != null)
                {
                    var nested = cr.User.Nested.FirstOrDefault(x => x.Name == fe.Name);
                    if (nested != null) return new ClassRef { User = nested };
                    if (cr.User.Kind == "enum" && cr.User.EnumConstants.Contains(fe.Name)) return new OpaqueObj(cr.User.Name + "." + fe.Name);
                    return GetStatic(cr.User, fe.Name);
                }
                return BuiltinStaticField(cr.Builtin, fe.Name, fe, f.Class);
            }
            return GetObjectField(target, fe.Name, fe, f.Class);
        }

        bool HasStaticField(ClassDecl cls, string name)
        {
            for (var c = cls; c != null; c = c.Outer)
                for (var k = c; k != null; k = SuperOf(k))
                    if (k.Fields.Any(x => x.Name == name)) return true;
            return false;
        }

        object GetObjectField(object target, string name, Node at, ClassDecl cls)
        {
            switch (target)
            {
                case PartPoseObj pp:
                    switch (name) { case "x": return pp.X; case "y": return pp.Y; case "z": return pp.Z; case "xRot": return pp.XRot; case "yRot": return pp.YRot; case "zRot": return pp.ZRot; }
                    break;
                case Vector3fObj v:
                    switch (name) { case "x": return v.X; case "y": return v.Y; case "z": return v.Z; }
                    break;
                case JArray a:
                    if (name == "length") return a.Items.Length;
                    break;
                case CubeDeformationObj d:
                    switch (name) { case "growX": return d.X; case "growY": return d.Y; case "growZ": return d.Z; }
                    break;
                case OpaqueObj o:
                    if (o.Type == "JappaPackReloadListener" || o.Type.EndsWith("LOGGER")) return o;
                    break;
            }
            throw Err(at, cls, "unknown field '" + name + "' of " + Describe(target));
        }

        object EvalCall(CallExpr c, Frame f)
        {
            var args = new List<object>();
            foreach (var a in c.Args) args.Add(Eval(a, f));
            if (c.Target == null)
            {
                // a local lambda variable cannot be called directly in Java; this is a method of the class
                return InvokeUser(f.Class, c.Name, args, c, f.Class);
            }
            if (c.Target is SuperExpr) throw Err(c, f.Class, "super calls are not supported in static code");
            // package-qualified static calls (net.minecraft.util.Mth.cos)
            var target = Eval(c.Target, f);
            if (target is ClassRef cr)
            {
                if (cr.User != null) return InvokeUser(cr.User, c.Name, args, c, f.Class);
                return CallBuiltinStatic(cr.Builtin, c.Name, args, c, f.Class);
            }
            return CallObject(target, c.Name, args, c, f.Class);
        }

        object EvalNew(NewExpr ne, Frame f)
        {
            if (ne.Dims != null)
            {
                if (ne.Init != null) return MakeArray(ne.Init, ne.Type, f);
                if (ne.Dims.Count == 0) throw Err(ne, f.Class, "array without size");
                int n = ToInt(Eval(ne.Dims[0], f));
                var arr = new JArray(n, ne.Type.Name);
                var elem = new TypeRef { Name = ne.Type.Name, Dims = ne.Type.Dims - 1 };
                for (int i = 0; i < n; i++) arr.Items[i] = DefaultValue(elem);
                return arr;
            }
            var args = new List<object>();
            foreach (var a in ne.Args) args.Add(Eval(a, f));
            switch (ne.Type.Simple)
            {
                case "MeshDefinition": return new MeshDef();
                case "CubeDeformation":
                    if (args.Count == 1) { float g = ToFloat(args[0]); return new CubeDeformationObj(g, g, g); }
                    return new CubeDeformationObj(ToFloat(args[0]), ToFloat(args[1]), ToFloat(args[2]));
                case "Vector3f":
                    if (args.Count == 0) return new Vector3fObj();
                    return new Vector3fObj { X = ToFloat(args[0]), Y = ToFloat(args[1]), Z = ToFloat(args[2]) };
                case "ModelLayerLocation": return new ModelLayerLocationObj { Model = Convert.ToString(args[0]), Layer = Convert.ToString(args[1]) };
                case "ArrayList": case "LinkedList": return new JList();
                case "StringBuilder": return new StringBuilder(args.Count > 0 ? Convert.ToString(args[0]) : "");
            }
            throw Err(ne, f.Class, "cannot construct " + ne.Type.Name);
        }

        JArray MakeArray(ArrayInitExpr ai, TypeRef type, Frame f)
        {
            var arr = new JArray(ai.Items.Count, type.Name);
            var elem = new TypeRef { Name = type.Name, Dims = Math.Max(0, type.Dims - 1) };
            for (int i = 0; i < ai.Items.Count; i++)
                arr.Items[i] = ai.Items[i] is ArrayInitExpr sub ? MakeArray(sub, elem, f) : Coerce(Eval(ai.Items[i], f), elem);
            return arr;
        }

        object EvalUnary(UnaryExpr u, Frame f)
        {
            switch (u.Op)
            {
                case "-":
                {
                    var v = Eval(u.Operand, f);
                    switch (v)
                    {
                        case int i: return unchecked(-i);
                        case long l: return unchecked(-l);
                        case float fl: return -fl;
                        case double d: return -d;
                        case char ch: return -(int)ch;
                    }
                    throw Err(u, f.Class, "cannot negate " + Describe(v));
                }
                case "+": return Promote(Eval(u.Operand, f));
                case "!": return !Truth(Eval(u.Operand, f), u);
                case "~":
                {
                    var v = Eval(u.Operand, f);
                    if (v is long l) return ~l;
                    return ~ToInt(v);
                }
                case "++": case "--":
                {
                    var old = Eval(u.Operand, f);
                    object one = 1;
                    var nv = Arith(u.Op == "++" ? "+" : "-", old, one, u, f.Class);
                    nv = CastPrimitive(nv, PrimName(old));
                    Store(u.Operand, nv, f);
                    return u.Postfix ? old : nv;
                }
            }
            throw Err(u, f.Class, "unsupported unary " + u.Op);
        }

        object EvalBinary(BinaryExpr b, Frame f)
        {
            if (b.Op == "&&") return Truth(Eval(b.Left, f), b) && Truth(Eval(b.Right, f), b);
            if (b.Op == "||") return Truth(Eval(b.Left, f), b) || Truth(Eval(b.Right, f), b);
            var l = Eval(b.Left, f);
            var r = Eval(b.Right, f);
            return Binary(b.Op, l, r, b, f.Class);
        }

        object Binary(string op, object l, object r, Node at, ClassDecl cls)
        {
            switch (op)
            {
                case "==": return BinEquals(l, r);
                case "!=": return !BinEquals(l, r);
                case "+":
                    if (l is string || r is string) return JavaString(l) + JavaString(r);
                    return Arith(op, l, r, at, cls);
                case "-": case "*": case "/": case "%": return Arith(op, l, r, at, cls);
                case "<": case ">": case "<=": case ">=":
                {
                    double a = ToDouble(l), c = ToDouble(r);
                    if (l is long || r is long) { long la = Convert.ToInt64(l), lc = Convert.ToInt64(r); return op == "<" ? la < lc : op == ">" ? la > lc : op == "<=" ? la <= lc : la >= lc; }
                    return op == "<" ? a < c : op == ">" ? a > c : op == "<=" ? a <= c : a >= c;
                }
                case "&": case "|": case "^":
                    if (l is bool lb && r is bool rb) return op == "&" ? lb & rb : op == "|" ? lb | rb : lb ^ rb;
                    if (l is long || r is long) { long a = Convert.ToInt64(l), c = Convert.ToInt64(r); return op == "&" ? a & c : op == "|" ? a | c : a ^ c; }
                    { int a = ToInt(l), c = ToInt(r); return op == "&" ? a & c : op == "|" ? a | c : a ^ c; }
                case "<<": case ">>": case ">>>":
                    if (l is long ll)
                    {
                        int s = ToInt(r) & 63;
                        return op == "<<" ? ll << s : op == ">>" ? ll >> s : (long)((ulong)ll >> s);
                    }
                    else
                    {
                        int li = ToInt(l), s = ToInt(r) & 31;
                        return op == "<<" ? li << s : op == ">>" ? li >> s : (int)((uint)li >> s);
                    }
            }
            throw Err(at, cls, "unsupported operator " + op);
        }

        static bool BinEquals(object l, object r)
        {
            if (l == null || r == null) return l == null && r == null;
            if (IsNumeric(l) && IsNumeric(r))
            {
                if (l is double || r is double || l is float || r is float) return ToDouble(l) == ToDouble(r);
                return Convert.ToInt64(l) == Convert.ToInt64(r);
            }
            if (l is bool lb && r is bool rb) return lb == rb;
            return ReferenceEquals(l, r) || (l is OpaqueObj lo && r is OpaqueObj ro && lo.Type == ro.Type);
        }

        object Arith(string op, object l, object r, Node at, ClassDecl cls)
        {
            l = Promote(l); r = Promote(r);
            if (!IsNumeric(l) || !IsNumeric(r)) throw Err(at, cls, "arithmetic on " + Describe(l) + " " + op + " " + Describe(r));
            if (l is double || r is double)
            {
                double a = ToDouble(l), c = ToDouble(r);
                switch (op) { case "+": return a + c; case "-": return a - c; case "*": return a * c; case "/": return a / c; case "%": return a % c; }
            }
            if (l is float || r is float)
            {
                float a = ToFloat(l), c = ToFloat(r);
                switch (op) { case "+": return a + c; case "-": return a - c; case "*": return a * c; case "/": return a / c; case "%": return a % c; }
            }
            if (l is long || r is long)
            {
                long a = Convert.ToInt64(l), c = Convert.ToInt64(r);
                switch (op)
                {
                    case "+": return unchecked(a + c); case "-": return unchecked(a - c); case "*": return unchecked(a * c);
                    case "/": if (c == 0) throw Err(at, cls, "division by zero"); return a / c;
                    case "%": if (c == 0) throw Err(at, cls, "division by zero"); return a % c;
                }
            }
            {
                int a = ToInt(l), c = ToInt(r);
                switch (op)
                {
                    case "+": return unchecked(a + c); case "-": return unchecked(a - c); case "*": return unchecked(a * c);
                    case "/": if (c == 0) throw Err(at, cls, "division by zero"); return a == int.MinValue && c == -1 ? a : a / c;
                    case "%": if (c == 0) throw Err(at, cls, "division by zero"); return c == -1 ? 0 : a % c;
                }
            }
            throw Err(at, cls, "unsupported arithmetic " + op);
        }

        object EvalAssign(AssignExpr a, Frame f)
        {
            object v;
            if (a.Op == "=")
            {
                v = a.Value is ArrayInitExpr ai ? MakeArray(ai, new TypeRef { Name = "Object", Dims = 1 }, f) : Eval(a.Value, f);
            }
            else
            {
                var old = Eval(a.Target, f);
                var rhs = Eval(a.Value, f);
                string op = a.Op.Substring(0, a.Op.Length - 1);
                v = Binary(op, old, rhs, a, f.Class);
                if (!(old is string)) v = CastPrimitive(v, PrimName(old)); // compound assignment narrows implicitly
            }
            return Store(a.Target, v, f);
        }

        object Store(Expr target, object v, Frame f)
        {
            switch (target)
            {
                case NameExpr n:
                {
                    var slot = f.Find(n.Name);
                    if (slot == null) throw Err(target, f.Class, "assignment to unknown '" + n.Name + "'");
                    slot.Value = slot.Type != null ? CoerceKey(v, slot.Type) : v;
                    return slot.Value;
                }
                case IndexExpr ie:
                {
                    var arr = Eval(ie.Target, f) as JArray;
                    if (arr == null) throw Err(target, f.Class, "indexing a non-array");
                    var elem = new TypeRef { Name = arr.ElemType };
                    arr.Items[ToInt(Eval(ie.Index, f))] = Coerce(v, elem);
                    return v;
                }
                case FieldExpr fe:
                {
                    var obj = Eval(fe.Target, f);
                    if (obj is Vector3fObj vec)
                    {
                        switch (fe.Name) { case "x": vec.X = ToFloat(v); return v; case "y": vec.Y = ToFloat(v); return v; case "z": vec.Z = ToFloat(v); return v; }
                    }
                    throw Err(target, f.Class, "cannot assign field " + fe.Name + " of " + Describe(obj));
                }
            }
            throw Err(target, f.Class, "unsupported assignment target");
        }

        // ---- builtin statics -----------------------------------------------------------------------------

        static readonly float[] SinTable = BuildSinTable();
        static float[] BuildSinTable()
        {
            var t = new float[65536];
            for (int i = 0; i < t.Length; i++) t[i] = (float)Math.Sin(i * Math.PI * 2.0 / 65536.0);
            return t;
        }
        public static float MthSin(float v) { return SinTable[(int)(long)(v * 10430.378F) & 65535]; }
        public static float MthCos(float v) { return SinTable[(int)(long)(v * 10430.378F + 16384.0F) & 65535]; }

        object BuiltinStaticField(string cls, string name, Node at, ClassDecl ctx)
        {
            switch (cls + "." + name)
            {
                case "Mth.PI": return 3.1415927F;
                case "Mth.HALF_PI": return 1.5707964F;
                case "Mth.TWO_PI": return 6.2831855F;
                case "Mth.DEG_TO_RAD": return 0.017453292F;
                case "Mth.RAD_TO_DEG": return 57.295776F;
                case "Mth.EPSILON": return 1.0E-5F;
                case "Mth.SQRT_OF_TWO": return 1.4142135F;
                case "Math.PI": return Math.PI;
                case "Math.E": return Math.E;
                case "PartPose.ZERO": return PartPoseObj.Zero;
                case "CubeDeformation.NONE": return new CubeDeformationObj(0, 0, 0);
                case "LayerDefinitions.INNER_ARMOR_DEFORMATION": return new CubeDeformationObj(0.5F, 0.5F, 0.5F);
                case "LayerDefinitions.OUTER_ARMOR_DEFORMATION": return new CubeDeformationObj(1.0F, 1.0F, 1.0F);
                case "JappaPackReloadListener.INSTANCE": return new OpaqueObj("JappaPackReloadListener");
                case "GloamingForestMod.LOGGER": return new OpaqueObj("GloamingForestMod.LOGGER");
                case "GloamingForestMod.ID": return "gloamingforest";
                case "Float.MAX_VALUE": return float.MaxValue;
                case "Float.MIN_VALUE": return float.Epsilon;
                case "Integer.MAX_VALUE": return int.MaxValue;
                case "Integer.MIN_VALUE": return int.MinValue;
            }
            if (cls == "Direction")
            {
                var d = DirectionObj.Values.FirstOrDefault(x => string.Equals(x.Name, name, StringComparison.OrdinalIgnoreCase));
                if (d != null) return d;
            }
            throw Err(at, ctx, "unknown builtin field " + cls + "." + name);
        }

        object CallBuiltinStatic(string cls, string name, List<object> a, Node at, ClassDecl ctx)
        {
            switch (cls)
            {
                case "Mth":
                    switch (name)
                    {
                        case "sin": return MthSin(ToFloat(a[0]));
                        case "cos": return MthCos(ToFloat(a[0]));
                        case "sqrt": return (float)Math.Sqrt(ToFloat(a[0]));
                        case "abs": return a[0] is int ai ? (object)Math.Abs(ai) : Math.Abs(ToFloat(a[0]));
                        case "floor": return (int)Math.Floor(ToDouble(a[0]));
                        case "ceil": return (int)Math.Ceiling(ToDouble(a[0]));
                        case "clamp":
                            if (a[0] is int) return Math.Min(Math.Max(ToInt(a[0]), ToInt(a[1])), ToInt(a[2]));
                            if (a[0] is double) return Math.Min(Math.Max(ToDouble(a[0]), ToDouble(a[1])), ToDouble(a[2]));
                            return Math.Min(Math.Max(ToFloat(a[0]), ToFloat(a[1])), ToFloat(a[2]));
                        case "lerp": return ToFloat(a[1]) + ToFloat(a[0]) * (ToFloat(a[2]) - ToFloat(a[1]));
                    }
                    break;
                case "Math":
                    switch (name)
                    {
                        case "sin": return Math.Sin(ToDouble(a[0]));
                        case "cos": return Math.Cos(ToDouble(a[0]));
                        case "tan": return Math.Tan(ToDouble(a[0]));
                        case "sqrt": return Math.Sqrt(ToDouble(a[0]));
                        case "toRadians": return ToDouble(a[0]) / 180.0 * Math.PI;
                        case "toDegrees": return ToDouble(a[0]) * 180.0 / Math.PI;
                        case "abs": return a[0] is int ai ? (object)Math.Abs(ai) : a[0] is float af ? (object)Math.Abs(af) : Math.Abs(ToDouble(a[0]));
                        case "floor": return Math.Floor(ToDouble(a[0]));
                        case "ceil": return Math.Ceiling(ToDouble(a[0]));
                        case "round": return a[0] is float rf ? (object)(int)Math.Floor(rf + 0.5f) : (long)Math.Floor(ToDouble(a[0]) + 0.5);
                        case "pow": return Math.Pow(ToDouble(a[0]), ToDouble(a[1]));
                        case "atan2": return Math.Atan2(ToDouble(a[0]), ToDouble(a[1]));
                        case "min": case "max":
                        {
                            var x = Promote(a[0]); var y = Promote(a[1]);
                            bool mn = name == "min";
                            if (x is int xi && y is int yi) return mn ? Math.Min(xi, yi) : Math.Max(xi, yi);
                            if (x is double || y is double) return mn ? Math.Min(ToDouble(x), ToDouble(y)) : Math.Max(ToDouble(x), ToDouble(y));
                            if (x is float || y is float) return mn ? Math.Min(ToFloat(x), ToFloat(y)) : Math.Max(ToFloat(x), ToFloat(y));
                            return mn ? Math.Min(Convert.ToInt64(x), Convert.ToInt64(y)) : Math.Max(Convert.ToInt64(x), Convert.ToInt64(y));
                        }
                    }
                    break;
                case "PartPose":
                    switch (name)
                    {
                        case "offset": return new PartPoseObj { X = ToFloat(a[0]), Y = ToFloat(a[1]), Z = ToFloat(a[2]) };
                        case "rotation": return new PartPoseObj { XRot = ToFloat(a[0]), YRot = ToFloat(a[1]), ZRot = ToFloat(a[2]) };
                        case "offsetAndRotation":
                            return new PartPoseObj { X = ToFloat(a[0]), Y = ToFloat(a[1]), Z = ToFloat(a[2]), XRot = ToFloat(a[3]), YRot = ToFloat(a[4]), ZRot = ToFloat(a[5]) };
                    }
                    break;
                case "CubeListBuilder":
                    if (name == "create") return new CubeListBuilderObj();
                    break;
                case "LayerDefinition":
                    if (name == "create")
                        return new LayerDef { Mesh = (MeshDef)a[0], TexWidth = ToInt(a[1]), TexHeight = ToInt(a[2]) };
                    break;
                case "HumanoidModel": case "SkeletonModel": case "PlayerModel":
                    if (name == "createMesh") return HumanoidMesh((CubeDeformationObj)a[0], ToFloat(a[1]));
                    break;
                case "QuadrupedModel": case "PigModel": case "SheepModel":
                    if (name == "createBodyMesh") return QuadrupedMesh(ToInt(a[0]), (CubeDeformationObj)a[1]);
                    break;
                case "RandomSource":
                    if (name == "create") return new JRandom(a.Count > 0 ? Convert.ToInt64(a[0]) : UnseededRandomSeed);
                    break;
                case "Direction":
                    if (name == "values") { var arr = new JArray(6, "Direction"); for (int i = 0; i < 6; i++) arr.Items[i] = DirectionObj.Values[i]; return arr; }
                    break;
                case "GloamingForestMod":
                    if (name == "prefix") return "gloamingforest:" + a[0];
                    if (name == "getModelTexture") return "gloamingforest:textures/entity/" + a[0];
                    break;
                case "ResourceLocation":
                    if (name == "withDefaultNamespace") return "minecraft:" + a[0];
                    if (name == "fromNamespaceAndPath") return a[0] + ":" + a[1];
                    break;
                case "Float":
                    if (name == "toString" || name == "valueOf") return name == "toString" ? JavaString(ToFloat(a[0])) : (object)ToFloat(a[0]);
                    break;
                case "Integer":
                    if (name == "toString") return ToInt(a[0]).ToString(CultureInfo.InvariantCulture);
                    if (name == "valueOf") return ToInt(a[0]);
                    break;
                case "String":
                    if (name == "valueOf") return JavaString(a[0]);
                    break;
                case "List": case "ImmutableList": case "Arrays": case "Set": case "EnumSet":
                    if (name == "of" || name == "asList")
                    {
                        var l = new JList();
                        foreach (var x in a) { if (x is JArray xa && a.Count == 1) l.Items.AddRange(xa.Items); else l.Items.Add(x); }
                        return l;
                    }
                    break;
            }
            throw Err(at, ctx, "unsupported builtin " + cls + "." + name + "(" + a.Count + " args)");
        }

        // HumanoidModel.createMesh (Minecraft 1.21.1): the vanilla biped upstream's humanoid models extend.
        static MeshDef HumanoidMesh(CubeDeformationObj def, float y)
        {
            var mesh = new MeshDef();
            var root = mesh.Root;
            var hat = new CubeDeformationObj(def.X + 0.5F, def.Y + 0.5F, def.Z + 0.5F);
            root.AddOrReplace("head", new List<CubeDef> { Box(0, 0, -4, -8, -4, 8, 8, 8, def, false) }, new PartPoseObj { Y = 0.0F + y });
            root.AddOrReplace("hat", new List<CubeDef> { Box(32, 0, -4, -8, -4, 8, 8, 8, hat, false) }, new PartPoseObj { Y = 0.0F + y });
            root.AddOrReplace("body", new List<CubeDef> { Box(16, 16, -4, 0, -2, 8, 12, 4, def, false) }, new PartPoseObj { Y = 0.0F + y });
            root.AddOrReplace("right_arm", new List<CubeDef> { Box(40, 16, -3, -2, -2, 4, 12, 4, def, false) }, new PartPoseObj { X = -5.0F, Y = 2.0F + y });
            root.AddOrReplace("left_arm", new List<CubeDef> { Box(40, 16, -1, -2, -2, 4, 12, 4, def, true) }, new PartPoseObj { X = 5.0F, Y = 2.0F + y });
            root.AddOrReplace("right_leg", new List<CubeDef> { Box(0, 16, -2, 0, -2, 4, 12, 4, def, false) }, new PartPoseObj { X = -1.9F, Y = 12.0F + y });
            root.AddOrReplace("left_leg", new List<CubeDef> { Box(0, 16, -2, 0, -2, 4, 12, 4, def, true) }, new PartPoseObj { X = 1.9F, Y = 12.0F + y });
            return mesh;
        }

        // QuadrupedModel.createBodyMesh (Minecraft 1.21.1).
        static MeshDef QuadrupedMesh(int legHeight, CubeDeformationObj def)
        {
            var mesh = new MeshDef();
            var root = mesh.Root;
            root.AddOrReplace("head", new List<CubeDef> { Box(0, 0, -4, -4, -8, 8, 8, 8, def, false) }, new PartPoseObj { Y = (float)(18 - legHeight), Z = -6.0F });
            root.AddOrReplace("body", new List<CubeDef> { Box(28, 8, -5, -10, -7, 10, 16, 8, def, false) },
                new PartPoseObj { Y = (float)(17 - legHeight), Z = 2.0F, XRot = (float)(Math.PI / 2) });
            Func<List<CubeDef>> leg = () => new List<CubeDef> { Box(0, 16, -2, 0, -2, 4, legHeight, 4, def, false) };
            root.AddOrReplace("right_hind_leg", leg(), new PartPoseObj { X = -3.0F, Y = (float)(24 - legHeight), Z = 7.0F });
            root.AddOrReplace("left_hind_leg", leg(), new PartPoseObj { X = 3.0F, Y = (float)(24 - legHeight), Z = 7.0F });
            root.AddOrReplace("right_front_leg", leg(), new PartPoseObj { X = -3.0F, Y = (float)(24 - legHeight), Z = -5.0F });
            root.AddOrReplace("left_front_leg", leg(), new PartPoseObj { X = 3.0F, Y = (float)(24 - legHeight), Z = -5.0F });
            return mesh;
        }

        static CubeDef Box(int u, int v, float x, float y, float z, float w, float h, float d, CubeDeformationObj def, bool mirror)
        {
            return new CubeDef { U = u, V = v, X = x, Y = y, Z = z, W = w, H = h, D = d, GrowX = def.X, GrowY = def.Y, GrowZ = def.Z, Mirror = mirror };
        }

        // ---- builtin objects ---------------------------------------------------------------------------

        object CallObject(object target, string name, List<object> a, Node at, ClassDecl ctx)
        {
            switch (target)
            {
                case CubeListBuilderObj b: return CallBuilder(b, name, a, at, ctx);
                case MeshDef m:
                    if (name == "getRoot") return m.Root;
                    break;
                case PartDef p:
                    switch (name)
                    {
                        case "addOrReplaceChild":
                        {
                            var cubes = a[1] is CubeListBuilderObj cb ? new List<CubeDef>(cb.Cubes) : throw Err(at, ctx, "addOrReplaceChild needs a CubeListBuilder");
                            return p.AddOrReplace((string)a[0], cubes, (PartPoseObj)a[2]);
                        }
                        case "getChild":
                        {
                            var c = p.Get((string)a[0]);
                            if (c == null) throw Err(at, ctx, "no child part '" + a[0] + "'");
                            return c;
                        }
                        case "clearChild": return p.AddOrReplace((string)a[0], new List<CubeDef>(), PartPoseObj.Zero);
                    }
                    break;
                case LayerDef l:
                    break;
                case CubeDeformationObj d:
                    if (name == "extend")
                    {
                        if (a.Count == 1) { float g = ToFloat(a[0]); return new CubeDeformationObj(d.X + g, d.Y + g, d.Z + g); }
                        return new CubeDeformationObj(d.X + ToFloat(a[0]), d.Y + ToFloat(a[1]), d.Z + ToFloat(a[2]));
                    }
                    break;
                case JRandom r:
                    switch (name)
                    {
                        case "nextInt": return a.Count == 0 ? r.NextInt() : a.Count == 1 ? r.NextInt(ToInt(a[0])) : ToInt(a[0]) + r.NextInt(ToInt(a[1]) - ToInt(a[0]));
                        case "nextFloat": return r.NextFloat();
                        case "nextDouble": return r.NextDouble();
                        case "nextBoolean": return r.NextBoolean();
                        case "setSeed": r.SetSeed(Convert.ToInt64(a[0])); return null;
                    }
                    break;
                case DirectionObj dir:
                    switch (name)
                    {
                        case "ordinal": case "get3DDataValue": return dir.Ordinal;
                        case "getStepX": return dir.StepX;
                        case "getStepY": return dir.StepY;
                        case "getStepZ": return dir.StepZ;
                        case "getName": case "getSerializedName": return dir.Name;
                        case "name": case "toString": return dir.Name.ToUpperInvariant();
                    }
                    break;
                case Vector3fObj v:
                    switch (name)
                    {
                        case "x": return v.X;
                        case "y": return v.Y;
                        case "z": return v.Z;
                        case "rotateX": { float s = (float)Math.Sin(ToFloat(a[0])), c = (float)Math.Cos(ToFloat(a[0])); float y = v.Y * c - v.Z * s, z = v.Y * s + v.Z * c; v.Y = y; v.Z = z; return v; }
                        case "rotateY": { float s = (float)Math.Sin(ToFloat(a[0])), c = (float)Math.Cos(ToFloat(a[0])); float x = v.X * c + v.Z * s, z = -v.X * s + v.Z * c; v.X = x; v.Z = z; return v; }
                        case "rotateZ": { float s = (float)Math.Sin(ToFloat(a[0])), c = (float)Math.Cos(ToFloat(a[0])); float x = v.X * c - v.Y * s, y = v.X * s + v.Y * c; v.X = x; v.Y = y; return v; }
                        case "add": v.X += ToFloat(a[0]); v.Y += ToFloat(a[1]); v.Z += ToFloat(a[2]); return v;
                        case "mul": if (a.Count == 1) { float s = ToFloat(a[0]); v.X *= s; v.Y *= s; v.Z *= s; } else { v.X *= ToFloat(a[0]); v.Y *= ToFloat(a[1]); v.Z *= ToFloat(a[2]); } return v;
                    }
                    break;
                case OpaqueObj o:
                    if (o.Type == "JappaPackReloadListener" && name == "isJappaPackLoaded") return false; // the Jappa models ship in an optional pack; upstream's own textures use the classic models
                    if (o.Type == "GloamingForestMod.LOGGER") { Notes.Add("LOGGER." + name + ": " + string.Join(", ", a.Select(JavaString))); return null; }
                    if (name == "ordinal" || name == "name") return o.Type;
                    break;
                case JArray arr:
                    if (name == "clone") { var c = new JArray(arr.Items.Length, arr.ElemType); Array.Copy(arr.Items, c.Items, arr.Items.Length); return c; }
                    break;
                case JList list:
                    switch (name)
                    {
                        case "get": return list.Items[ToInt(a[0])];
                        case "size": return list.Items.Count;
                        case "add": list.Items.Add(a[0]); return true;
                        case "isEmpty": return list.Items.Count == 0;
                    }
                    break;
                case string s:
                    switch (name)
                    {
                        case "length": return s.Length;
                        case "equals": return a[0] is string o2 && s == o2;
                        case "concat": return s + JavaString(a[0]);
                        case "toLowerCase": return s.ToLowerInvariant();
                        case "toUpperCase": return s.ToUpperInvariant();
                        case "isEmpty": return s.Length == 0;
                        case "startsWith": return s.StartsWith((string)a[0], StringComparison.Ordinal);
                        case "endsWith": return s.EndsWith((string)a[0], StringComparison.Ordinal);
                        case "replace": return s.Replace(JavaString(a[0]), JavaString(a[1]));
                    }
                    break;
                case StringBuilder sb:
                    if (name == "append") { sb.Append(JavaString(a[0])); return sb; }
                    if (name == "toString") return sb.ToString();
                    break;
                case Callable c:
                    if (name == "get" || name == "apply" || name == "accept" || name == "run" || name == "create") return Invoke(c, a);
                    break;
            }
            throw Err(at, ctx, "unsupported call " + Describe(target) + "." + name + "(" + a.Count + " args)");
        }

        object CallBuilder(CubeListBuilderObj b, string name, List<object> a, Node at, ClassDecl ctx)
        {
            switch (name)
            {
                case "texOffs": b.U = ToInt(a[0]); b.V = ToInt(a[1]); return b;
                case "mirror": b.Mirror = a.Count == 0 || Truth(a[0], at); return b;
                case "getCubes": return b.Cubes;
                case "addBox": break;
                default: throw Err(at, ctx, "unsupported CubeListBuilder." + name);
            }
            // CubeListBuilder.addBox overloads (Minecraft 1.21.1)
            string comment = null;
            int i = 0;
            if (a.Count > 0 && a[0] is string s) { comment = s; i = 1; }
            if (a.Count - i < 6) throw Err(at, ctx, "addBox with " + a.Count + " args");
            var cube = new CubeDef
            {
                Comment = comment,
                X = ToFloat(a[i]), Y = ToFloat(a[i + 1]), Z = ToFloat(a[i + 2]),
                W = ToFloat(a[i + 3]), H = ToFloat(a[i + 4]), D = ToFloat(a[i + 5]),
                Mirror = b.Mirror,
            };
            var rest = a.Skip(i + 6).ToList();
            if (comment != null)
            {
                // (comment, x, y, z, w, h, d[, deformation][, u, v])
                if (rest.Count > 0 && rest[0] is CubeDeformationObj d0) { cube.GrowX = d0.X; cube.GrowY = d0.Y; cube.GrowZ = d0.Z; rest.RemoveAt(0); }
                if (rest.Count == 2) { b.U = ToInt(rest[0]); b.V = ToInt(rest[1]); rest.Clear(); }
            }
            else if (rest.Count >= 1)
            {
                if (rest[0] is CubeDeformationObj d1)
                {
                    cube.GrowX = d1.X; cube.GrowY = d1.Y; cube.GrowZ = d1.Z;
                    if (rest.Count == 3) { cube.TexScaleU = ToFloat(rest[1]); cube.TexScaleV = ToFloat(rest[2]); }
                    rest.Clear();
                }
                else if (rest[0] is bool m) { cube.Mirror = m; rest.Clear(); }
                else if (rest[0] is JList faces)
                {
                    cube.VisibleFaces = new HashSet<string>(faces.Items.OfType<DirectionObj>().Select(x => x.Name));
                    rest.Clear();
                }
            }
            if (rest.Count > 0) throw Err(at, ctx, "unsupported addBox overload (" + a.Count + " args)");
            cube.U = b.U; cube.V = b.V;
            b.Cubes.Add(cube);
            return b;
        }

        // ---- values ------------------------------------------------------------------------------------

        public static bool IsNumeric(object v) { return v is int || v is long || v is float || v is double || v is char || v is short || v is byte; }

        static object Promote(object v)
        {
            if (v is char c) return (int)c;
            if (v is short s) return (int)s;
            if (v is byte b) return (int)b;
            return v;
        }

        public static int ToInt(object v)
        {
            switch (v)
            {
                case int i: return i;
                case long l: return unchecked((int)l);
                case char c: return c;
                case short s: return s;
                case byte b: return b;
                case float f: return F2I(f);
                case double d: return D2I(d);
            }
            throw new JavaEvalException("not an int: " + Describe(v));
        }

        public static float ToFloat(object v)
        {
            switch (v)
            {
                case float f: return f;
                case int i: return i;
                case long l: return l;
                case double d: return (float)d;
                case char c: return c;
            }
            throw new JavaEvalException("not a number: " + Describe(v));
        }

        public static double ToDouble(object v)
        {
            switch (v)
            {
                case double d: return d;
                case float f: return f;
                case int i: return i;
                case long l: return l;
                case char c: return c;
            }
            throw new JavaEvalException("not a number: " + Describe(v));
        }

        static int F2I(float f) { if (float.IsNaN(f)) return 0; if (f >= int.MaxValue) return int.MaxValue; if (f <= int.MinValue) return int.MinValue; return (int)f; }
        static int D2I(double d) { if (double.IsNaN(d)) return 0; if (d >= int.MaxValue) return int.MaxValue; if (d <= int.MinValue) return int.MinValue; return (int)d; }

        static bool Truth(object v, Node at)
        {
            if (v is bool b) return b;
            throw new JavaEvalException("line " + (at != null ? at.Line : 0) + ": not a boolean: " + Describe(v));
        }

        static string PrimName(object v)
        {
            switch (v) { case int _: return "int"; case long _: return "long"; case float _: return "float"; case double _: return "double"; case char _: return "char"; case short _: return "short"; case byte _: return "byte"; }
            return null;
        }

        static string TypeKey(TypeRef t) { return t == null ? null : t.Dims > 0 ? "array" : t.Name; }

        object Coerce(object v, TypeRef t) { return t == null ? v : CoerceKey(v, TypeKey(t)); }

        static object CoerceKey(object v, string key)
        {
            if (v == null || key == null) return v;
            switch (key)
            {
                case "int": case "long": case "float": case "double": case "short": case "byte": case "char":
                    return IsNumeric(v) ? CastPrimitive(v, key) : v;
                case "Float": return IsNumeric(v) ? (object)ToFloat(v) : v;
                case "Integer": return IsNumeric(v) ? (object)ToInt(v) : v;
                case "Double": return IsNumeric(v) ? (object)ToDouble(v) : v;
            }
            return v;
        }

        static object CastPrimitive(object v, string key)
        {
            if (key == null || !IsNumeric(v)) return v;
            switch (key)
            {
                case "int": return ToInt(v);
                case "long": return v is float f ? (long)f : v is double d ? (long)d : Convert.ToInt64(v);
                case "float": return ToFloat(v);
                case "double": return ToDouble(v);
                case "short": return unchecked((short)ToInt(v));
                case "byte": return unchecked((byte)ToInt(v));
                case "char": return unchecked((char)ToInt(v));
            }
            return v;
        }

        object CastTo(object v, TypeRef t)
        {
            if (t.Dims == 0)
            {
                switch (t.Name)
                {
                    case "int": case "long": case "float": case "double": case "short": case "byte": case "char": return CastPrimitive(v, t.Name);
                }
            }
            return v;
        }

        static object DefaultValue(TypeRef t)
        {
            if (t == null || t.Dims > 0) return null;
            switch (t.Name)
            {
                case "int": return 0; case "long": return 0L; case "float": return 0f; case "double": return 0.0;
                case "boolean": return false; case "char": return '\0'; case "short": return (short)0; case "byte": return (byte)0;
            }
            return null;
        }

        // Java's String.valueOf for the values the model code concatenates.
        public static string JavaString(object v)
        {
            switch (v)
            {
                case null: return "null";
                case string s: return s;
                case bool b: return b ? "true" : "false";
                case float f: return JavaFloatString(f);
                case double d: return JavaDoubleString(d);
                case char c: return c.ToString();
                case int i: return i.ToString(CultureInfo.InvariantCulture);
                case long l: return l.ToString(CultureInfo.InvariantCulture);
                case DirectionObj dir: return dir.Name;
            }
            return v.ToString();
        }

        public static string JavaFloatString(float f)
        {
            if (float.IsNaN(f)) return "NaN";
            if (float.IsInfinity(f)) return f > 0 ? "Infinity" : "-Infinity";
            if (f == Math.Floor(f) && Math.Abs(f) < 1e7f) return ((long)f).ToString(CultureInfo.InvariantCulture) + ".0";
            return f.ToString("R", CultureInfo.InvariantCulture);
        }

        public static string JavaDoubleString(double d)
        {
            if (d == Math.Floor(d) && Math.Abs(d) < 1e7) return ((long)d).ToString(CultureInfo.InvariantCulture) + ".0";
            return d.ToString("R", CultureInfo.InvariantCulture);
        }

        public static string Describe(object v)
        {
            if (v == null) return "null";
            if (v is string s) return "\"" + s + "\"";
            return v.GetType().Name + "(" + JavaString(v) + ")";
        }
    }
}
