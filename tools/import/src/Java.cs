// GloamingForest import tool (contract "import", unit F1): reads upstream's registration code
// (init/GloamingBlocks.java, GloamingItems.java, GloamingEntities.java, GloamingCreativeTabs.java) for the facts the data
// files do not carry: block classes and properties, item stack sizes, entity sizes and spawn egg
// colours, and the creative tab order. Facts only; no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class JBlock
    {
        public string Id, Const, Cls, Stmt;
        public string PropText;    // the text the properties are read from (helper body + statement + named property sets)
        // BlockBehaviour.Properties.of(): destroyTime 0, explosionResistance 0, SoundType.STONE (Minecraft 1.21.1)
        public double Hardness = 0, Resistance = 0;
        public int Light = 0;
        public bool DynamicLight, NoCollision, NoOcclusion, Unbreakable, Flammable, Replaceable;
        public bool RequiresTool;      // requiresCorrectToolForDrops(): only a correct tool (upstream's / vanilla's tool tags) drops it
        /// <summary>instabreak() = strength(0): from the final values, whatever call set them.</summary>
        public bool Instabreak { get { return Hardness == 0 && Resistance == 0; } }
        public string Sound = "STONE", MapColor;
        public string MapColorSide;    // logProperties(top, side): the colour of a lying log (axis x / z)
        public string MapColorProp, MapColorIf;   // mapColor(state -> state.getValue(P) ? IF : MapColor): P's constant, IF (null: NONE)
        public double Friction = -1;
        public int FireEncouragement = -1, FireFlammability = -1;   // FireBlock.setFlammable (RegistrationEvents)
        public string LightExpr;   // the lightLevel lambda body when it is not a constant
        public string CopyOf;      // upstream constant or "Blocks.X"
        public double CopyScale = 1;   // copyAndScaleProperties(X, scale): destroyTime and explosionResistance x scale
    }

    public sealed class JItem
    {
        public string Id, Const, Cls, Stmt;
        public int StackSize = 64, Durability = 0;
        public List<string> Chain = new List<string>();
        public string ToolKind;            // pickaxe | axe | shovel | hoe | sword (the vanilla tool class reached), or null
        public Java.TierInfo Tier;         // the tool tier (GloamingToolMaterials / Tiers), or null
        public string TierName;            // "GloamingToolMaterials.IRONWOOD"
        public bool FireResistant;
        public string Rarity;
        public bool Food;
    }

    public sealed class JEntity
    {
        public string Id, Cls, Category;
        public double Width = 0.6, Height = 1.8;
        public bool HasEgg, FireImmune;
        public int EggPrimary, EggSecondary;
    }

    public static class Java
    {
        public static string StripComments(string text)
        {
            text = Regex.Replace(text, @"/\*.*?\*/", "", RegexOptions.Singleline);
            return Regex.Replace(text, @"(?m)^\s*//.*$|//[^\n""]*$", "");
        }

        /// <summary>Statements "public static final Type NAME = ...;" (parentheses balanced), in file order.</summary>
        public static IEnumerable<Tuple<string, string, string>> Fields(string text, string typePrefix)
        {
            var re = new Regex(@"public\s+static\s+final\s+(" + typePrefix + @"[^=;]*?)\s+(\w+)\s*=\s*", RegexOptions.Compiled);
            foreach (Match m in re.Matches(text))
            {
                int i = m.Index + m.Length, depth = 0;
                bool inStr = false;
                int start = i;
                for (; i < text.Length; i++)
                {
                    char c = text[i];
                    if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                    if (c == '"') inStr = true;
                    else if (c == '(' || c == '{') depth++;
                    else if (c == ')' || c == '}') depth--;
                    else if (c == ';' && depth <= 0) break;
                }
                yield return Tuple.Create(m.Groups[1].Value, m.Groups[2].Value, text.Substring(start, i - start));
            }
        }

        static double F(string s) { return double.Parse(s.TrimEnd('F', 'f', 'D', 'd'), CultureInfo.InvariantCulture); }

        /// <summary>A sound type of upstream's own (init/GloamingSoundTypes.java): NeoForge's DeferredSoundType(volume, pitch,
        /// break, step, place, hit, fall), the sounds as upstream's sound event names (init/GloamingSounds.java createEvent).</summary>
        public sealed class SoundTypeInfo
        {
            public string Name;            // "JAR"
            public double Volume, Pitch;
            public string Break, Step, Place, Hit, Fall;
        }

        /// <summary>upstream's sound types by constant name.</summary>
        public static Dictionary<string, SoundTypeInfo> SoundTypes(Ctx ctx)
        {
            var events = new Dictionary<string, string>(StringComparer.Ordinal);
            string sounds = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingSounds.java")));
            foreach (Match m in Regex.Matches(sounds, @"\b(\w+)\s*=\s*createEvent\(\s*""([a-z0-9_.]+)""\s*\)")) events[m.Groups[1].Value] = m.Groups[2].Value;
            var types = new Dictionary<string, SoundTypeInfo>(StringComparer.Ordinal);
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingSoundTypes.java")));
            foreach (var f in Fields(text, @"DeferredSoundType"))
            {
                var m = Regex.Match(f.Item3, @"^new\s+DeferredSoundType\(\s*([\d.]+[fF]?)\s*,\s*([\d.]+[fF]?)\s*,\s*GloamingSounds\.(\w+)\s*,\s*GloamingSounds\.(\w+)\s*,\s*GloamingSounds\.(\w+)\s*,\s*GloamingSounds\.(\w+)\s*,\s*GloamingSounds\.(\w+)\s*\)$");
                if (!m.Success) { ctx.Warn("GloamingSoundTypes." + f.Item2 + ": not a DeferredSoundType(volume, pitch, break, step, place, hit, fall) of GloamingSounds: no Bedrock block sound"); continue; }
                Func<int, string> ev = g =>
                {
                    string e;
                    if (events.TryGetValue(m.Groups[g].Value, out e)) return e;
                    ctx.Warn("GloamingSoundTypes." + f.Item2 + ": GloamingSounds." + m.Groups[g].Value + " is not a createEvent sound event");
                    return null;
                };
                var t = new SoundTypeInfo { Name = f.Item2, Volume = F(m.Groups[1].Value), Pitch = F(m.Groups[2].Value), Break = ev(3), Step = ev(4), Place = ev(5), Hit = ev(6), Fall = ev(7) };
                if (t.Break != null && t.Step != null && t.Place != null && t.Hit != null && t.Fall != null) types[t.Name] = t;
            }
            return types;
        }

        public static Dictionary<string, JBlock> Blocks(Ctx ctx)
        {
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingBlocks.java")));
            var props = new Dictionary<string, string>();
            foreach (var f in Fields(text, @"BlockBehaviour\.Properties")) props[f.Item2] = f.Item3;
            var helpers = new Dictionary<string, string>();
            var helperParams = new Dictionary<string, List<string>>();
            foreach (Match m in Regex.Matches(text, @"public static DeferredBlock<(\w+)> (\w+)\((String name[^)]*)\)\s*\{(.*?)\n\t\}", RegexOptions.Singleline))
            {
                helpers[m.Groups[2].Value] = m.Groups[1].Value + "|" + m.Groups[4].Value;
                // the parameter names, in order ("String name, MapColor mapColor, Block candle" -> name, mapColor, candle)
                helperParams[m.Groups[2].Value] = m.Groups[3].Value.Split(',').Select(p => p.Trim().Split(' ').Last()).ToList();
            }
            // methods returning BlockBehaviour.Properties anywhere in upstream: the import reads logProperties (map colour)
            // and copyAndScaleProperties (a copy with scaled destroyTime / explosionResistance); a block whose
            // properties come from any other one is a warning (one pass over the class-file index's texts)
            var propHelpers = new SortedSet<string>(StringComparer.Ordinal);
            foreach (var jf in AllClassFiles(ctx))
                foreach (Match m in Regex.Matches(SourceText(jf), @"\bstatic\s+(?:final\s+)?BlockBehaviour\.Properties\s+(\w+)\s*\("))
                    if (!KnownPropHelpers.Contains(m.Groups[1].Value)) propHelpers.Add(m.Groups[1].Value);
            var byConst = new Dictionary<string, JBlock>();
            var res = new Dictionary<string, JBlock>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredBlock<"))
            {
                var idm = Regex.Match(f.Item3, @"^\s*([\w.]+)\(\s*""([a-z0-9_]+)""");
                if (!idm.Success) continue;
                var b = new JBlock { Id = idm.Groups[2].Value, Const = f.Item2, Stmt = f.Item3 };
                string body = f.Item3;
                string call = idm.Groups[1].Value;
                string helper;
                if (helpers.TryGetValue(call, out helper) && !body.Contains("new "))
                {
                    var hp = helper.Split(new[] { '|' }, 2);
                    b.Cls = hp[0];
                    // the helper body with its parameters replaced by this call's arguments (ominousCandle(name, MapColor.SAND, ...))
                    string hbody = hp[1];
                    var args = SplitArgs(body.Substring(body.IndexOf('(') + 1));
                    var pars = helperParams[call];
                    for (int i = 1; i < pars.Count && i < args.Count; i++)
                        hbody = Regex.Replace(hbody, @"(?<![\w.])" + Regex.Escape(pars[i]) + @"\b", args[i].Trim().Replace("$", "$$"));
                    body = hbody + body;
                }
                var cm = Regex.Match(body, @"new\s+(\w+)\s*[(<]");
                if (b.Cls == null) b.Cls = cm.Success ? cm.Groups[1].Value : Regex.Match(f.Item1, @"DeferredBlock<(\w+)").Groups[1].Value;
                // inline named property sets in place (GLOAMING_OAK_LOG_PROPS): calls chained onto the set apply after its own
                body = Regex.Replace(body, @"\b([A-Z][A-Z0-9_]*_PROPS)\b", pm => { string p; return props.TryGetValue(pm.Groups[1].Value, out p) ? p : pm.Value; });
                b.PropText = body;
                foreach (var h in propHelpers)
                    if (Regex.IsMatch(body, @"\b" + h + @"\s*\(")) unknownHelpers.Add(b.Id + ": " + h);
                if (!Regex.IsMatch(body, @"Properties\.of\(|of(?:Full|Legacy)Copy\(|\b(?:" + string.Join("|", KnownPropHelpers) + @")\s*\(")
                    && !propHelpers.Any(h => Regex.IsMatch(body, @"\b" + h + @"\s*\(")))
                    noProperties.Add(b.Id);
                ParseProps(b, body);
                res[b.Id] = b;
                byConst[b.Const] = b;
            }
            // property copies: ofFullCopy(OTHER.get()) / ofFullCopy(OTHER) / Blocks.X, and copyAndScaleProperties(X, k)
            // (GloamingBlocks: ofFullCopy(X), then destroyTime and explosionResistance x k), then the calls chained after the
            // copy; then the calls an upstream block class chains onto the properties in its constructor (SpecialMagicLogBlock:
            // super(properties.strength(2.0F).sound(SoundType.WOOD)...)), the leaf class first. A block is resolved
            // completely (its own copy and its class's calls) before a block copies it: Java's ofFullCopy reads the
            // Properties the source block's constructor has already changed (chains: A copies B copies C)
            var resolved = new HashSet<JBlock>();
            var chains = new Dictionary<string, string>(StringComparer.Ordinal);
            Action<JBlock> resolve = null;
            resolve = b =>
            {
                if (!resolved.Add(b)) return;
                if (b.CopyOf != null)
                {
                    JBlock src;
                    var cm = CopyCall.Match(b.PropText);
                    string after = cm.Success ? b.PropText.Substring(cm.Index + cm.Length) : "";
                    bool copied = true;
                    if (byConst.TryGetValue(b.CopyOf, out src) && src != b)
                    {
                        resolve(src);
                        b.Hardness = src.Hardness; b.Resistance = src.Resistance; b.Sound = src.Sound; b.MapColor = src.MapColor;
                        b.MapColorSide = src.MapColorSide; b.MapColorProp = src.MapColorProp; b.MapColorIf = src.MapColorIf;
                        b.Light = src.Light; b.Flammable = src.Flammable; b.Unbreakable = src.Unbreakable; b.RequiresTool = src.RequiresTool;
                        b.LightExpr = src.LightExpr; b.DynamicLight = src.DynamicLight; b.Friction = src.Friction;
                        b.NoCollision = src.NoCollision; b.NoOcclusion = src.NoOcclusion; b.Replaceable = src.Replaceable;
                    }
                    else if (b.CopyOf.StartsWith("Blocks.", StringComparison.Ordinal)) VanillaProps(b, b.CopyOf.Substring(7));
                    else { unknownCopies.Add(b.Id + ": " + b.CopyOf); copied = false; }
                    if (copied)
                    {
                        b.Hardness *= b.CopyScale;
                        b.Resistance *= b.CopyScale;
                        ParseProps(b, after, false); // explicit settings after the copy win
                    }
                }
                foreach (var cls in ClassChain(ctx, b.Cls, chains))
                {
                    string ct = ClassText(ctx, cls);
                    if (ct == null) break;
                    foreach (Match sm in Regex.Matches(ct, @"\bsuper\(\s*\w+((?:\s*\.\s*\w+\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\))+)\s*\)\s*;"))
                        ParseProps(b, sm.Groups[1].Value, false);
                }
            };
            foreach (var b in res.Values) resolve(b);
            // flammability: only the blocks upstream registers with FireBlock.setFlammable(block, encouragement,
            // flammability) in events/RegistrationEvents.java burn (ignitedByLava alone does not make a block
            // burn in Java)
            string reg = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "events", "RegistrationEvents.java")));
            foreach (Match fm in Regex.Matches(reg, @"\.setFlammable\(\s*GloamingBlocks\.(\w+)(?:\.get\(\))?\s*,\s*(\d+)\s*,\s*(\d+)\s*\)"))
            {
                JBlock b;
                if (!byConst.TryGetValue(fm.Groups[1].Value, out b)) { ctx.Warn("setFlammable: unknown block constant " + fm.Groups[1].Value); continue; }
                b.FireEncouragement = int.Parse(fm.Groups[2].Value, CultureInfo.InvariantCulture);
                b.FireFlammability = int.Parse(fm.Groups[3].Value, CultureInfo.InvariantCulture);
            }
            // (HARDENED_DARK_LEAVES is registered twice, with the same values)
            int calls = Regex.Matches(reg, @"\.setFlammable\(\s*([\w.]+)").Cast<Match>().Select(x => x.Groups[1].Value).Distinct().Count();
            int parsed = res.Values.Count(b => b.FireEncouragement >= 0);
            if (calls != parsed) ctx.Warn("RegistrationEvents: setFlammable registers " + calls + " blocks, " + parsed + " parsed");
            foreach (var v in unknownVanilla) ctx.Warn("upstream copies the properties of Blocks." + v + ", which Java.VanillaProps does not know");
            foreach (var v in unknownCopies) ctx.Warn("block " + v + ": the copied block is neither an upstream block nor Blocks.X (properties not copied)");
            foreach (var v in unknownHelpers) ctx.Warn("block " + v + ": its properties come from a helper the import does not read (BlockBehaviour.Properties)");
            foreach (var v in noProperties) ctx.Warn("block " + v + ": no BlockBehaviour.Properties in its registration (Java defaults assumed)");
            foreach (var v in unparsedStrength) ctx.Warn("block property not parsed: " + v);
            return res;
        }

        // the helpers returning BlockBehaviour.Properties that ParseProps reads (GloamingBlocks.java)
        static readonly string[] KnownPropHelpers = { "logProperties", "copyAndScaleProperties" };
        const string Num = @"-?[\d.]+[FfDd]?(?:\s*[*/]\s*[\d.]+[FfDd]?)*";
        // ofFullCopy(X) / ofLegacyCopy(X) / copyAndScaleProperties(X, k), X an upstream constant (.get()) or Blocks.X
        static readonly Regex CopyCall = new Regex(@"(?:of(?:Full|Legacy)Copy\(\s*([\w.]+?)(?:\.get\(\))?\s*|copyAndScaleProperties\(\s*([\w.]+?)(?:\.get\(\))?\s*,\s*(" + Num + @")\s*)\)", RegexOptions.Compiled);

        /// <summary>A float expression of upstream's block properties: a literal, or a product / quotient of literals
        /// ("0.2F * 64F").</summary>
        public static bool TryFloat(string expr, out double v)
        {
            v = 0;
            expr = expr.Trim();
            var parts = Regex.Split(expr, @"\s*([*/])\s*");
            double acc = 0;
            for (int i = 0; i < parts.Length; i += 2)
            {
                double x;
                if (!double.TryParse(parts[i].TrimEnd('F', 'f', 'D', 'd'), NumberStyles.Float, CultureInfo.InvariantCulture, out x)) return false;
                if (i == 0) acc = x;
                else if (parts[i - 1] == "*") acc *= x;
                else acc /= x;
            }
            v = acc;
            return true;
        }

        // Minecraft 1.21.1 map colours of the vanilla wood sets: planks (= stripped wood), wood (bark)
        static readonly Dictionary<string, string[]> WoodColors = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            // planks, wood (bark) / hyphae, stripped wood / hyphae
            { "OAK", new[] { "WOOD", "WOOD", "WOOD" } }, { "SPRUCE", new[] { "PODZOL", "PODZOL", "PODZOL" } }, { "BIRCH", new[] { "SAND", "SAND", "SAND" } },
            { "JUNGLE", new[] { "DIRT", "DIRT", "DIRT" } }, { "ACACIA", new[] { "COLOR_ORANGE", "COLOR_GRAY", "COLOR_ORANGE" } },
            { "CHERRY", new[] { "TERRACOTTA_WHITE", "TERRACOTTA_GRAY", "TERRACOTTA_PINK" } }, { "DARK_OAK", new[] { "COLOR_BROWN", "COLOR_BROWN", "COLOR_BROWN" } },
            { "MANGROVE", new[] { "COLOR_RED", "COLOR_RED", "COLOR_RED" } }, { "BAMBOO", new[] { "COLOR_YELLOW", "COLOR_YELLOW", "COLOR_YELLOW" } },
            { "CRIMSON", new[] { "CRIMSON_STEM", "CRIMSON_HYPHAE", "CRIMSON_HYPHAE" } }, { "WARPED", new[] { "WARPED_STEM", "WARPED_HYPHAE", "WARPED_HYPHAE" } },
        };

        static void VanillaProps(JBlock b, string v)
        {
            // the vanilla blocks upstream copies properties from (Minecraft 1.21.1 Blocks.java values); flammability is
            // not a block property in Java (FireBlock.setFlammable, see Blocks())
            b.Hardness = 0; b.Resistance = 0; b.Sound = "STONE"; b.MapColor = null; b.Friction = -1; b.Light = 0;
            b.MapColorSide = null; b.MapColorProp = null; b.MapColorIf = null;
            b.LightExpr = null; b.DynamicLight = false;
            b.NoCollision = false; b.NoOcclusion = false; b.Unbreakable = false; b.RequiresTool = false;
            string wood = WoodColors.Keys.Where(k => v.StartsWith(k + "_", StringComparison.Ordinal) || v.StartsWith("STRIPPED_" + k + "_", StringComparison.Ordinal))
                .OrderByDescending(k => k.Length).FirstOrDefault();
            bool nether = wood == "CRIMSON" || wood == "WARPED";
            if (v == "SPAWNER") { b.Hardness = 5; b.Resistance = 5; b.Sound = "METAL"; b.MapColor = "STONE"; b.NoOcclusion = true; b.RequiresTool = true; }
            else if (v == "FLOWER_POT") { b.NoOcclusion = true; }   // instabreak(): strength 0
            else if (v.EndsWith("_HEAD") || v.EndsWith("_SKULL")) { b.Hardness = 1; b.Resistance = 1; }
            else if (v.EndsWith("_CARPET") && v != "MOSS_CARPET") { b.Hardness = 0.1; b.Resistance = 0.1; b.Sound = "WOOL"; b.MapColor = DyeMapColor(v.Substring(0, v.Length - 7)); }
            else if (wood != null && (v == wood + "_PLANKS" || v == wood + "_SLAB"))
            {
                // planks and their slab: strength(2.0F, 3.0F), the planks' map colour and wood sound
                b.Hardness = 2; b.Resistance = 3; b.MapColor = WoodColors[wood][0];
                b.Sound = nether ? "NETHER_WOOD" : wood == "CHERRY" ? "CHERRY_WOOD" : wood == "BAMBOO" ? "BAMBOO_WOOD" : "WOOD";
            }
            else if (wood != null && (v.EndsWith("_WOOD") || v.EndsWith("_LOG") || v.EndsWith("_HYPHAE") || v.EndsWith("_STEM")))
            {
                // logs / wood / hyphae: strength 2.0 (upstream copies only wood and hyphae, logs use logProperties)
                bool stripped = v.StartsWith("STRIPPED_", StringComparison.Ordinal);
                bool bark = v.EndsWith("_WOOD") || v.EndsWith("_HYPHAE");
                b.Hardness = 2; b.Resistance = 2;
                b.MapColor = !bark ? WoodColors[wood][0] : WoodColors[wood][stripped ? 2 : 1];
                b.Sound = nether ? "STEM" : wood == "CHERRY" ? "CHERRY_WOOD" : wood == "BAMBOO" ? "BAMBOO_WOOD" : "WOOD";
            }
            else if (v == "OAK_SIGN" || v == "OAK_WALL_SIGN" || v.EndsWith("HANGING_SIGN")) { b.Hardness = 1; b.Resistance = 1; b.Sound = "WOOD"; b.NoCollision = true; b.MapColor = "WOOD"; }
            else if (v == "CHEST" || v == "TRAPPED_CHEST") { b.Hardness = 2.5; b.Resistance = 2.5; b.Sound = "WOOD"; b.MapColor = "WOOD"; }
            else if (v == "BOOKSHELF") { b.Hardness = 1.5; b.Resistance = 1.5; b.Sound = "WOOD"; b.MapColor = "WOOD"; }
            else if (v == "LADDER") { b.Hardness = 0.4; b.Resistance = 0.4; b.Sound = "LADDER"; b.NoOcclusion = true; }
            else if (v == "VINE") { b.Hardness = 0.2; b.Resistance = 0.2; b.Sound = "VINE"; b.NoCollision = true; b.MapColor = "PLANT"; }
            else if (v == "COBBLESTONE" || v == "STONE_BRICKS") { b.Hardness = 2; b.Resistance = 6; b.MapColor = "STONE"; b.RequiresTool = true; }
            else if (v == "STONE") { b.Hardness = 1.5; b.Resistance = 6; b.MapColor = "STONE"; b.RequiresTool = true; }
            else if (v == "OBSIDIAN") { b.Hardness = 50; b.Resistance = 1200; b.MapColor = "COLOR_BLACK"; b.RequiresTool = true; }
            else if (v == "GLASS") { b.Hardness = 0.3; b.Resistance = 0.3; b.Sound = "GLASS"; b.NoOcclusion = true; }
            else if (v == "SLIME_BLOCK") { b.Friction = 0.8; b.Sound = "SLIME_BLOCK"; b.NoOcclusion = true; b.MapColor = "GRASS"; }
            else if (v == "OAK_LEAVES" || v == "SPRUCE_LEAVES" || v == "BIRCH_LEAVES" || v == "JUNGLE_LEAVES" || v == "ACACIA_LEAVES" || v == "DARK_OAK_LEAVES" || v == "MANGROVE_LEAVES")
            { b.Hardness = 0.2; b.Resistance = 0.2; b.Sound = "GRASS"; b.NoOcclusion = true; b.MapColor = "PLANT"; }
            else if (v == "TORCH" || v == "WALL_TORCH") { b.Light = 14; b.Sound = "WOOD"; b.NoCollision = true; }   // instabreak()
            else if (v == "MOSS_CARPET") { b.Hardness = 0.1; b.Resistance = 0.1; b.Sound = "MOSS_CARPET"; b.MapColor = "COLOR_GREEN"; }
            else if (v == "AIR") { b.NoCollision = true; }
            else unknownVanilla.Add(v);
        }

        /// <summary>A Java MapColor expression as a MapColor constant name (null: MapColor.NONE).</summary>
        static string ColorName(string expr)
        {
            var m = Regex.Match(expr, @"^DyeColor\.(\w+)");
            if (m.Success) return DyeMapColor(m.Groups[1].Value);
            string c = expr.Substring(expr.IndexOf('.') + 1);
            return c == "NONE" ? null : c;
        }

        /// <summary>DyeColor.getMapColor() (Minecraft 1.21.1): COLOR_X, except white (SNOW) and lime (COLOR_LIGHT_GREEN).</summary>
        static string DyeMapColor(string dye)
        {
            return dye == "WHITE" ? "SNOW" : dye == "LIME" ? "COLOR_LIGHT_GREEN" : "COLOR_" + dye;
        }

        static readonly SortedSet<string> unknownVanilla = new SortedSet<string>(StringComparer.Ordinal);
        static readonly SortedSet<string> unparsedStrength = new SortedSet<string>(StringComparer.Ordinal);
        static readonly SortedSet<string> unknownCopies = new SortedSet<string>(StringComparer.Ordinal);
        static readonly SortedSet<string> unknownHelpers = new SortedSet<string>(StringComparer.Ordinal);
        static readonly SortedSet<string> noProperties = new SortedSet<string>(StringComparer.Ordinal);

        static void ParseProps(JBlock b, string s, bool resetCopy = true)
        {
            Match m;
            if (resetCopy)
            {
                m = CopyCall.Match(s);
                if (m.Success)
                {
                    string src = m.Groups[1].Success ? m.Groups[1].Value : m.Groups[2].Value;
                    b.CopyOf = src.StartsWith("GloamingBlocks.", StringComparison.Ordinal) ? src.Substring(9) : src;
                    double k;
                    b.CopyScale = m.Groups[3].Success && TryFloat(m.Groups[3].Value, out k) ? k : 1;
                }
            }
            // strength(h[, r]) / destroyTime(h) / explosionResistance(r) / instabreak(), in call order, as Java's
            // BlockBehaviour.Properties does: strength(f) = strength(f, f) = destroyTime(f).explosionResistance(f),
            // explosionResistance(r) keeps max(0, r), instabreak() = strength(0); arguments are literals or
            // products of literals ("0.2F * 64.0F")
            foreach (Match sm in Regex.Matches(s, @"\.(strength|destroyTime|explosionResistance)\(([^()]*(?:\([^()]*\))?[^()]*)\)|\.instabreak\(\)"))
            {
                if (!sm.Groups[1].Success) { b.Hardness = 0; b.Resistance = 0; continue; }
                var args = sm.Groups[2].Value.Split(',');
                var vals = new List<double>();
                foreach (var a in args) { double x; if (TryFloat(a, out x)) vals.Add(x); }
                string fn = sm.Groups[1].Value;
                if (vals.Count != args.Length || (fn == "strength" ? args.Length > 2 : args.Length != 1)) { unparsedStrength.Add(b.Id + ": " + sm.Value); continue; }
                if (fn == "explosionResistance") b.Resistance = Math.Max(0, vals[0]);
                else
                {
                    b.Hardness = vals[0];
                    if (fn == "strength") b.Resistance = Math.Max(0, vals.Count > 1 ? vals[1] : vals[0]);
                }
            }
            b.Unbreakable = b.Hardness < 0;
            m = Regex.Match(s, @"\.lightLevel\(\s*\(?\s*\w+\s*\)?\s*->\s*(\d+)\s*\)");
            if (m.Success) { b.Light = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture); b.DynamicLight = false; b.LightExpr = null; }
            else if (s.Contains(".lightLevel(")) { b.DynamicLight = true; b.Light = 0; b.LightExpr = Light.LambdaOf(s); }
            // a vanilla SoundType by its constant ("WOOD"), one of upstream's own as "GloamingSoundTypes.JAR"
            m = Regex.Match(s, @"\.sound\(\s*(SoundType|GloamingSoundTypes)\.(\w+)\s*\)");
            if (m.Success) b.Sound = (m.Groups[1].Value == "GloamingSoundTypes" ? "GloamingSoundTypes." : "") + m.Groups[2].Value;
            // map colour: MapColor.X, DyeColor.X (its map colour), logProperties(top[, side]) (side: a lying log's bark), or
            // a lambda of one boolean state (the castle doors: VANISHED ? MapColor.NONE : the door's colour)
            const string color = @"MapColor\.\w+|DyeColor\.\w+(?:\.getMapColor\(\))?";
            m = Regex.Match(s, @"logProperties\(\s*MapColor\.(\w+)\s*(?:,\s*MapColor\.(\w+)\s*)?\)");
            if (m.Success) { b.MapColor = m.Groups[1].Value; b.MapColorSide = m.Groups[2].Success ? m.Groups[2].Value : null; b.MapColorProp = null; }
            m = Regex.Match(s, @"\.mapColor\(\s*(" + color + @")\s*\)");
            if (m.Success) { b.MapColor = ColorName(m.Groups[1].Value); b.MapColorSide = null; b.MapColorProp = null; }
            m = Regex.Match(s, @"\.mapColor\(\s*\(?\s*(\w+)\s*\)?\s*->\s*\1\.getValue\(\s*([\w.]+)\s*\)\s*\?\s*(" + color + @")\s*:\s*(" + color + @")\s*\)");
            if (m.Success) { b.MapColorProp = m.Groups[2].Value; b.MapColorIf = ColorName(m.Groups[3].Value); b.MapColor = ColorName(m.Groups[4].Value); b.MapColorSide = null; }
            else if (Regex.IsMatch(s, @"\.mapColor\(\s*\(?\s*\w+\s*\)?\s*->")) { b.MapColor = null; unparsedStrength.Add(b.Id + ": map colour lambda"); }
            if (s.Contains(".noCollission()")) b.NoCollision = true;
            if (s.Contains(".noOcclusion()")) b.NoOcclusion = true;
            if (s.Contains(".ignitedByLava()")) b.Flammable = true;
            if (s.Contains(".replaceable()")) b.Replaceable = true;
            if (Regex.IsMatch(s, @"\.requiresCorrectToolForDrops\(\s*\)")) b.RequiresTool = true;
            m = Regex.Match(s, @"\.friction\(\s*([\d.]+F?)\s*\)");
            if (m.Success) b.Friction = F(m.Groups[1].Value);
        }

        /// <summary>A tool tier: its uses, mining speed and the vanilla tier whose blocks it drops (Tier.getIncorrectBlocksForDrops:
        /// wooden, stone, iron, diamond, gold or netherite).</summary>
        public sealed class TierInfo
        {
            public int Uses;
            public double Speed;
            public string Drops;
        }

        // Minecraft 1.21.1 Tiers: uses, speed, incorrect_for_<x>_tool
        static readonly Dictionary<string, TierInfo> VanillaTiers = new Dictionary<string, TierInfo>(StringComparer.Ordinal)
        {
            { "WOOD", new TierInfo { Uses = 59, Speed = 2, Drops = "wooden" } }, { "STONE", new TierInfo { Uses = 131, Speed = 4, Drops = "stone" } },
            { "IRON", new TierInfo { Uses = 250, Speed = 6, Drops = "iron" } }, { "DIAMOND", new TierInfo { Uses = 1561, Speed = 8, Drops = "diamond" } },
            { "GOLD", new TierInfo { Uses = 32, Speed = 12, Drops = "gold" } }, { "NETHERITE", new TierInfo { Uses = 2031, Speed = 9, Drops = "netherite" } },
        };
        // the tool kinds of the vanilla tool classes (Minecraft 1.21.1: DiggerItem's mineable tag, SwordItem's own rules)
        static readonly string[][] ToolKinds =
        {
            new[] { "PickaxeItem", "pickaxe" }, new[] { "AxeItem", "axe" }, new[] { "ShovelItem", "shovel" }, new[] { "HoeItem", "hoe" }, new[] { "SwordItem", "sword" },
        };
        static readonly HashSet<string> TieredBases = new HashSet<string>(StringComparer.Ordinal)
        {
            "TieredItem", "SwordItem", "DiggerItem", "PickaxeItem", "AxeItem", "ShovelItem", "HoeItem",
        };
        // vanilla item classes whose instances never stack (damageable or single by design)
        static readonly HashSet<string> SingleStackBases = new HashSet<string>(StringComparer.Ordinal)
        {
            "TieredItem", "SwordItem", "DiggerItem", "PickaxeItem", "AxeItem", "ShovelItem", "HoeItem", "ArmorItem", "BowItem",
            "CrossbowItem", "ShieldItem", "TridentItem", "FishingRodItem", "ElytraItem", "ShearsItem", "FlintAndSteelItem", "MaceItem",
        };
        static readonly Dictionary<string, string> itemChains = new Dictionary<string, string>(StringComparer.Ordinal);
        static Dictionary<string, TierInfo> gloamingTiers;

        /// <summary>Forgets every per-run cache (the assembly stays loaded in a PowerShell session between runs).</summary>
        public static void Reset()
        {
            itemChains.Clear();
            gloamingTiers = null;
            classFiles = null;
            allJavaFiles = null;
            sourceTexts.Clear();
            unknownVanilla.Clear();
            unparsedStrength.Clear();
            unknownCopies.Clear();
            unknownHelpers.Clear();
            noProperties.Clear();
            JavaStates.Reset();
        }

        /// <summary>The comment-stripped text of an upstream class file (cached), or null for a vanilla class.</summary>
        public static string ClassText(Ctx ctx, string cls)
        {
            return JavaStates.Text(ctx, cls);
        }

        public static Dictionary<string, JItem> Items(Ctx ctx)
        {
            // GloamingToolMaterials: new SimpleTier(incorrectBlocks, uses, speed, damage, enchantability, repair); the incorrect-blocks
            // tag (BlockTagGenerator.INCORRECT_FOR_X_TOOL = gloamingforest:incorrect_for_x_tool) includes the vanilla
            // #minecraft:incorrect_for_<tier>_tool whose blocks the tier does not drop
            gloamingTiers = new Dictionary<string, TierInfo>(StringComparer.Ordinal);
            string tiers = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "util", "GloamingToolMaterials.java")));
            foreach (Match tm in Regex.Matches(tiers, @"Tier\s+(\w+)\s*=\s*new\s+SimpleTier\(\s*(?:\w+\.)?(\w+)\s*,\s*(\d+)\s*,\s*([\d.]+)[Ff]?\s*,"))
            {
                var ti = new TierInfo { Uses = int.Parse(tm.Groups[3].Value, CultureInfo.InvariantCulture), Speed = F(tm.Groups[4].Value) };
                var tag = ctx.TagFile("block", "gloamingforest:" + tm.Groups[2].Value.ToLowerInvariant());
                var vref = tag != null && tag["values"] != null ? tag["values"].A.Select(v => v.IsStr ? v.AsString() : null)
                    .Select(v => v == null ? null : Regex.Match(v, @"^#minecraft:incorrect_for_(\w+)_tool$")).FirstOrDefault(x => x != null && x.Success) : null;
                if (vref != null) ti.Drops = vref.Groups[1].Value;
                else ctx.Warn("GloamingToolMaterials." + tm.Groups[1].Value + ": " + tm.Groups[2].Value + " does not include a vanilla incorrect_for_<tier>_tool tag");
                gloamingTiers[tm.Groups[1].Value] = ti;
            }
            if (gloamingTiers.Count == 0) ctx.Warn("GloamingToolMaterials: no tier parsed");
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingItems.java")));
            var res = new Dictionary<string, JItem>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredItem<"))
            {
                var idm = Regex.Match(f.Item3, @"^\s*[\w.]+\(\s*""([a-z0-9_]+)""");
                if (!idm.Success) continue;
                var it = new JItem { Id = idm.Groups[1].Value, Const = f.Item2, Stmt = f.Item3 };
                var cm = Regex.Match(f.Item3, @"new\s+(\w+)\s*\(");
                it.Cls = cm.Success ? cm.Groups[1].Value : "Item";
                var m = Regex.Match(f.Item3, @"\.stacksTo\(\s*(\d+)\s*\)");
                if (m.Success) it.StackSize = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                m = Regex.Match(f.Item3, @"\.durability\(\s*(\d+)\s*\)");
                if (m.Success) it.Durability = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                m = Regex.Match(f.Item3, @"getDurability\(\s*(\d+)\s*\)");
                if (m.Success)
                {
                    // ArmorItem.Type.X.getDurability(n): helmet 11, chestplate 16, leggings 15, boots 13 (x n)
                    var tm = Regex.Match(f.Item3, @"ArmorItem\.Type\.(\w+)\.getDurability");
                    int mult = 16;
                    if (tm.Success) mult = tm.Groups[1].Value == "HELMET" ? 11 : tm.Groups[1].Value == "CHESTPLATE" ? 16 : tm.Groups[1].Value == "LEGGINGS" ? 15 : 13;
                    it.Durability = mult * int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                }
                // tools: a TieredItem's durability is its tier's uses (TieredItem: properties.durability(tier.getUses())),
                // whatever the upstream subclass is called (KnightmetalPickItem, MinotaurAxeItem, CustomDamageSwordItem, ...)
                var chain = ClassChain(ctx, it.Cls, itemChains);
                if (chain.Any(c => TieredBases.Contains(c)))
                {
                    var tm = Regex.Match(f.Item3, @"\b(GloamingToolMaterials|Tiers)\.([A-Z_]+)\b");
                    TierInfo ti;
                    if (tm.Success && (tm.Groups[1].Value == "Tiers" ? VanillaTiers.TryGetValue(tm.Groups[2].Value, out ti) : gloamingTiers.TryGetValue(tm.Groups[2].Value, out ti)))
                    {
                        it.Durability = ti.Uses;
                        it.Tier = ti;
                        it.TierName = tm.Groups[1].Value + "." + tm.Groups[2].Value;
                    }
                    else ctx.Warn("item " + it.Id + ": tiered item without a known tier (" + string.Join(" < ", chain) + ")");
                    // the tool kind: the vanilla class reached (DiggerItem's mineable tag; SwordItem's cobweb / sword_efficient rules)
                    var kind = ToolKinds.FirstOrDefault(k => chain.Contains(k[0]));
                    if (kind != null) it.ToolKind = kind[1];
                }
                // armor whose class sets the durability: TravellersArmorItem(type, properties, slots, durability) does
                // properties.durability(type.getDurability(durability)); the call or a subclass's super(...) gives the values
                if (it.Durability == 0 && chain.Any(c => { var ct = ClassText(ctx, c); return ct != null && Regex.IsMatch(ct, @"\.durability\(\s*\w+\.getDurability\(\s*durability\s*\)\s*\)"); }))
                {
                    Match am = null;
                    var nm = Regex.Match(f.Item3, @"new\s+" + Regex.Escape(it.Cls) + @"\s*\(");
                    if (nm.Success)
                    {
                        var a = SplitArgs(f.Item3.Substring(nm.Index + nm.Length));
                        if (a.Count == 4) am = Regex.Match(a[0].Trim() + "|" + a[3].Trim(), @"^(?:ArmorItem\.)?Type\.(\w+)\|(\d+)$");
                    }
                    if (am == null || !am.Success)
                    {
                        var ct = ClassText(ctx, it.Cls);
                        if (ct != null) am = Regex.Match(ct, @"super\(\s*(?:ArmorItem\.)?Type\.(\w+)\s*,\s*\w+\s*,\s*\d+\s*,\s*(\d+)\s*\)");
                    }
                    if (am != null && am.Success)
                    {
                        string t = am.Groups[1].Value;
                        int mult = t == "HELMET" ? 11 : t == "CHESTPLATE" || t == "BODY" ? 16 : t == "LEGGINGS" ? 15 : 13;
                        it.Durability = mult * int.Parse(am.Groups[2].Value, CultureInfo.InvariantCulture);
                    }
                }
                // a damageable item (durability) stacks to 1 (Item.Properties.durability sets stacksTo(1)), as do the
                // vanilla single-item bases
                if (it.Durability > 0) it.StackSize = 1;
                if (chain.Any(c => SingleStackBases.Contains(c)))
                {
                    if (it.Durability == 0 && it.StackSize != 1) ctx.Warn("item " + it.Id + ": " + string.Join(" < ", chain) + " without a parsed durability or stacksTo(1): stack size 1 assumed");
                    it.StackSize = 1;
                }
                it.Chain = chain;
                if (f.Item3.Contains(".fireResistant()")) it.FireResistant = true;
                m = Regex.Match(f.Item3, @"\.rarity\(\s*(?:Rarity\.|GloamingRarityEnumExtension\.\w*\.|gloamingRarityEnumExtension\.)?(\w+)");
                if (m.Success) it.Rarity = m.Groups[1].Value;
                if (f.Item3.Contains(".food(")) it.Food = true;
                res[it.Id] = it;
            }
            return res;
        }

        public static Dictionary<string, JEntity> Entities(Ctx ctx)
        {
            var names = new Dictionary<string, string>();
            foreach (Match m in Regex.Matches(Ctx.ReadText(Path.Combine(ctx.JavaDir, "entity", "GloamingEntityNames.java")), @"ResourceLocation (\w+) = prefix\(""([a-z0-9_]+)""\)"))
                names[m.Groups[1].Value] = m.Groups[2].Value;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingEntities.java")));
            var res = new Dictionary<string, JEntity>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredHolder<EntityType"))
            {
                var m = Regex.Match(f.Item3, @"^\s*(make|buildNoEgg|build)\(\s*GloamingEntityNames\.(\w+)\s*,\s*(.*)$", RegexOptions.Singleline);
                if (!m.Success) continue;
                string id;
                if (!names.TryGetValue(m.Groups[2].Value, out id)) continue;
                var e = new JEntity { Id = id };
                string rest = m.Groups[3].Value;
                var cls = Regex.Match(rest, @"(\w+)::new");
                if (cls.Success) e.Cls = cls.Groups[1].Value;
                var cat = Regex.Match(rest, @"MobCategory\.(\w+)");
                if (cat.Success) e.Category = cat.Groups[1].Value;
                if (m.Groups[1].Value == "make")
                {
                    // make(id, factory, category, width, height, [eyeHeight,] [fireproof,] primary, secondary [, ridingOffset])
                    var args = SplitArgs(rest);
                    var nums = new List<string>();
                    for (int i = 2; i < args.Count; i++) nums.Add(args[i].Trim());
                    e.Width = F(nums[0]); e.Height = F(nums[1]);
                    int k = 2;
                    if (k < nums.Count && nums[k].EndsWith("F") && k + 1 < nums.Count && (nums[k + 1] == "true" || nums[k + 1] == "false")) k++;
                    if (k < nums.Count && (nums[k] == "true" || nums[k] == "false")) { e.FireImmune = nums[k] == "true"; k++; }
                    e.EggPrimary = ParseInt(nums[k]); e.EggSecondary = ParseInt(nums[k + 1]);
                    e.HasEgg = true;
                }
                else
                {
                    var sz = Regex.Match(rest, @"(?:makeCastedBuilder\([^,]+,\s*[\w:]+,\s*|makeBuilder\([\w:]+,\s*MobCategory\.\w+,\s*|\.sized\()\s*([\d.]+F?)\s*,\s*([\d.]+F?)");
                    if (sz.Success) { e.Width = F(sz.Groups[1].Value); e.Height = F(sz.Groups[2].Value); }
                    var fp = Regex.Match(rest, @",\s*(true|false)\s*(?:,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+))?\s*\)\s*$");
                    if (fp.Success)
                    {
                        e.FireImmune = fp.Groups[1].Value == "true";
                        if (m.Groups[1].Value == "build" && fp.Groups[2].Success) { e.HasEgg = true; e.EggPrimary = ParseInt(fp.Groups[2].Value); e.EggSecondary = ParseInt(fp.Groups[3].Value); }
                    }
                }
                res[e.Id] = e;
            }
            return res;
        }

        static int ParseInt(string s)
        {
            s = s.Trim();
            if (s.StartsWith("0x")) return int.Parse(s.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return int.Parse(s, CultureInfo.InvariantCulture);
        }

        /// <summary>Top-level comma-separated arguments of "a, b(c, d), e)" (stops at the closing parenthesis).</summary>
        public static List<string> SplitArgs(string s)
        {
            var res = new List<string>();
            int depth = 0, start = 0;
            for (int i = 0; i < s.Length; i++)
            {
                char c = s[i];
                if (c == '(' || c == '<' || c == '{') depth++;
                else if (c == ')' || c == '>' || c == '}')
                {
                    if (depth == 0) { res.Add(s.Substring(start, i - start)); return res; }
                    depth--;
                }
                else if (c == ',' && depth == 0) { res.Add(s.Substring(start, i - start)); start = i + 1; }
            }
            res.Add(s.Substring(start));
            return res;
        }

        public sealed class Tab
        {
            public string Id, TitleKey, IconConst, Icon;
            public List<string> Items = new List<string>();
        }

        /// <summary>GloamingCreativeTabs: the 4 tabs, their icon and their item ids in order.</summary>
        public static List<Tab> CreativeTabs(Ctx ctx, Dictionary<string, JBlock> blocks, Dictionary<string, JItem> items, IEnumerable<string> spawnEggs)
        {
            var constId = new Dictionary<string, string>();
            foreach (var b in blocks.Values) constId["GloamingBlocks." + b.Const] = b.Id;
            foreach (var i in items.Values) constId["GloamingItems." + i.Const] = i.Id;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "GloamingCreativeTabs.java")));
            var tabs = new List<Tab>();
            var re = new Regex(@"TABS\.register\(""(\w+)""");
            var starts = re.Matches(text).Cast<Match>().ToList();
            for (int t = 0; t < starts.Count; t++)
            {
                int from = starts[t].Index, to = t + 1 < starts.Count ? starts[t + 1].Index : text.IndexOf("private static void", from, StringComparison.Ordinal);
                if (to < 0) to = text.Length;
                string body = text.Substring(from, to - from);
                var tab = new Tab { Id = "gloamingforest:" + starts[t].Groups[1].Value };
                var tk = Regex.Match(body, @"translatable\(""([^""]+)""\)");
                if (tk.Success) tab.TitleKey = tk.Groups[1].Value;
                var ic = Regex.Match(body, @"\.icon\(\(\)\s*->\s*new ItemStack\((upstream\w+\.\w+)");
                if (ic.Success) { tab.IconConst = ic.Groups[1].Value; string icon; if (constId.TryGetValue(tab.IconConst, out icon)) tab.Icon = "gloamingforest:" + icon; }
                var seen = new HashSet<string>();
                foreach (Match m in Regex.Matches(body, @"output\.accept\((upstream(?:Blocks|Items)\.\w+)|(generateGearWithEnchants|createDefaultSkullCandle)\(output,\s*(upstream(?:Blocks|Items)\.\w+)|(createSpawnEggsAlphabetical|createCaskets|createGlassSwordAndLoreVer)\(output"))
                {
                    var add = new List<string>();
                    if (m.Groups[1].Success || m.Groups[3].Success)
                    {
                        string c = m.Groups[1].Success ? m.Groups[1].Value : m.Groups[3].Value;
                        string id;
                        if (constId.TryGetValue(c, out id)) add.Add(id);
                        else ctx.Warn("creative tab " + tab.Id + ": unknown constant " + c);
                    }
                    else if (m.Groups[4].Value == "createSpawnEggsAlphabetical") add.AddRange(spawnEggs.OrderBy(x => x, StringComparer.Ordinal));
                    else if (m.Groups[4].Value == "createCaskets") add.Add("keepsake_casket");
                    else if (m.Groups[4].Value == "createGlassSwordAndLoreVer") add.Add("glass_sword");
                    foreach (var a in add) if (seen.Add(a)) tab.Items.Add("gloamingforest:" + a);
                }
                if (tab.Icon == null && tab.Items.Count > 0) tab.Icon = tab.Items[0];
                tabs.Add(tab);
            }
            return tabs;
        }

        /// <summary>The superclass chain of an upstream block class (upstream classes from block/*.java, then the vanilla base).</summary>
        static Dictionary<string, string> classFiles;

        static List<string> allJavaFiles;
        static readonly Dictionary<string, string> sourceTexts = new Dictionary<string, string>(StringComparer.Ordinal);

        /// <summary>Every upstream Java source file, in ordinal order (listed once per run, with the class-file index).</summary>
        public static List<string> AllClassFiles(Ctx ctx)
        {
            ClassFile(ctx, "");
            return allJavaFiles;
        }

        /// <summary>The text of an upstream Java source file, read once per run (line endings normalised).</summary>
        public static string SourceText(string file)
        {
            string t;
            if (!sourceTexts.TryGetValue(file, out t)) sourceTexts[file] = t = Ctx.ReadText(file);
            return t;
        }

        /// <summary>The source file of an upstream class by simple name (null for vanilla classes).</summary>
        public static string ClassFile(Ctx ctx, string cls)
        {
            if (classFiles == null)
            {
                classFiles = new Dictionary<string, string>(StringComparer.Ordinal);
                allJavaFiles = Directory.GetFiles(ctx.JavaDir, "*.java", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal).ToList();
                foreach (var f in allJavaFiles)
                {
                    var n = Path.GetFileNameWithoutExtension(f);
                    if (!classFiles.ContainsKey(n)) classFiles[n] = f;
                }
            }
            string file;
            return classFiles.TryGetValue(cls, out file) ? file : null;
        }

        public static List<string> ClassChain(Ctx ctx, string cls, Dictionary<string, string> extendsCache)
        {
            ClassFile(ctx, cls);
            var chain = new List<string>();
            string c = cls;
            for (int guard = 0; c != null && guard < 12; guard++)
            {
                chain.Add(c);
                string sup;
                if (!extendsCache.TryGetValue(c, out sup))
                {
                    sup = null;
                    string file;
                    if (classFiles.TryGetValue(c, out file))
                    {
                        var m = Regex.Match(SourceText(file), @"class\s+" + c + @"\b[^{]*?\bextends\s+([\w.]+)");
                        if (m.Success) sup = m.Groups[1].Value.Split('.').Last();
                    }
                    extendsCache[c] = sup;
                }
                c = sup;
            }
            return chain;
        }
    }
}
