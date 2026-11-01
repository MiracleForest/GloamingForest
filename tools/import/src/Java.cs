// GloamingForest import tool (contract "import", unit F1): reads TF's registration code
// (init/TFBlocks.java, TFItems.java, TFEntities.java, TFCreativeTabs.java) for the facts the data
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
        public double Hardness = 1.0, Resistance = -1;  // -1 resistance: = hardness
        public int Light = 0;
        public bool DynamicLight, NoCollision, NoOcclusion, Instabreak, Unbreakable, Flammable, Replaceable;
        public string Sound = "STONE", MapColor;
        public double Friction = -1;
        public int FireEncouragement = -1, FireFlammability = -1;   // FireBlock.setFlammable (RegistrationEvents)
        public string LightExpr;   // the lightLevel lambda body when it is not a constant
        public string CopyOf;      // TF constant or "Blocks.X"
    }

    public sealed class JItem
    {
        public string Id, Const, Cls, Stmt;
        public int StackSize = 64, Durability = 0;
        public List<string> Chain = new List<string>();
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

        public static Dictionary<string, JBlock> Blocks(Ctx ctx)
        {
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFBlocks.java")));
            var props = new Dictionary<string, string>();
            foreach (var f in Fields(text, @"BlockBehaviour\.Properties")) props[f.Item2] = f.Item3;
            var helpers = new Dictionary<string, string>();
            foreach (Match m in Regex.Matches(text, @"public static DeferredBlock<(\w+)> (\w+)\(String name[^)]*\)\s*\{(.*?)\n\t\}", RegexOptions.Singleline))
                helpers[m.Groups[2].Value] = m.Groups[1].Value + "|" + m.Groups[3].Value;
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
                    body = hp[1] + body;
                }
                var cm = Regex.Match(body, @"new\s+(\w+)\s*[(<]");
                if (b.Cls == null) b.Cls = cm.Success ? cm.Groups[1].Value : Regex.Match(f.Item1, @"DeferredBlock<(\w+)").Groups[1].Value;
                // inline named property sets (TWILIGHT_OAK_LOG_PROPS)
                foreach (Match pm in Regex.Matches(body, @"\b([A-Z][A-Z0-9_]*_PROPS)\b"))
                {
                    string p;
                    if (props.TryGetValue(pm.Groups[1].Value, out p)) body = body + " " + p;
                }
                ParseProps(b, body);
                res[b.Id] = b;
                byConst[b.Const] = b;
            }
            // property copies: ofFullCopy(OTHER.get()) / ofFullCopy(OTHER) / Blocks.X; a copied block is
            // resolved before the blocks that copy it (chains: A copies B copies C)
            var resolved = new HashSet<JBlock>();
            Action<JBlock> resolve = null;
            resolve = b =>
            {
                if (b.CopyOf == null || !resolved.Add(b)) return;
                JBlock src;
                int at = b.Stmt.IndexOf("Copy(", StringComparison.Ordinal);
                string after = at >= 0 ? b.Stmt.Substring(at) : "";
                if (byConst.TryGetValue(b.CopyOf, out src) && src != b)
                {
                    resolve(src);
                    b.Hardness = src.Hardness; b.Resistance = src.Resistance; b.Sound = src.Sound; b.MapColor = src.MapColor;
                    b.Light = src.Light; b.Flammable = src.Flammable; b.Unbreakable = src.Unbreakable; b.Instabreak = src.Instabreak;
                    b.LightExpr = src.LightExpr; b.DynamicLight = src.DynamicLight; b.Friction = src.Friction;
                    b.NoCollision = src.NoCollision; b.NoOcclusion = src.NoOcclusion;
                    ParseProps(b, after, false); // explicit settings after the copy win
                }
                else if (b.CopyOf.StartsWith("Blocks.", StringComparison.Ordinal))
                {
                    VanillaProps(b, b.CopyOf.Substring(7));
                    ParseProps(b, after, false);
                }
            };
            foreach (var b in res.Values) resolve(b);
            // flammability: only the blocks TF registers with FireBlock.setFlammable(block, encouragement,
            // flammability) in events/RegistrationEvents.java burn (ignitedByLava alone does not make a block
            // burn in Java)
            string reg = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "events", "RegistrationEvents.java")));
            foreach (Match fm in Regex.Matches(reg, @"\.setFlammable\(\s*TFBlocks\.(\w+)(?:\.get\(\))?\s*,\s*(\d+)\s*,\s*(\d+)\s*\)"))
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
            foreach (var v in unknownVanilla) ctx.Warn("TF copies the properties of Blocks." + v + ", which Java.VanillaProps does not know");
            foreach (var v in unparsedStrength) ctx.Warn("strength not parsed: " + v);
            return res;
        }

        /// <summary>A float expression of TF's block properties: a literal, or a product / quotient of literals
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
            // the vanilla blocks TF copies properties from (Minecraft 1.21.1 Blocks.java values); flammability is
            // not a block property in Java (FireBlock.setFlammable, see Blocks())
            b.Hardness = 0; b.Resistance = 0; b.Sound = "STONE"; b.MapColor = null; b.Friction = -1; b.Light = 0;
            b.LightExpr = null; b.DynamicLight = false;
            b.NoCollision = false; b.NoOcclusion = false; b.Instabreak = false; b.Unbreakable = false;
            string wood = WoodColors.Keys.Where(k => v.StartsWith(k + "_", StringComparison.Ordinal) || v.StartsWith("STRIPPED_" + k + "_", StringComparison.Ordinal))
                .OrderByDescending(k => k.Length).FirstOrDefault();
            bool nether = wood == "CRIMSON" || wood == "WARPED";
            if (v == "SPAWNER") { b.Hardness = 5; b.Resistance = 5; b.Sound = "METAL"; b.MapColor = "STONE"; b.NoOcclusion = true; }
            else if (v == "FLOWER_POT") { b.Instabreak = true; b.NoOcclusion = true; }
            else if (v.EndsWith("_HEAD") || v.EndsWith("_SKULL")) { b.Hardness = 1; b.Resistance = 1; }
            else if (v.EndsWith("_CARPET") && v != "MOSS_CARPET") { b.Hardness = 0.1; b.Resistance = 0.1; b.Sound = "WOOL"; b.MapColor = "COLOR_" + v.Substring(0, v.Length - 7); }
            else if (wood != null && v.EndsWith("_PLANKS"))
            {
                b.Hardness = 2; b.Resistance = 3; b.MapColor = WoodColors[wood][0];
                b.Sound = nether ? "NETHER_WOOD" : wood == "CHERRY" ? "CHERRY_WOOD" : wood == "BAMBOO" ? "BAMBOO_WOOD" : "WOOD";
            }
            else if (wood != null && (v.EndsWith("_WOOD") || v.EndsWith("_LOG") || v.EndsWith("_HYPHAE") || v.EndsWith("_STEM")))
            {
                // logs / wood / hyphae: strength 2.0 (TF copies only wood and hyphae, logs use logProperties)
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
            else if (v == "COBBLESTONE" || v == "STONE_BRICKS") { b.Hardness = 2; b.Resistance = 6; b.MapColor = "STONE"; }
            else if (v == "STONE") { b.Hardness = 1.5; b.Resistance = 6; b.MapColor = "STONE"; }
            else if (v == "OBSIDIAN") { b.Hardness = 50; b.Resistance = 1200; b.MapColor = "COLOR_BLACK"; }
            else if (v == "GLASS") { b.Hardness = 0.3; b.Resistance = 0.3; b.Sound = "GLASS"; b.NoOcclusion = true; }
            else if (v == "SLIME_BLOCK") { b.Friction = 0.8; b.Sound = "SLIME_BLOCK"; b.NoOcclusion = true; b.MapColor = "GRASS"; }
            else if (v == "OAK_LEAVES" || v == "SPRUCE_LEAVES" || v == "BIRCH_LEAVES" || v == "JUNGLE_LEAVES" || v == "ACACIA_LEAVES" || v == "DARK_OAK_LEAVES" || v == "MANGROVE_LEAVES")
            { b.Hardness = 0.2; b.Resistance = 0.2; b.Sound = "GRASS"; b.NoOcclusion = true; b.MapColor = "PLANT"; }
            else if (v == "TORCH" || v == "WALL_TORCH") { b.Instabreak = true; b.Light = 14; b.Sound = "WOOD"; b.NoCollision = true; }
            else if (v == "MOSS_CARPET") { b.Hardness = 0.1; b.Resistance = 0.1; b.Sound = "MOSS_CARPET"; b.MapColor = "COLOR_GREEN"; }
            else if (v == "AIR") { b.NoCollision = true; }
            else unknownVanilla.Add(v);
        }

        static readonly SortedSet<string> unknownVanilla = new SortedSet<string>(StringComparer.Ordinal);
        static readonly SortedSet<string> unparsedStrength = new SortedSet<string>(StringComparer.Ordinal);

        static void ParseProps(JBlock b, string s, bool resetCopy = true)
        {
            Match m;
            if (resetCopy)
            {
                m = Regex.Match(s, @"of(?:Full|Legacy)Copy\(\s*([\w.]+?)(?:\.get\(\))?\s*\)");
                if (m.Success) b.CopyOf = m.Groups[1].Value.StartsWith("TFBlocks.") ? m.Groups[1].Value.Substring(9) : m.Groups[1].Value;
            }
            // strength(h[, r]) / destroyTime(h) / explosionResistance(r): literals or products of literals ("0.2F * 64.0F")
            const string num = @"-?[\d.]+[FfDd]?(?:\s*[*/]\s*[\d.]+[FfDd]?)*";
            foreach (Match sm in Regex.Matches(s, @"\.strength\(\s*(" + num + @")\s*(?:,\s*(" + num + @"))?\s*\)"))
            {
                double h, r;
                if (!TryFloat(sm.Groups[1].Value, out h)) continue;
                b.Hardness = h;
                b.Resistance = sm.Groups[2].Success && TryFloat(sm.Groups[2].Value, out r) ? r : h;
            }
            foreach (Match sm in Regex.Matches(s, @"\.strength\(([^()]*(?:\([^()]*\))?[^()]*)\)"))
                if (!Regex.IsMatch(sm.Groups[1].Value, @"^\s*" + num + @"\s*(?:,\s*" + num + @")?\s*$")) unparsedStrength.Add(b.Id + ": " + sm.Value);
            m = Regex.Match(s, @"\.destroyTime\(\s*(" + num + @")\s*\)");
            double d;
            if (m.Success && TryFloat(m.Groups[1].Value, out d)) b.Hardness = d;
            m = Regex.Match(s, @"\.explosionResistance\(\s*(" + num + @")\s*\)");
            if (m.Success && TryFloat(m.Groups[1].Value, out d)) b.Resistance = d;
            if (s.Contains(".instabreak()")) { b.Hardness = 0; b.Resistance = 0; b.Instabreak = true; }
            if (b.Hardness < 0) b.Unbreakable = true;
            m = Regex.Match(s, @"\.lightLevel\(\s*\(?\s*\w+\s*\)?\s*->\s*(\d+)\s*\)");
            if (m.Success) { b.Light = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture); b.DynamicLight = false; b.LightExpr = null; }
            else if (s.Contains(".lightLevel(")) { b.DynamicLight = true; b.Light = 0; b.LightExpr = Light.LambdaOf(s); }
            m = Regex.Match(s, @"\.sound\(\s*(?:SoundType|TFSoundTypes)\.(\w+)\s*\)");
            if (m.Success) b.Sound = m.Groups[1].Value;
            m = Regex.Match(s, @"(?:\.mapColor|logProperties)\(\s*(?:MapColor\.(\w+)|\(state\)[^?]*\?\s*(\w+))");
            if (m.Success) b.MapColor = m.Groups[1].Success ? m.Groups[1].Value : null;
            m = Regex.Match(s, @"\.mapColor\(\s*DyeColor\.(\w+)\s*\)");
            if (m.Success) b.MapColor = "COLOR_" + m.Groups[1].Value;
            if (s.Contains(".noCollission()")) b.NoCollision = true;
            if (s.Contains(".noOcclusion()")) b.NoOcclusion = true;
            if (s.Contains(".ignitedByLava()")) b.Flammable = true;
            if (s.Contains(".replaceable()")) b.Replaceable = true;
            m = Regex.Match(s, @"\.friction\(\s*([\d.]+F?)\s*\)");
            if (m.Success) b.Friction = F(m.Groups[1].Value);
        }

        // Minecraft 1.21.1 Tiers: uses
        static readonly Dictionary<string, int> VanillaTierUses = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "WOOD", 59 }, { "STONE", 131 }, { "IRON", 250 }, { "DIAMOND", 1561 }, { "GOLD", 32 }, { "NETHERITE", 2031 },
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
        static Dictionary<string, int> tfTiers;

        /// <summary>Forgets every per-run cache (the assembly stays loaded in a PowerShell session between runs).</summary>
        public static void Reset()
        {
            itemChains.Clear();
            tfTiers = null;
            classFiles = null;
            unknownVanilla.Clear();
            unparsedStrength.Clear();
            JavaStates.Reset();
        }

        /// <summary>The comment-stripped text of a TF class file (cached), or null for a vanilla class.</summary>
        public static string ClassText(Ctx ctx, string cls)
        {
            return JavaStates.Text(ctx, cls);
        }

        public static Dictionary<string, JItem> Items(Ctx ctx)
        {
            // TFToolMaterials: new SimpleTier(incorrectBlocks, uses, speed, damage, enchantability, repair)
            tfTiers = new Dictionary<string, int>(StringComparer.Ordinal);
            string tiers = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "util", "TFToolMaterials.java")));
            foreach (Match tm in Regex.Matches(tiers, @"Tier\s+(\w+)\s*=\s*new\s+SimpleTier\(\s*[\w.]+\s*,\s*(\d+)\s*,"))
                tfTiers[tm.Groups[1].Value] = int.Parse(tm.Groups[2].Value, CultureInfo.InvariantCulture);
            if (tfTiers.Count == 0) ctx.Warn("TFToolMaterials: no tier parsed");
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFItems.java")));
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
                // whatever the TF subclass is called (KnightmetalPickItem, MinotaurAxeItem, CustomDamageSwordItem, ...)
                var chain = ClassChain(ctx, it.Cls, itemChains);
                if (chain.Any(c => TieredBases.Contains(c)))
                {
                    var tm = Regex.Match(f.Item3, @"\b(TFToolMaterials|Tiers)\.([A-Z_]+)\b");
                    int uses;
                    if (tm.Success && (tm.Groups[1].Value == "Tiers" ? VanillaTierUses.TryGetValue(tm.Groups[2].Value, out uses) : tfTiers.TryGetValue(tm.Groups[2].Value, out uses)))
                        it.Durability = uses;
                    else ctx.Warn("item " + it.Id + ": tiered item without a known tier (" + string.Join(" < ", chain) + ")");
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
                m = Regex.Match(f.Item3, @"\.rarity\(\s*(?:Rarity\.|TFRarityEnumExtension\.\w*\.|tfRarityEnumExtension\.)?(\w+)");
                if (m.Success) it.Rarity = m.Groups[1].Value;
                if (f.Item3.Contains(".food(")) it.Food = true;
                res[it.Id] = it;
            }
            return res;
        }

        public static Dictionary<string, JEntity> Entities(Ctx ctx)
        {
            var names = new Dictionary<string, string>();
            foreach (Match m in Regex.Matches(Ctx.ReadText(Path.Combine(ctx.JavaDir, "entity", "TFEntityNames.java")), @"ResourceLocation (\w+) = prefix\(""([a-z0-9_]+)""\)"))
                names[m.Groups[1].Value] = m.Groups[2].Value;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFEntities.java")));
            var res = new Dictionary<string, JEntity>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredHolder<EntityType"))
            {
                var m = Regex.Match(f.Item3, @"^\s*(make|buildNoEgg|build)\(\s*TFEntityNames\.(\w+)\s*,\s*(.*)$", RegexOptions.Singleline);
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

        /// <summary>TFCreativeTabs: the 4 tabs, their icon and their item ids in order.</summary>
        public static List<Tab> CreativeTabs(Ctx ctx, Dictionary<string, JBlock> blocks, Dictionary<string, JItem> items, IEnumerable<string> spawnEggs)
        {
            var constId = new Dictionary<string, string>();
            foreach (var b in blocks.Values) constId["TFBlocks." + b.Const] = b.Id;
            foreach (var i in items.Values) constId["TFItems." + i.Const] = i.Id;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFCreativeTabs.java")));
            var tabs = new List<Tab>();
            var re = new Regex(@"TABS\.register\(""(\w+)""");
            var starts = re.Matches(text).Cast<Match>().ToList();
            for (int t = 0; t < starts.Count; t++)
            {
                int from = starts[t].Index, to = t + 1 < starts.Count ? starts[t + 1].Index : text.IndexOf("private static void", from, StringComparison.Ordinal);
                if (to < 0) to = text.Length;
                string body = text.Substring(from, to - from);
                var tab = new Tab { Id = "twilightforest:" + starts[t].Groups[1].Value };
                var tk = Regex.Match(body, @"translatable\(""([^""]+)""\)");
                if (tk.Success) tab.TitleKey = tk.Groups[1].Value;
                var ic = Regex.Match(body, @"\.icon\(\(\)\s*->\s*new ItemStack\((TF\w+\.\w+)");
                if (ic.Success) { tab.IconConst = ic.Groups[1].Value; string icon; if (constId.TryGetValue(tab.IconConst, out icon)) tab.Icon = "twilightforest:" + icon; }
                var seen = new HashSet<string>();
                foreach (Match m in Regex.Matches(body, @"output\.accept\((TF(?:Blocks|Items)\.\w+)|(generateGearWithEnchants|createDefaultSkullCandle)\(output,\s*(TF(?:Blocks|Items)\.\w+)|(createSpawnEggsAlphabetical|createCaskets|createGlassSwordAndLoreVer)\(output"))
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
                    foreach (var a in add) if (seen.Add(a)) tab.Items.Add("twilightforest:" + a);
                }
                if (tab.Icon == null && tab.Items.Count > 0) tab.Icon = tab.Items[0];
                tabs.Add(tab);
            }
            return tabs;
        }

        /// <summary>The superclass chain of a TF block class (TF classes from block/*.java, then the vanilla base).</summary>
        static Dictionary<string, string> classFiles;

        /// <summary>The source file of a TF class by simple name (null for vanilla classes).</summary>
        public static string ClassFile(Ctx ctx, string cls)
        {
            if (classFiles == null)
            {
                classFiles = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var f in Directory.GetFiles(ctx.JavaDir, "*.java", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal))
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
                        var m = Regex.Match(Ctx.ReadText(file), @"class\s+" + c + @"\b[^{]*?\bextends\s+([\w.]+)");
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
