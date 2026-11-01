// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Bedrock geometry writer for the model converter (contract "geo", unit F2). Part of GloamingForest,
// LGPL-3.0-or-later.
//
// Coordinates. Java model space is in pixels with y pointing down; a part's PartPose offset is its
// pivot relative to the parent's pivot, and its rotation is applied as Rz * Ry * Rx (radians). The
// living-entity renderer draws model point p at (p.x, 24 - p.y, -p.z) / 16 blocks for an entity
// facing south. Bedrock entity geometry (format 1.12.0) uses absolute pivots and cube origins in
// pixels with y up and draws point b at (b.x, b.y, -b.z) / 16. Hence:
//   pivot    b = (P.x, 24 - P.y, P.z)        P = the sum of the PartPose offsets up the chain
//   cube     origin = (P.x + x, 24 - (P.y + y + h), P.z + z), size = (w, h, d), inflate = growth
//   rotation [deg(xRot), deg(yRot), deg(zRot)] (Bedrock's z-y-x order, its sign conventions and the
//            y flip cancel out: Bedrock's look_at_target uses +query.target_x/y_rotation like Java's
//            head.xRot = headPitch, head.yRot = netHeadYaw)
// UV. Java's CubeListBuilder uses the box layout with float sizes; Bedrock's box UV floors the
// sizes, so a cube with a fractional size (or a scaled extract) gets per-face UV with the exact
// Java regions instead. Bedrock's "east" face is the -x face (Java's WEST).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace Gf.Models
{
    // ---- ordered JSON ---------------------------------------------------------------------------------

    public sealed class JObj
    {
        public readonly List<KeyValuePair<string, object>> Props = new List<KeyValuePair<string, object>>();
        public JObj Add(string key, object value) { Props.Add(new KeyValuePair<string, object>(key, value)); return this; }
        public object Get(string key) { foreach (var kv in Props) if (kv.Key == key) return kv.Value; return null; }
        public void Set(string key, object value)
        {
            for (int i = 0; i < Props.Count; i++) if (Props[i].Key == key) { Props[i] = new KeyValuePair<string, object>(key, value); return; }
            Add(key, value);
        }
    }

    public static class Json
    {
        public static string Num(double v)
        {
            if (double.IsNaN(v) || double.IsInfinity(v)) throw new ArgumentException("not a finite number");
            double r = Math.Round(v, 5, MidpointRounding.AwayFromZero);
            if (r == 0) r = 0; // no "-0"
            return r.ToString("0.#####", CultureInfo.InvariantCulture);
        }

        public static string Write(object v)
        {
            var sb = new StringBuilder();
            WriteValue(sb, v, 0);
            sb.Append('\n');
            return sb.ToString();
        }

        static bool IsInline(object v)
        {
            if (v is List<object> l) return l.All(x => x == null || x is string || x is bool || IsNumber(x)) && l.Count <= 16;
            return false;
        }

        static bool IsNumber(object x) { return x is int || x is long || x is double || x is float; }

        static void WriteValue(StringBuilder sb, object v, int indent)
        {
            switch (v)
            {
                case null: sb.Append("null"); return;
                case bool b: sb.Append(b ? "true" : "false"); return;
                case string s: WriteString(sb, s); return;
                case int i: sb.Append(i.ToString(CultureInfo.InvariantCulture)); return;
                case long l: sb.Append(l.ToString(CultureInfo.InvariantCulture)); return;
                case float f: sb.Append(Num(f)); return;
                case double d: sb.Append(Num(d)); return;
                case JObj o:
                {
                    if (o.Props.Count == 0) { sb.Append("{}"); return; }
                    sb.Append("{\n");
                    for (int k = 0; k < o.Props.Count; k++)
                    {
                        sb.Append(' ', indent + 2);
                        WriteString(sb, o.Props[k].Key);
                        sb.Append(": ");
                        WriteValue(sb, o.Props[k].Value, indent + 2);
                        if (k < o.Props.Count - 1) sb.Append(',');
                        sb.Append('\n');
                    }
                    sb.Append(' ', indent).Append('}');
                    return;
                }
                case List<object> l:
                {
                    if (l.Count == 0) { sb.Append("[]"); return; }
                    if (IsInline(l))
                    {
                        sb.Append('[');
                        for (int k = 0; k < l.Count; k++) { if (k > 0) sb.Append(", "); WriteValue(sb, l[k], indent); }
                        sb.Append(']');
                        return;
                    }
                    sb.Append("[\n");
                    for (int k = 0; k < l.Count; k++)
                    {
                        sb.Append(' ', indent + 2);
                        WriteValue(sb, l[k], indent + 2);
                        if (k < l.Count - 1) sb.Append(',');
                        sb.Append('\n');
                    }
                    sb.Append(' ', indent).Append(']');
                    return;
                }
            }
            throw new ArgumentException("cannot write " + v.GetType().Name + " as JSON");
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4")); else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        public static List<object> Arr(params object[] items) { return new List<object>(items); }
        public static List<object> Vec(double x, double y, double z) { return new List<object> { x, y, z }; }
    }

    // ---- small linear algebra (column vectors, row-major 3x3) -----------------------------------------

    public struct V3
    {
        public double X, Y, Z;
        public V3(double x, double y, double z) { X = x; Y = y; Z = z; }
        public static V3 operator +(V3 a, V3 b) { return new V3(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
        public static V3 operator -(V3 a, V3 b) { return new V3(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
        public static V3 operator *(double s, V3 a) { return new V3(s * a.X, s * a.Y, s * a.Z); }
    }

    public sealed class M3
    {
        public readonly double[,] A = new double[3, 3];
        public static M3 Identity() { var m = new M3(); m.A[0, 0] = m.A[1, 1] = m.A[2, 2] = 1; return m; }
        public static M3 Diag(double x, double y, double z) { var m = new M3(); m.A[0, 0] = x; m.A[1, 1] = y; m.A[2, 2] = z; return m; }
        public static M3 RotX(double r) { var m = Identity(); double c = Math.Cos(r), s = Math.Sin(r); m.A[1, 1] = c; m.A[1, 2] = -s; m.A[2, 1] = s; m.A[2, 2] = c; return m; }
        public static M3 RotY(double r) { var m = Identity(); double c = Math.Cos(r), s = Math.Sin(r); m.A[0, 0] = c; m.A[0, 2] = s; m.A[2, 0] = -s; m.A[2, 2] = c; return m; }
        public static M3 RotZ(double r) { var m = Identity(); double c = Math.Cos(r), s = Math.Sin(r); m.A[0, 0] = c; m.A[0, 1] = -s; m.A[1, 0] = s; m.A[1, 1] = c; return m; }
        // Java's ModelPart rotation (Quaternionf.rotationZYX(z, y, x)): Rz * Ry * Rx
        public static M3 Zyx(double x, double y, double z) { return RotZ(z) * RotY(y) * RotX(x); }
        public static M3 operator *(M3 a, M3 b)
        {
            var m = new M3();
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double s = 0; for (int k = 0; k < 3; k++) s += a.A[i, k] * b.A[k, j]; m.A[i, j] = s; }
            return m;
        }
        public static V3 operator *(M3 a, V3 v)
        {
            return new V3(a.A[0, 0] * v.X + a.A[0, 1] * v.Y + a.A[0, 2] * v.Z, a.A[1, 0] * v.X + a.A[1, 1] * v.Y + a.A[1, 2] * v.Z, a.A[2, 0] * v.X + a.A[2, 1] * v.Y + a.A[2, 2] * v.Z);
        }
        public static M3 operator *(double s, M3 a) { var m = new M3(); for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m.A[i, j] = s * a.A[i, j]; return m; }
        public M3 T() { var m = new M3(); for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m.A[i, j] = A[j, i]; return m; }
        public double Det()
        {
            return A[0, 0] * (A[1, 1] * A[2, 2] - A[1, 2] * A[2, 1]) - A[0, 1] * (A[1, 0] * A[2, 2] - A[1, 2] * A[2, 0]) + A[0, 2] * (A[1, 0] * A[2, 1] - A[1, 1] * A[2, 0]);
        }
        // Euler angles (x, y, z) in radians with this == Rz(z) * Ry(y) * Rx(x).
        public V3 EulerZyx()
        {
            double sy = -A[2, 0];
            if (sy > 1) sy = 1; if (sy < -1) sy = -1;
            double y = Math.Asin(sy);
            double x, z;
            if (Math.Abs(sy) < 0.9999999)
            {
                x = Math.Atan2(A[2, 1], A[2, 2]);
                z = Math.Atan2(A[1, 0], A[0, 0]);
            }
            else
            {
                // gimbal lock: put everything into z
                x = 0;
                z = Math.Atan2(-A[0, 1], A[1, 1]);
            }
            return new V3(x, y, z);
        }
    }

    // A PoseStack: affine transforms in Java's convention (translate in blocks, rotations in radians).
    public sealed class Pose
    {
        public M3 L = M3.Identity();
        public V3 T;
        public Pose Clone() { var p = new Pose(); p.L = 1.0 * L; p.T = T; return p; }
        public void Translate(double x, double y, double z) { T = T + L * new V3(x, y, z); }
        public void Scale(double x, double y, double z) { L = L * M3.Diag(x, y, z); }
        public void Rotate(M3 r) { L = L * r; }
        public V3 Apply(V3 v) { return L * v + T; }
    }

    // ---- conversion ------------------------------------------------------------------------------------

    public sealed class ConvertOptions
    {
        public string Identifier;
        // Root-level Java part names -> Bedrock bone names (vanilla skeleton names)
        public Dictionary<string, string> Rename = new Dictionary<string, string>();
        // When set, only these root-level parts (and their children) are written
        public HashSet<string> RootFilter;
        // Parent the root-level "hat" to "head" when both share the pivot and rotation (Java copies the head pose to the hat)
        public bool HatUnderHead = true;
        // Pose overrides by Java part path ("head", "head/jaw"): radians, absolute like Java's setupAnim
        public Dictionary<string, double[]> RotOverride = new Dictionary<string, double[]>();
        public string BoneParentOfRoots; // armor: none; extracts: wrapper bone name
    }

    public sealed class BoneOut
    {
        public string Name, Parent, JavaPath, JavaName;
        public V3 JavaPivot;          // absolute, Java model space
        public V3 Rot;                // radians (Java)
        public PartPoseObj Pose;
        public List<CubeDef> Cubes;
        public List<object> CubesJson = new List<object>();
    }

    public sealed class GeometryOut
    {
        public string Identifier;
        public int TexWidth, TexHeight;
        public List<BoneOut> Bones = new List<BoneOut>();
        public List<JObj> ExtraBones = new List<JObj>(); // wrapper bones written before the converted ones
        public double BoundsWidth, BoundsHeight, BoundsOffsetY;
        public int PerFaceCubes, BoxCubes, BakedGrowCubes;

        public JObj ToJson()
        {
            var desc = new JObj()
                .Add("identifier", Identifier)
                .Add("texture_width", TexWidth)
                .Add("texture_height", TexHeight)
                .Add("visible_bounds_width", BoundsWidth)
                .Add("visible_bounds_height", BoundsHeight)
                .Add("visible_bounds_offset", Json.Vec(0, BoundsOffsetY, 0));
            var bones = new List<object>();
            foreach (var b in ExtraBones) bones.Add(b);
            foreach (var b in Bones)
            {
                var o = new JObj().Add("name", b.Name);
                if (b.Parent != null) o.Add("parent", b.Parent);
                o.Add("pivot", PivotJson(b));
                if (b.Rot.X != 0 || b.Rot.Y != 0 || b.Rot.Z != 0) o.Add("rotation", Json.Vec(Deg(b.Rot.X), Deg(b.Rot.Y), Deg(b.Rot.Z)));
                if (b.CubesJson.Count > 0) o.Add("cubes", b.CubesJson);
                bones.Add(o);
            }
            var geo = new JObj().Add("description", desc).Add("bones", bones);
            return new JObj().Add("format_version", "1.12.0").Add("minecraft:geometry", Json.Arr(geo));
        }

        public Func<V3, V3> MapPoint; // Java model point (px) -> Bedrock geometry point (px)
        List<object> PivotJson(BoneOut b) { var p = MapPoint(b.JavaPivot); return Json.Vec(p.X, p.Y, p.Z); }

        public static double Deg(double rad)
        {
            double d = rad * 180.0 / Math.PI;
            double r = Math.Round(d, 4);
            return r;
        }
    }

    public static class Converter
    {
        public static readonly Dictionary<string, string> HumanoidNames = new Dictionary<string, string> {
            { "head", "head" }, { "hat", "hat" }, { "body", "body" },
            { "right_arm", "rightArm" }, { "left_arm", "leftArm" }, { "right_leg", "rightLeg" }, { "left_leg", "leftLeg" },
        };
        public static readonly Dictionary<string, string> QuadrupedNames = new Dictionary<string, string> {
            { "right_hind_leg", "leg0" }, { "left_hind_leg", "leg1" }, { "right_front_leg", "leg2" }, { "left_front_leg", "leg3" },
        };

        // The standard entity mapping: (x, 24 - y, z), scaled about the model origin and moved.
        public static Func<V3, V3> EntityMap(double scale, V3 offset, bool yOffset24)
        {
            return p => new V3(scale * p.X + offset.X, (yOffset24 ? 24 : 0) - scale * p.Y + offset.Y, scale * p.Z + offset.Z);
        }

        // Converts a mesh. `scale` != 1 bakes a uniform scale (per-face UV keeps the texture regions).
        public static GeometryOut Convert(LayerDef layer, ConvertOptions opt, Func<V3, V3> map = null, double scale = 1.0)
        {
            var g = new GeometryOut { Identifier = opt.Identifier, TexWidth = layer.TexWidth, TexHeight = layer.TexHeight };
            g.MapPoint = map ?? EntityMap(1.0, new V3(0, 0, 0), true);
            var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in layer.Mesh.Root.Children)
            {
                if (opt.RootFilter != null && !opt.RootFilter.Contains(kv.Key)) continue;
                Walk(g, opt, kv.Key, kv.Value, null, kv.Key, new V3(0, 0, 0), used, scale, true);
            }
            if (opt.HatUnderHead)
            {
                var hat = g.Bones.FirstOrDefault(b => b.JavaPath == "hat");
                var head = g.Bones.FirstOrDefault(b => b.JavaPath == "head");
                if (hat != null && head != null && hat.Parent == opt.BoneParentOfRoots && SameVec(hat.JavaPivot, head.JavaPivot) && SameVec(hat.Rot, head.Rot))
                {
                    hat.Parent = head.Name;
                    hat.Rot = new V3(0, 0, 0); // relative to the head now (Java's hat.copyFrom(head))
                }
            }
            ComputeBounds(g, scale);
            return g;
        }

        static bool SameVec(V3 a, V3 b) { return Math.Abs(a.X - b.X) < 1e-6 && Math.Abs(a.Y - b.Y) < 1e-6 && Math.Abs(a.Z - b.Z) < 1e-6; }

        static void Walk(GeometryOut g, ConvertOptions opt, string javaName, PartDef part, BoneOut parent, string path, V3 parentPivot, HashSet<string> used, double scale, bool isRoot)
        {
            var pose = part.Pose;
            double[] ov;
            V3 rot = new V3(pose.XRot, pose.YRot, pose.ZRot);
            if (opt.RotOverride.TryGetValue(path, out ov))
                rot = new V3(double.IsNaN(ov[0]) ? rot.X : ov[0], double.IsNaN(ov[1]) ? rot.Y : ov[1], double.IsNaN(ov[2]) ? rot.Z : ov[2]);
            var pivot = parentPivot + new V3(pose.X, pose.Y, pose.Z);
            string name;
            if (isRoot && opt.Rename.TryGetValue(javaName, out name)) { }
            else name = javaName;
            if (used.Contains(name))
            {
                string prefixed = (parent != null ? parent.Name : "root") + "_" + javaName;
                name = prefixed;
                for (int k = 2; used.Contains(name); k++) name = prefixed + "_" + k;
            }
            used.Add(name);
            var b = new BoneOut
            {
                Name = name, JavaName = javaName, JavaPath = path, Parent = parent != null ? parent.Name : opt.BoneParentOfRoots,
                JavaPivot = pivot, Rot = rot, Pose = pose, Cubes = part.Cubes,
            };
            foreach (var c in part.Cubes) b.CubesJson.Add(CubeJson(g, c, pivot, scale));
            g.Bones.Add(b);
            foreach (var kv in part.Children) Walk(g, opt, kv.Key, kv.Value, b, path + "/" + kv.Key, pivot, used, scale, false);
        }

        static bool IsInt(double v) { return Math.Abs(v - Math.Round(v)) < 1e-6; }

        static JObj CubeJson(GeometryOut g, CubeDef c, V3 pivot, double scale)
        {
            bool uniformGrow = c.GrowX == c.GrowY && c.GrowY == c.GrowZ;
            double gx = uniformGrow ? 0 : c.GrowX, gy = uniformGrow ? 0 : c.GrowY, gz = uniformGrow ? 0 : c.GrowZ;
            // the cube's Java corners (absolute), grown when the growth is not uniform
            var a = pivot + new V3(c.X - gx, c.Y - gy, c.Z - gz);
            var bb = pivot + new V3(c.X + c.W + gx, c.Y + c.H + gy, c.Z + c.D + gz);
            var p1 = g.MapPoint(a);
            var p2 = g.MapPoint(bb);
            var o = new JObj();
            o.Add("origin", Json.Vec(Math.Min(p1.X, p2.X), Math.Min(p1.Y, p2.Y), Math.Min(p1.Z, p2.Z)));
            o.Add("size", Json.Vec(Math.Abs(p2.X - p1.X), Math.Abs(p2.Y - p1.Y), Math.Abs(p2.Z - p1.Z)));
            if (uniformGrow && c.GrowX != 0) o.Add("inflate", (double)c.GrowX * scale);
            if (!uniformGrow) g.BakedGrowCubes++;
            bool boxUv = scale == 1.0 && uniformGrow && IsInt(c.W) && IsInt(c.H) && IsInt(c.D) && IsInt(c.U) && IsInt(c.V)
                         && c.TexScaleU == 1 && c.TexScaleV == 1 && c.VisibleFaces == null;
            if (boxUv)
            {
                o.Add("uv", Json.Arr((double)c.U, (double)c.V));
                if (c.Mirror) o.Add("mirror", true);
                g.BoxCubes++;
            }
            else
            {
                o.Add("uv", PerFaceUv(c));
                g.PerFaceCubes++;
            }
            return o;
        }

        // Java's box layout as Bedrock per-face UV (Bedrock face name: Java direction).
        public static JObj PerFaceUv(CubeDef c)
        {
            double u = c.U, v = c.V, w = c.W, h = c.H, d = c.D;
            double su = c.TexScaleU, sv = c.TexScaleV;
            var faces = new List<KeyValuePair<string, double[]>>();
            if (!c.Mirror)
            {
                faces.Add(Face("north", u + d, v + d, w, h));
                faces.Add(Face("east", u, v + d, d, h));
                faces.Add(Face("south", u + d + w + d, v + d, w, h));
                faces.Add(Face("west", u + d + w, v + d, d, h));
                faces.Add(Face("up", u + d, v, w, d));
                faces.Add(Face("down", u + d + w, v + d, w, -d));
            }
            else
            {
                faces.Add(Face("north", u + d + w, v + d, -w, h));
                faces.Add(Face("east", u + d + w + d, v + d, -d, h));
                faces.Add(Face("south", u + d + w + d + w, v + d, -w, h));
                faces.Add(Face("west", u + d, v + d, -d, h));
                faces.Add(Face("up", u + d + w, v, -w, d));
                faces.Add(Face("down", u + d + w + w, v + d, -w, -d));
            }
            // Java direction of each Bedrock face (for visibleFaces)
            var javaDir = new Dictionary<string, string> { { "north", "north" }, { "south", "south" }, { "east", "west" }, { "west", "east" }, { "up", "down" }, { "down", "up" } };
            var o = new JObj();
            foreach (var f in faces)
            {
                if (c.VisibleFaces != null && !c.VisibleFaces.Contains(javaDir[f.Key])) continue;
                var arr = f.Value;
                o.Add(f.Key, new JObj().Add("uv", Json.Arr(arr[0] / su, arr[1] / sv)).Add("uv_size", Json.Arr(arr[2] / su, arr[3] / sv)));
            }
            return o;
        }

        static KeyValuePair<string, double[]> Face(string name, double u, double v, double w, double h)
        {
            return new KeyValuePair<string, double[]>(name, new[] { u, v, w, h });
        }

        // Visible bounds from the static pose (blocks), with half a block of margin for animation.
        static void ComputeBounds(GeometryOut g, double scale)
        {
            var byName = g.Bones.ToDictionary(b => b.Name, b => b);
            var world = new Dictionary<string, Pose>();
            Func<BoneOut, Pose> worldOf = null;
            worldOf = b =>
            {
                Pose w;
                if (world.TryGetValue(b.Name, out w)) return w;
                BoneOut parent;
                Pose pw = b.Parent != null && byName.TryGetValue(b.Parent, out parent) ? worldOf(parent) : new Pose();
                var parentPivot = b.Parent != null && byName.TryGetValue(b.Parent, out parent) ? parent.JavaPivot : new V3(0, 0, 0);
                w = pw.Clone();
                w.Translate(b.JavaPivot.X - parentPivot.X, b.JavaPivot.Y - parentPivot.Y, b.JavaPivot.Z - parentPivot.Z);
                w.Rotate(M3.Zyx(b.Rot.X, b.Rot.Y, b.Rot.Z));
                world[b.Name] = w;
                return w;
            };
            double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
            bool any = false;
            foreach (var b in g.Bones)
            {
                var w = worldOf(b);
                foreach (var c in b.Cubes)
                {
                    for (int k = 0; k < 8; k++)
                    {
                        var corner = new V3(c.X + ((k & 1) != 0 ? c.W + c.GrowX : -c.GrowX), c.Y + ((k & 2) != 0 ? c.H + c.GrowY : -c.GrowY), c.Z + ((k & 4) != 0 ? c.D + c.GrowZ : -c.GrowZ));
                        var p = g.MapPoint(w.Apply(corner));
                        if (!any) { minX = maxX = p.X; minY = maxY = p.Y; minZ = maxZ = p.Z; any = true; }
                        minX = Math.Min(minX, p.X); maxX = Math.Max(maxX, p.X);
                        minY = Math.Min(minY, p.Y); maxY = Math.Max(maxY, p.Y);
                        minZ = Math.Min(minZ, p.Z); maxZ = Math.Max(maxZ, p.Z);
                    }
                }
            }
            if (!any) { g.BoundsWidth = 1; g.BoundsHeight = 1; g.BoundsOffsetY = 0.5; return; }
            double half = Math.Max(Math.Max(Math.Abs(minX), Math.Abs(maxX)), Math.Max(Math.Abs(minZ), Math.Abs(maxZ))) / 16.0;
            g.BoundsWidth = Math.Ceiling((2 * half + 1) * 2) / 2;
            g.BoundsHeight = Math.Ceiling(((maxY - minY) / 16.0 + 1) * 2) / 2;
            g.BoundsOffsetY = Math.Round((maxY + minY) / 32.0 * 4) / 4;
        }

        // Bedrock rotation (degrees) of a bone whose transform is R (a proper rotation acting on
        // Bedrock geometry coordinates). Bedrock's convention is E * Rz(c) Ry(b) Rx(a) * E with
        // E = diag(1, -1, 1) (see the header), so decompose E R E.
        public static V3 BedrockEuler(M3 r)
        {
            var e = M3.Diag(1, -1, 1);
            var n = e * r * e;
            var ang = n.EulerZyx();
            double x = GeometryOut.Deg(ang.X), y = GeometryOut.Deg(ang.Y), z = GeometryOut.Deg(ang.Z);
            // (x, y, z) and (x - 180, 180 - y, z - 180) are the same rotation: prefer the smaller x and z
            if (Math.Abs(x) > 90 && Math.Abs(z) > 90)
            {
                x = x > 0 ? x - 180 : x + 180;
                z = z > 0 ? z - 180 : z + 180;
                y = y >= 0 ? 180 - y : -180 - y;
            }
            return new V3(Clean(x), Clean(y), Clean(z));
        }

        static double Clean(double d)
        {
            double r = Math.Round(d, 4);
            if (r == 0 || r == -0.0) r = 0;
            if (r == -180) r = 180;
            return r;
        }
    }
}
