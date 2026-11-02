// GloamingForest import tool (contract "import", unit F1): Java block / item models (TF's generated
// and hand-made models plus a built-in library of the vanilla parent shapes they extend) resolved to
// elements and textures, and converted to Bedrock block geometry.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace GfImport
{
    public sealed class Face
    {
        public string Dir;          // north, south, east, west, up, down
        public double[] Uv;         // u1, v1, u2, v2 (Java, 0..16)
        public string Tex;          // "#var" or a texture id
        public int Rotation;        // 0, 90, 180, 270
        public int TintIndex = -1;
    }

    public sealed class Element
    {
        public double[] From, To;
        public double[] RotOrigin; public string RotAxis; public double RotAngle; public bool Rescale;
        public List<Face> Faces = new List<Face>();
    }

    public sealed class Model
    {
        public string Id;                      // "twilightforest:block/naga_stone"
        public string ElementsFrom;            // the model (self or ancestor) whose elements are used
        public List<Element> Elements;         // null: no elements anywhere in the chain
        public Dictionary<string, string> Textures = new Dictionary<string, string>(StringComparer.Ordinal);
        public string RenderType;              // cutout, cutout_mipped, translucent, solid
        public string Loader;
        public bool BuiltinEntity, ItemGenerated;
        public List<string> Chain = new List<string>();

        /// <summary>The final texture id of a "#var" reference ("twilightforest:block/x"), or null.</summary>
        public string Resolve(string tex)
        {
            for (int guard = 0; tex != null && tex.StartsWith("#", StringComparison.Ordinal) && guard < 16; guard++)
            {
                string v;
                if (!Textures.TryGetValue(tex.Substring(1), out v)) return null;
                tex = v;
            }
            if (tex == null || tex.StartsWith("#", StringComparison.Ordinal)) return null;
            return tex.Contains(":") ? tex : "minecraft:" + tex;
        }

        public string Particle
        {
            get
            {
                var p = Resolve("#particle");
                if (p != null) return p;
                foreach (var k in new[] { "all", "side", "texture", "top", "end", "layer0", "cross", "plant", "front" })
                {
                    p = Resolve("#" + k);
                    if (p != null) return p;
                }
                foreach (var k in Textures.Keys.OrderBy(x => x, StringComparer.Ordinal))
                {
                    p = Resolve("#" + k);
                    if (p != null) return p;
                }
                return null;
            }
        }
    }

    public sealed class Models
    {
        readonly Ctx ctx;
        public readonly Dictionary<string, J> Raw = new Dictionary<string, J>(StringComparer.Ordinal);
        readonly Dictionary<string, Model> cache = new Dictionary<string, Model>(StringComparer.Ordinal);
        public readonly SortedSet<string> UnknownParents = new SortedSet<string>(StringComparer.Ordinal);

        public Models(Ctx ctx)
        {
            this.ctx = ctx;
            foreach (var res in new[] { ctx.GenRes, ctx.MainRes })
            {
                string dir = Path.Combine(res, "assets", "twilightforest", "models");
                foreach (var rel in Ctx.Files(dir, "*.json"))
                    Raw["twilightforest:" + Ctx.StripExt(rel)] = Ctx.ReadJson(Path.Combine(dir, rel));
            }
            Vanilla.Register(Raw);
        }

        public static string Norm(string id)
        {
            if (id == null) return null;
            return id.Contains(":") ? id : "minecraft:" + id;
        }

        public Model Get(string id)
        {
            id = Norm(id);
            Model m;
            if (cache.TryGetValue(id, out m)) return m;
            m = new Model { Id = id };
            cache[id] = m;
            var chain = new List<KeyValuePair<string, J>>();
            string cur = id;
            for (int guard = 0; cur != null && guard < 24; guard++)
            {
                J j;
                if (!Raw.TryGetValue(cur, out j))
                {
                    if (cur == "minecraft:builtin/entity") m.BuiltinEntity = true;
                    else if (cur == "minecraft:builtin/generated") m.ItemGenerated = true;
                    else UnknownParents.Add(cur);
                    break;
                }
                chain.Add(new KeyValuePair<string, J>(cur, j));
                if (cur == "minecraft:item/generated" || cur == "minecraft:item/handheld") m.ItemGenerated = true;
                string loader = j["loader"] != null ? j["loader"].AsString() : null;
                // twilightforest:noise_varying picks one of its variant models per position: the first one stands for all
                if (loader == "twilightforest:noise_varying" && j["variants"] != null && j["variants"].Count > 0) { cur = Norm(j["variants"].A[0].AsString()); continue; }
                // twilightforest:patch draws a flat, randomly shaped patch of its texture on the ground: a 1-pixel layer
                if (loader == "twilightforest:patch" && j["texture"] != null)
                {
                    chain.Add(new KeyValuePair<string, J>(cur + "#patch", J.Parse("{\"textures\": {\"texture\": \"" + j["texture"].AsString() + "\", \"particle\": \"#texture\"}, \"render_type\": \"minecraft:cutout\", \"elements\": [{\"from\": [0, 0, 0], \"to\": [16, 1, 16], \"faces\": {\"up\": {\"texture\": \"#texture\"}, \"down\": {\"texture\": \"#texture\"}}}]}")));
                    break;
                }
                // separate_transforms: the base model carries the shape
                if (j["loader"] != null && j["loader"].AsString() == "neoforge:separate_transforms" && j["base"] != null)
                {
                    var b = j["base"];
                    if (b["parent"] != null) { chain.Add(new KeyValuePair<string, J>(cur + "#base", b)); cur = Norm(b["parent"].AsString()); continue; }
                }
                cur = j["parent"] != null ? Norm(j["parent"].AsString()) : null;
            }
            foreach (var kv in chain) m.Chain.Add(kv.Key);
            // textures: the child wins
            for (int i = chain.Count - 1; i >= 0; i--)
            {
                var t = chain[i].Value["textures"];
                if (t != null && t.IsObj) foreach (var k in t.Keys) if (t[k].IsStr) m.Textures[k] = t[k].AsString();
            }
            foreach (var kv in chain)
            {
                var j = kv.Value;
                if (m.RenderType == null && j["render_type"] != null) m.RenderType = j["render_type"].AsString().Replace("minecraft:", "");
                if (m.Loader == null && j["loader"] != null) m.Loader = j["loader"].AsString();
                if (m.Elements == null && j["elements"] != null && j["elements"].IsArr)
                {
                    m.Elements = j["elements"].A.Select(ParseElement).ToList();
                    m.ElementsFrom = kv.Key.Split('#')[0];
                }
                // neoforge:composite: the children's elements together
                if (m.Elements == null && j["loader"] != null && j["loader"].AsString() == "neoforge:composite" && j["children"] != null)
                {
                    var els = new List<Element>();
                    foreach (var ck in j["children"].Keys.OrderBy(x => x, StringComparer.Ordinal))
                    {
                        var child = j["children"][ck];
                        if (child["parent"] != null && child["elements"] == null)
                        {
                            // the child as a model of its own, so its "textures" override its parent's
                            string childId = kv.Key.Split('#')[0] + "#child/" + ck;
                            if (!Raw.ContainsKey(childId)) Raw[childId] = child;
                            var cm = Get(childId);
                            if (cm.Elements != null) els.AddRange(cm.Elements.Select(e => RetargetTextures(e, cm)));
                        }
                        else if (child["elements"] != null) els.AddRange(child["elements"].A.Select(ParseElement));
                        var ct = child["textures"];
                        if (ct != null) foreach (var k in ct.Keys) if (!m.Textures.ContainsKey(k) && ct[k].IsStr) m.Textures[k] = ct[k].AsString();
                    }
                    if (els.Count > 0) { m.Elements = els; m.ElementsFrom = kv.Key.Split('#')[0]; }
                }
            }
            return m;
        }

        // a composite child's faces resolved against the child's textures (so the parent's variables do not capture them)
        static Element RetargetTextures(Element e, Model child)
        {
            var c = new Element { From = e.From, To = e.To, RotOrigin = e.RotOrigin, RotAxis = e.RotAxis, RotAngle = e.RotAngle, Rescale = e.Rescale };
            foreach (var f in e.Faces)
                c.Faces.Add(new Face { Dir = f.Dir, Uv = f.Uv, Rotation = f.Rotation, TintIndex = f.TintIndex, Tex = child.Resolve(f.Tex) ?? f.Tex });
            return c;
        }

        static double[] Vec(J a) { return a.A.Select(x => x.AsDouble()).ToArray(); }

        public static Element ParseElement(J e)
        {
            var el = new Element { From = Vec(e["from"]), To = Vec(e["to"]) };
            var r = e["rotation"];
            if (r != null && r.IsObj)
            {
                el.RotOrigin = r["origin"] != null ? Vec(r["origin"]) : new double[] { 8, 8, 8 };
                el.RotAxis = r["axis"] != null ? r["axis"].AsString() : "y";
                el.RotAngle = r["angle"] != null ? r["angle"].AsDouble() : 0;
                el.Rescale = r["rescale"] != null && r["rescale"].Kind == JKind.Bool && r["rescale"].B;
            }
            var faces = e["faces"];
            if (faces != null)
            {
                foreach (var dir in faces.Keys)
                {
                    var f = faces[dir];
                    var face = new Face { Dir = dir == "bottom" ? "down" : dir, Tex = f["texture"] != null ? f["texture"].AsString() : "#missing" };
                    if (f["uv"] != null) face.Uv = Vec(f["uv"]);
                    if (f["rotation"] != null) face.Rotation = ((int)f["rotation"].AsDouble() % 360 + 360) % 360;
                    if (f["tintindex"] != null) face.TintIndex = (int)f["tintindex"].AsDouble();
                    if (face.Uv == null) face.Uv = AutoUv(face.Dir, el.From, el.To);
                    el.Faces.Add(face);
                }
            }
            return el;
        }

        /// <summary>Java's default face UVs from the element bounds.</summary>
        public static double[] AutoUv(string dir, double[] f, double[] t)
        {
            switch (dir)
            {
                case "down": return new[] { f[0], 16 - t[2], t[0], 16 - f[2] };
                case "up": return new[] { f[0], f[2], t[0], t[2] };
                case "north": return new[] { 16 - t[0], 16 - t[1], 16 - f[0], 16 - f[1] };
                case "south": return new[] { f[0], 16 - t[1], t[0], 16 - f[1] };
                case "west": return new[] { f[2], 16 - t[1], t[2], 16 - f[1] };
                default: return new[] { 16 - t[2], 16 - t[1], 16 - f[2], 16 - f[1] };
            }
        }

        /// <summary>One full, unrotated cube with six faces using default UVs (rendered as minecraft:geometry.full_block).</summary>
        public static bool IsFullCube(Model m)
        {
            if (m.Elements == null || m.Elements.Count != 1) return false;
            var e = m.Elements[0];
            if (e.RotAngle != 0) return false;
            for (int i = 0; i < 3; i++) if (e.From[i] != 0 || e.To[i] != 16) return false;
            if (e.Faces.Count != 6) return false;
            foreach (var f in e.Faces)
            {
                if (f.Rotation != 0 && !(f.Dir == "up" || f.Dir == "down")) return false;
                if (!(f.Uv[0] == 0 && f.Uv[1] == 0 && f.Uv[2] == 16 && f.Uv[3] == 16)) return false;
            }
            return true;
        }

        // ---- Bedrock geometry ----------------------------------------------------------------------

        static double R4(double v) { return Math.Round(v, 4); }
        static J V(params double[] v) { return J.Arr(v.Select(x => J.Num(R4(x)))); }

        /// <summary>Material instance name of a face texture reference ("#side" -> "side").</summary>
        public static string InstanceName(string tex, string prefix)
        {
            string n = tex.StartsWith("#", StringComparison.Ordinal) ? tex.Substring(1) : "t_" + tex.Replace(':', '_').Replace('/', '_');
            return prefix + n;
        }

        /// <summary>Bedrock cubes of Java elements (block space 0..16 -> Bedrock -8..8 with mirrored x).</summary>
        public static List<J> Cubes(IEnumerable<Element> elements, string instancePrefix)
        {
            var cubes = new List<J>();
            foreach (var e in elements)
            {
                double[] f = (double[])e.From.Clone(), t = (double[])e.To.Clone();
                // clamp to Bedrock's block geometry limits (30 x 30 x 30, centred, from y 0)
                for (int i = 0; i < 3; i++)
                {
                    double lo = i == 1 ? 0 : -7, hi = i == 1 ? 30 : 23;
                    f[i] = Math.Max(lo, Math.Min(hi, f[i]));
                    t[i] = Math.Max(lo, Math.Min(hi, t[i]));
                }
                double[] size = { t[0] - f[0], t[1] - f[1], t[2] - f[2] };
                if (size[0] < 0 || size[1] < 0 || size[2] < 0) continue;
                var cube = J.Obj();
                double[] origin = { 8 - t[0], f[1], f[2] - 8 };
                if (e.RotAngle != 0 && e.Rescale)
                {
                    // Java rescales the faces across the rotation axis by 1/cos(angle)
                    double s = 1 / Math.Cos(Math.Abs(e.RotAngle) * Math.PI / 180);
                    int[] axes = e.RotAxis == "x" ? new[] { 1, 2 } : e.RotAxis == "y" ? new[] { 0, 2 } : new[] { 0, 1 };
                    double[] piv = { 8 - e.RotOrigin[0], e.RotOrigin[1], e.RotOrigin[2] - 8 };
                    foreach (var a in axes)
                    {
                        origin[a] = piv[a] + (origin[a] - piv[a]) * s;
                        size[a] *= s;
                    }
                }
                cube.Set("origin", V(origin));
                cube.Set("size", V(size));
                if (e.RotAngle != 0)
                {
                    cube.Set("pivot", V(8 - e.RotOrigin[0], e.RotOrigin[1], e.RotOrigin[2] - 8));
                    double a = e.RotAngle;
                    // Bedrock geometry rotations are [-x, -y, z] of Java's (the mirrored x axis; as Blockbench exports them)
                    cube.Set("rotation", e.RotAxis == "x" ? V(-a, 0, 0) : e.RotAxis == "y" ? V(0, -a, 0) : V(0, 0, a));
                }
                var uv = J.Obj();
                foreach (var face in e.Faces.OrderBy(x => DirOrder(x.Dir)))
                {
                    var fu = face.Uv;
                    double u = fu[0], v = fu[1], w = fu[2] - fu[0], h = fu[3] - fu[1];
                    if (face.Dir == "up" || face.Dir == "down") { u = fu[2]; v = fu[3]; w = -w; h = -h; }
                    var fo = J.Obj().Set("uv", V(u, v)).Set("uv_size", V(w, h));
                    if (face.Rotation != 0) fo.Set("uv_rotation", face.Rotation);
                    fo.Set("material_instance", InstanceName(face.Tex, instancePrefix));
                    uv.Set(face.Dir, fo);
                }
                if (uv.Count == 0) continue;
                cube.Set("uv", uv);
                cubes.Add(cube);
            }
            return cubes;
        }

        static int DirOrder(string d) { return Array.IndexOf(new[] { "north", "east", "south", "west", "up", "down" }, d); }

        public static J GeometryFile(string identifier, List<J> bones)
        {
            var desc = J.Obj()
                .Set("identifier", identifier)
                .Set("texture_width", 16)
                .Set("texture_height", 16)
                .Set("visible_bounds_width", 3)
                .Set("visible_bounds_height", 3)
                .Set("visible_bounds_offset", V(0, 0.75, 0));
            var geo = J.Obj().Set("description", desc).Set("bones", J.Arr(bones));
            return J.Obj().Set("format_version", "1.21.0").Set("minecraft:geometry", J.Arr(new[] { geo }));
        }

        /// <summary>The bounding box of elements in Java block coordinates (clamped to the block), or null.</summary>
        public static double[] Bounds(IEnumerable<Element> elements)
        {
            double[] lo = { 16, 16, 16 }, hi = { 0, 0, 0 };
            bool any = false;
            foreach (var e in elements)
            {
                for (int i = 0; i < 3; i++)
                {
                    lo[i] = Math.Min(lo[i], Math.Min(e.From[i], e.To[i]));
                    hi[i] = Math.Max(hi[i], Math.Max(e.From[i], e.To[i]));
                }
                any = true;
            }
            if (!any) return null;
            for (int i = 0; i < 3; i++) { lo[i] = Math.Max(0, Math.Min(16, lo[i])); hi[i] = Math.Max(0, Math.Min(16, hi[i])); }
            return new[] { lo[0], lo[1], lo[2], hi[0], hi[1], hi[2] };
        }
    }

    /// <summary>The vanilla parent models TF extends, as plain shapes (Minecraft 1.21.1 geometry).</summary>
    public static class Vanilla
    {
        static J El(double fx, double fy, double fz, double tx, double ty, double tz, string faces, J rotation = null)
        {
            // faces: "north:#side,south:#side,..." ; an optional "@u1;v1;u2;v2" after a texture sets the uv, "^90" a rotation
            var e = J.Obj().Set("from", J.Arr(new[] { J.Num(fx), J.Num(fy), J.Num(fz) })).Set("to", J.Arr(new[] { J.Num(tx), J.Num(ty), J.Num(tz) }));
            if (rotation != null) e.Set("rotation", rotation);
            var fo = J.Obj();
            foreach (var part in faces.Split(','))
            {
                var kv = part.Split(':');
                string dir = kv[0], spec = kv[1];
                var face = J.Obj();
                int rot = 0;
                int caret = spec.IndexOf('^');
                if (caret >= 0) { rot = int.Parse(spec.Substring(caret + 1), CultureInfo.InvariantCulture); spec = spec.Substring(0, caret); }
                int at = spec.IndexOf('@');
                if (at >= 0)
                {
                    var uv = spec.Substring(at + 1).Split(';').Select(x => J.Num(double.Parse(x, CultureInfo.InvariantCulture)));
                    face.Set("uv", J.Arr(uv));
                    spec = spec.Substring(0, at);
                }
                face.Set("texture", spec);
                if (rot != 0) face.Set("rotation", rot);
                fo.Set(dir, face);
            }
            return e.Set("faces", fo);
        }

        static J Rot(double ox, double oy, double oz, string axis, double angle, bool rescale)
        {
            return J.Obj().Set("origin", J.Arr(new[] { J.Num(ox), J.Num(oy), J.Num(oz) })).Set("axis", axis).Set("angle", angle).Set("rescale", rescale);
        }

        static string All(string tex) { return "down:" + tex + ",up:" + tex + ",north:" + tex + ",south:" + tex + ",west:" + tex + ",east:" + tex; }

        static void Def(Dictionary<string, J> raw, string id, string parent, J textures, string renderType, params J[] elements)
        {
            var m = J.Obj();
            if (parent != null) m.Set("parent", parent);
            if (textures != null) m.Set("textures", textures);
            if (renderType != null) m.Set("render_type", renderType);
            if (elements.Length > 0) m.Set("elements", J.Arr(elements));
            if (!raw.ContainsKey(id)) raw[id] = m;
        }

        static J T(params string[] kv)
        {
            var o = J.Obj();
            for (int i = 0; i + 1 < kv.Length; i += 2) o.Set(kv[i], kv[i + 1]);
            return o;
        }

        public static void Register(Dictionary<string, J> raw)
        {
            const string B = "minecraft:block/";
            Def(raw, B + "block", null, null, null);
            Def(raw, B + "thin_block", B + "block", null, null);
            Def(raw, B + "cube", B + "block", null, null, El(0, 0, 0, 16, 16, 16, "down:#down,up:#up,north:#north,south:#south,west:#west,east:#east"));
            Def(raw, B + "cube_all", B + "cube", T("particle", "#all", "down", "#all", "up", "#all", "north", "#all", "east", "#all", "south", "#all", "west", "#all"), null);
            Def(raw, B + "cube_column", B + "cube", T("particle", "#side", "down", "#end", "up", "#end", "north", "#side", "east", "#side", "south", "#side", "west", "#side"), null);
            Def(raw, B + "cube_column_horizontal", B + "block", T("particle", "#side"), null,
                El(0, 0, 0, 16, 16, 16, "down:#side,up:#side^180,north:#end,south:#end,west:#side^270,east:#side^90"));
            Def(raw, B + "cube_bottom_top", B + "cube", T("particle", "#side", "down", "#bottom", "up", "#top", "north", "#side", "east", "#side", "south", "#side", "west", "#side"), null);
            Def(raw, B + "cube_top", B + "cube", T("particle", "#side", "down", "#side", "up", "#top", "north", "#side", "east", "#side", "south", "#side", "west", "#side"), null);
            Def(raw, B + "orientable_with_bottom", B + "cube", T("particle", "#front", "down", "#bottom", "up", "#top", "north", "#front", "east", "#side", "south", "#side", "west", "#side"), null);
            Def(raw, B + "orientable", B + "orientable_with_bottom", T("bottom", "#top"), null);
            Def(raw, B + "furnace", B + "orientable", T("front", "minecraft:block/furnace_front", "side", "minecraft:block/furnace_side", "top", "minecraft:block/furnace_top"), null);
            Def(raw, B + "furnace_on", B + "orientable", T("front", "minecraft:block/furnace_front_on", "side", "minecraft:block/furnace_side", "top", "minecraft:block/furnace_top"), null);
            Def(raw, B + "skull", "minecraft:builtin/entity", T("particle", "minecraft:block/soul_sand"), null);
            Def(raw, B + "leaves", B + "cube_all", null, "minecraft:cutout_mipped");
            Def(raw, B + "oak_leaves", B + "leaves", T("all", "minecraft:block/oak_leaves"), null);
            Def(raw, B + "azalea_leaves", B + "cube_column", T("end", "minecraft:block/azalea_leaves", "side", "minecraft:block/azalea_leaves"), "minecraft:cutout_mipped");
            foreach (var n in new[] { "gold_block", "diamond_block", "obsidian", "cobblestone", "dirt", "stone" })
                Def(raw, B + n, B + "cube_all", T("all", "minecraft:block/" + n), null);
            Def(raw, B + "slime_block", B + "cube_all", T("all", "minecraft:block/slime_block"), "minecraft:translucent");
            Def(raw, B + "spawner", B + "cube_all", T("all", "minecraft:block/spawner"), "minecraft:cutout");
            Def(raw, B + "oak_log", B + "cube_column", T("end", "minecraft:block/oak_log_top", "side", "minecraft:block/oak_log"), null);
            Def(raw, B + "chiseled_bookshelf", B + "orientable", T("front", "minecraft:block/chiseled_bookshelf_empty", "side", "minecraft:block/chiseled_bookshelf_side", "top", "minecraft:block/chiseled_bookshelf_top"), null);
            foreach (var n in new[] { "top_left", "top_mid", "top_right", "bottom_left", "bottom_mid", "bottom_right" })
                Def(raw, B + "template_chiseled_bookshelf_slot_" + n, null, null, null);

            const string S = "down:#bottom,up:#top,north:#side,south:#side,west:#side,east:#side";
            Def(raw, B + "slab", null, T("particle", "#side"), null, El(0, 0, 0, 16, 8, 16, S));
            Def(raw, B + "slab_top", null, T("particle", "#side"), null, El(0, 8, 0, 16, 16, 16, S));
            Def(raw, B + "stairs", null, T("particle", "#side"), null,
                El(0, 0, 0, 16, 8, 16, S), El(8, 8, 0, 16, 16, 16, "up:#top,north:#side,south:#side,west:#side,east:#side"));
            Def(raw, B + "inner_stairs", null, T("particle", "#side"), null,
                El(0, 0, 0, 16, 8, 16, S), El(8, 8, 0, 16, 16, 16, "up:#top,north:#side,south:#side,west:#side,east:#side"),
                El(0, 8, 8, 8, 16, 16, "up:#top,north:#side,south:#side,west:#side"));
            Def(raw, B + "outer_stairs", null, T("particle", "#side"), null,
                El(0, 0, 0, 16, 8, 16, S), El(8, 8, 8, 16, 16, 16, "up:#top,north:#side,south:#side,west:#side,east:#side"));

            Def(raw, B + "fence_post", null, T("particle", "#texture"), null, El(6, 0, 6, 10, 16, 10, All("#texture")));
            Def(raw, B + "fence_side", null, T("particle", "#texture"), null,
                El(7, 12, 0, 9, 15, 9, "down:#texture,up:#texture,north:#texture,west:#texture,east:#texture"),
                El(7, 6, 0, 9, 9, 9, "down:#texture,up:#texture,north:#texture,west:#texture,east:#texture"));
            Def(raw, B + "fence_inventory", null, T("particle", "#texture"), null,
                El(6, 0, 0, 10, 16, 4, All("#texture")), El(6, 0, 12, 10, 16, 16, All("#texture")),
                El(7, 13, -2, 9, 15, 18, All("#texture")), El(7, 5, -2, 9, 7, 18, All("#texture")));
            Def(raw, B + "template_fence_gate", null, T("particle", "#texture"), null,
                El(0, 5, 7, 2, 16, 9, All("#texture")), El(14, 5, 7, 16, 16, 9, All("#texture")),
                El(6, 6, 7, 8, 15, 9, All("#texture")), El(8, 6, 7, 10, 15, 9, All("#texture")),
                El(2, 6, 7, 6, 9, 9, All("#texture")), El(2, 12, 7, 6, 15, 9, All("#texture")),
                El(10, 6, 7, 14, 9, 9, All("#texture")), El(10, 12, 7, 14, 15, 9, All("#texture")));
            Def(raw, B + "template_fence_gate_open", null, T("particle", "#texture"), null,
                El(0, 5, 7, 2, 16, 9, All("#texture")), El(14, 5, 7, 16, 16, 9, All("#texture")),
                El(0, 6, 13, 2, 15, 15, All("#texture")), El(14, 6, 13, 16, 15, 15, All("#texture")),
                El(0, 6, 9, 2, 9, 13, All("#texture")), El(0, 12, 9, 2, 15, 13, All("#texture")),
                El(14, 6, 9, 16, 9, 13, All("#texture")), El(14, 12, 9, 16, 15, 13, All("#texture")));
            Def(raw, B + "template_fence_gate_wall", null, T("particle", "#texture"), null,
                El(0, 2, 7, 2, 13, 9, All("#texture")), El(14, 2, 7, 16, 13, 9, All("#texture")),
                El(6, 3, 7, 8, 12, 9, All("#texture")), El(8, 3, 7, 10, 12, 9, All("#texture")),
                El(2, 3, 7, 6, 6, 9, All("#texture")), El(2, 9, 7, 6, 12, 9, All("#texture")),
                El(10, 3, 7, 14, 6, 9, All("#texture")), El(10, 9, 7, 14, 12, 9, All("#texture")));
            Def(raw, B + "template_fence_gate_wall_open", null, T("particle", "#texture"), null,
                El(0, 2, 7, 2, 13, 9, All("#texture")), El(14, 2, 7, 16, 13, 9, All("#texture")),
                El(0, 3, 13, 2, 12, 15, All("#texture")), El(14, 3, 13, 16, 12, 15, All("#texture")),
                El(0, 3, 9, 2, 6, 13, All("#texture")), El(0, 9, 9, 2, 12, 13, All("#texture")),
                El(14, 3, 9, 16, 6, 13, All("#texture")), El(14, 9, 9, 16, 12, 13, All("#texture")));
            Def(raw, B + "button", null, T("particle", "#texture"), null, El(5, 0, 6, 11, 2, 10, All("#texture")));
            Def(raw, B + "button_pressed", null, T("particle", "#texture"), null, El(5, 0, 6, 11, 1, 10, All("#texture")));
            Def(raw, B + "button_inventory", null, T("particle", "#texture"), null, El(5, 6, 6, 11, 10, 10, All("#texture")));
            Def(raw, B + "pressure_plate_up", null, T("particle", "#texture"), null, El(1, 0, 1, 15, 1, 15, All("#texture")));
            Def(raw, B + "pressure_plate_down", null, T("particle", "#texture"), null, El(1, 0, 1, 15, 0.5, 15, All("#texture")));
            Def(raw, B + "carpet", null, T("particle", "#wool"), null, El(0, 0, 0, 16, 1, 16, All("#wool")));
            Def(raw, B + "template_single_face", null, T("particle", "#texture"), null, El(0, 0, 0, 16, 16, 16, "north:#texture"));
            Def(raw, B + "template_farmland", null, T("particle", "#dirt"), null,
                El(0, 0, 0, 16, 15, 16, "down:#dirt,up:#top,north:#dirt,south:#dirt,west:#dirt,east:#dirt"));

            var r45 = Rot(8, 8, 8, "y", 45, true);
            Def(raw, B + "cross", null, T("particle", "#cross"), "minecraft:cutout",
                El(0.8, 0, 8, 15.2, 16, 8, "north:#cross@0;0;16;16,south:#cross@0;0;16;16", r45),
                El(8, 0, 0.8, 8, 16, 15.2, "west:#cross@0;0;16;16,east:#cross@0;0;16;16", r45));
            Def(raw, B + "tinted_cross", B + "cross", null, null);
            var pot = new[] {
                El(5, 0, 5, 6, 6, 11, "down:#flowerpot,up:#flowerpot,north:#flowerpot,south:#flowerpot,west:#flowerpot,east:#flowerpot"),
                El(10, 0, 5, 11, 6, 11, "down:#flowerpot,up:#flowerpot,north:#flowerpot,south:#flowerpot,west:#flowerpot,east:#flowerpot"),
                El(6, 0, 5, 10, 6, 6, "down:#flowerpot,up:#flowerpot,north:#flowerpot,south:#flowerpot"),
                El(6, 0, 10, 10, 6, 11, "down:#flowerpot,up:#flowerpot,north:#flowerpot,south:#flowerpot"),
                El(6, 0, 6, 10, 4, 10, "down:#flowerpot,up:#dirt"),
                El(2.6, 4, 8, 13.4, 16, 8, "north:#plant@0;0;16;16,south:#plant@0;0;16;16", r45),
                El(8, 4, 2.6, 8, 16, 13.4, "west:#plant@0;0;16;16,east:#plant@0;0;16;16", r45),
            };
            Def(raw, B + "flower_pot_cross", null, T("particle", "#flowerpot", "flowerpot", "minecraft:block/flower_pot", "dirt", "minecraft:block/dirt"), "minecraft:cutout", pot);
            Def(raw, B + "tinted_flower_pot_cross", B + "flower_pot_cross", null, null);

            foreach (var side in new[] { "left", "right" })
                foreach (var open in new[] { "", "_open" })
                {
                    Def(raw, B + "door_bottom_" + side + open, null, T("particle", "#bottom"), "minecraft:cutout",
                        El(0, 0, 0, 3, 16, 16, "down:#bottom@13;0;16;16,north:#bottom@3;0;0;16,south:#bottom@0;0;3;16,west:#bottom@0;0;16;16,east:#bottom@16;0;0;16"));
                    Def(raw, B + "door_top_" + side + open, null, T("particle", "#top"), "minecraft:cutout",
                        El(0, 0, 0, 3, 16, 16, "up:#top@13;0;16;16,north:#top@3;0;0;16,south:#top@0;0;3;16,west:#top@0;0;16;16,east:#top@16;0;0;16"));
                }
            Def(raw, B + "template_orientable_trapdoor_bottom", null, T("particle", "#texture"), "minecraft:cutout", El(0, 0, 0, 16, 3, 16, All("#texture")));
            Def(raw, B + "template_orientable_trapdoor_top", null, T("particle", "#texture"), "minecraft:cutout", El(0, 13, 0, 16, 16, 16, All("#texture")));
            Def(raw, B + "template_orientable_trapdoor_open", null, T("particle", "#texture"), "minecraft:cutout", El(0, 0, 13, 16, 16, 16, All("#texture")));
            Def(raw, B + "template_trapdoor_bottom", null, T("particle", "#texture"), "minecraft:cutout", El(0, 0, 0, 16, 3, 16, All("#texture")));
            Def(raw, B + "template_trapdoor_top", null, T("particle", "#texture"), "minecraft:cutout", El(0, 13, 0, 16, 16, 16, All("#texture")));
            Def(raw, B + "template_trapdoor_open", null, T("particle", "#texture"), "minecraft:cutout", El(0, 0, 13, 16, 16, 16, All("#texture")));

            Def(raw, B + "template_glass_pane_post", null, T("particle", "#pane"), "minecraft:cutout", El(7, 0, 7, 9, 16, 9, "down:#edge,up:#edge"));
            Def(raw, B + "template_glass_pane_side", null, T("particle", "#pane"), "minecraft:cutout", El(7, 0, 0, 9, 16, 7, "down:#edge,up:#edge,north:#edge,west:#pane,east:#pane"));
            Def(raw, B + "template_glass_pane_side_alt", null, T("particle", "#pane"), "minecraft:cutout", El(7, 0, 9, 9, 16, 16, "down:#edge,up:#edge,south:#edge,west:#pane,east:#pane"));
            Def(raw, B + "template_glass_pane_noside", null, T("particle", "#pane"), "minecraft:cutout", El(7, 0, 7, 9, 16, 9, "north:#pane"));
            Def(raw, B + "template_glass_pane_noside_alt", null, T("particle", "#pane"), "minecraft:cutout", El(7, 0, 7, 9, 16, 9, "east:#pane"));

            Def(raw, B + "template_fire_floor", null, T("particle", "#fire"), "minecraft:cutout",
                El(0, 0, 8.8, 16, 22.4, 8.8, "south:#fire@0;0;16;16", Rot(8, 8, 8, "x", -22.5, true)),
                El(0, 0, 7.2, 16, 22.4, 7.2, "north:#fire@0;0;16;16", Rot(8, 8, 8, "x", 22.5, true)),
                El(8.8, 0, 0, 8.8, 22.4, 16, "west:#fire@0;0;16;16", Rot(8, 8, 8, "z", -22.5, true)),
                El(7.2, 0, 0, 7.2, 22.4, 16, "east:#fire@0;0;16;16", Rot(8, 8, 8, "z", 22.5, true)));
            Def(raw, B + "template_fire_side", null, T("particle", "#fire"), "minecraft:cutout",
                El(0, 0, 0.01, 16, 22.4, 0.01, "south:#fire@0;0;16;16,north:#fire@0;0;16;16"));
            Def(raw, B + "template_fire_side_alt", null, T("particle", "#fire"), "minecraft:cutout",
                El(0, 0, 0.01, 16, 22.4, 0.01, "south:#fire@16;0;0;16,north:#fire@16;0;0;16"));

            // item parents
            const string I = "minecraft:item/";
            Def(raw, I + "generated", null, null, null);
            Def(raw, I + "handheld", I + "generated", null, null);
            Def(raw, I + "handheld_rod", I + "handheld", null, null);
            Def(raw, I + "template_spawn_egg", I + "generated", T("layer0", "minecraft:item/spawn_egg", "layer1", "minecraft:item/spawn_egg_overlay"), null);
            Def(raw, I + "chest", "minecraft:builtin/entity", T("particle", "minecraft:block/oak_planks"), null);
            Def(raw, I + "bow", I + "generated", T("layer0", "minecraft:item/bow"), null);
            Def(raw, I + "goat_horn", I + "generated", T("layer0", "minecraft:item/goat_horn"), null);
            Def(raw, I + "tooting_goat_horn", I + "generated", T("layer0", "minecraft:item/goat_horn"), null);
            Def(raw, I + "stone_sword", I + "handheld", T("layer0", "minecraft:item/stone_sword"), null);
            Def(raw, I + "stone_pickaxe", I + "handheld", T("layer0", "minecraft:item/stone_pickaxe"), null);
            Def(raw, "neoforge:item/default", null, null, null);
        }
    }
}
