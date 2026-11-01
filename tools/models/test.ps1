<#
.SYNOPSIS
  Tests of the model converter (contract "geo", unit F2): parser, interpreter, conversion and the
  committed assets/gen/geo tree.

.DESCRIPTION
  - lexer and parser: Java literals, generics, casts, lambdas, switch expressions, local classes
  - interpreter: Java's int / float / double semantics, string concatenation, Mth's sine table,
    LegacyRandomSource (java.util.Random's LCG), loops, helper methods, addOrReplaceChild
  - conversion: pivots, cube origins, rotations and UVs against Bedrock's vanilla pig geometry,
    per-face UV of fractional and mirrored cubes, unique bone names, extracts (wrapper rotation)
  - setupAnim -> Molang (look_at_target, the quadruped walk)
  - the three hard models (Hydra, Naga segment, Snow Queen) against values worked out by hand
    from TF's Java code
  - every committed geometry / animation / attachable: valid structure, unique bones, parents,
    UVs inside the texture; and convert.ps1 -Check reproduces the tree byte for byte

  Exit code 0 = every test passed, 1 = a failure (listed).

.EXAMPLE
  pwsh tools/models/test.ps1
#>
param([switch]$SkipTree)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/Models.psm1') -Force
Import-GfModelsAssembly
$root = (Resolve-Path "$PSScriptRoot/../..").Path

$script:pass = 0
$script:fail = [System.Collections.Generic.List[string]]::new()
function Test([string]$name, [scriptblock]$body) {
    try { & $body; $script:pass++; Write-Host "  ok   $name" }
    catch { $script:fail.Add("$name : $($_.Exception.Message)"); Write-Host "  FAIL $name : $($_.Exception.Message)" -ForegroundColor Red }
}
function Eq($actual, $expected, [string]$what = '') {
    $a = if ($null -eq $actual) { '<null>' } else { "$actual" }
    $e = if ($null -eq $expected) { '<null>' } else { "$expected" }
    if ($a -cne $e) { throw "$what expected '$e', got '$a'" }
}
function Near([double]$actual, [double]$expected, [double]$eps = 1e-4, [string]$what = '') {
    if ([Math]::Abs($actual - $expected) -gt $eps) { throw "$what expected $expected, got $actual" }
}
function AssertType($v, [string]$t, [string]$what = '') { if ($v.GetType().Name -ne $t) { throw "$what expected $t, got $($v.GetType().Name) ($v)" } }

$cp = [Gf.Models.ClassPath]::new((Get-GfTfJavaRoot))
$interp = [Gf.Models.Interp]::new($cp)
function J([string]$expr) { $interp.Eval([Gf.Models.Parser]::ParseExpression($expr), [Gf.Models.Frame]::new($null)) }

# A tiny Java model class compiled from source (Java model space, LGPL-free test fixture).
function New-TestModel([string]$body, [string]$name = 'TestModel') {
    $src = "package test; public class $name { $body }"
    $cu = [Gf.Models.Parser]::ParseUnit($src, "$name.java")
    return $cu.Types[0]
}
function Convert-Layer($layer, [string]$vanilla = $null, [string]$id = 'geometry.test') {
    # the vanilla skeleton names of the model's vanilla family (none for $null)
    $opt = [Gf.Models.ConvertOptions]::new()
    $opt.Identifier = $id
    foreach ($kv in [Gf.Models.Converter]::RenamesFor($vanilla).GetEnumerator()) { $opt.Rename[$kv.Key] = $kv.Value }
    $geo = [Gf.Models.Converter]::Convert($layer, $opt, $null, 1.0)
    $json = [Gf.Models.Json]::Write($geo.ToJson()) | ConvertFrom-Json
    return @{ Geo = $geo; Json = $json; Bones = @{} + (& { $h = @{}; foreach ($b in $json.'minecraft:geometry'[0].bones) { $h[$b.name] = $b }; $h }) }
}

Write-Host '[geo-test] lexer and parser'
Test 'number literals keep their Java types' {
    $t = [Gf.Models.Lexer]::Tokenize('1.0F 4 0.5D 100L 0x10 .5f 1e3 2.')
    AssertType $t[0].Value 'Single'; AssertType $t[1].Value 'Int32'; AssertType $t[2].Value 'Double'; AssertType $t[3].Value 'Int64'
    Eq $t[4].Value 16; AssertType $t[5].Value 'Single'; AssertType $t[6].Value 'Double'; AssertType $t[7].Value 'Double'
}
Test 'generics, casts, lambdas, method references and switch expressions parse' {
    $c = New-TestModel @'
    private static final java.util.Map<String, java.util.List<Integer>> M = null;
    public static <T extends Object> int f(int i) {
        java.util.function.Supplier<Integer> s = () -> (int) (float) i;
        Runnable r = Foo::bar;
        int v = switch (i) { case 0 -> 1; case 1, 2 -> { yield 2; } default -> 3; };
        float g = (float) -i + (i > 1 ? 1.0F : 2.0F);
        return v;
    }
'@
    $m = $c.Methods | Where-Object Name -eq 'f'
    $b = [Gf.Models.Parser]::ParseBody($c.Unit, $m)
    Eq $b.Stmts.Count 5
}

Write-Host '[geo-test] interpreter'
Test 'int / float / double arithmetic follows Java' {
    Eq (J '7 / 2') 3; AssertType (J '7 / 2') 'Int32'
    Eq (J '7 / 2.0F') 3.5; AssertType (J '7 / 2.0F') 'Single'
    AssertType (J '7 / 2.0') 'Double'
    Eq (J '(int) 3.9F') 3; Eq (J '(int) -3.9F') -3
    Eq (J '-7 % 3') -1
    Eq (J '1 << 4') 16
    Eq (J '"a" + 1 + 2') 'a12'; Eq (J '1 + 2 + "a"') '3a'
    Eq (J '"x" + 1.0F') 'x1.0'
    AssertType (J '0.1F + 0.2F') 'Single'
    Eq ([float](J '0.1F + 0.2F')).ToString('R', [Globalization.CultureInfo]::InvariantCulture) '0.3'
}
Test 'Mth constants and the sine table' {
    Eq (J 'Mth.PI') ([float]3.1415927); AssertType (J 'Mth.DEG_TO_RAD') 'Single'
    Near (J 'Mth.sin(Mth.PI / 2.0F)') 1.0 1e-6
    Near (J 'Mth.cos(0.0F)') 1.0 1e-6
    Near (J 'Mth.cos(Mth.PI)') -1.0 1e-6
    AssertType (J 'Mth.cos(1.0F)') 'Single'
}
Test 'RandomSource.create(seed) is java.util.Random (LegacyRandomSource)' {
    $r = [Gf.Models.JRandom]::new(42)
    Eq $r.NextInt() -1170105035
    $r = [Gf.Models.JRandom]::new(42); Eq $r.NextInt(10) 0
    $r = [Gf.Models.JRandom]::new(42); Eq ([float]$r.NextFloat()).ToString('R', [Globalization.CultureInfo]::InvariantCulture) '0.7275637'
}
Test 'loops, helper methods, addOrReplaceChild keeps the old children' {
    $c = New-TestModel @'
    static final int COUNT = 3;
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        PartDefinition root = mesh.getRoot();
        var a = root.addOrReplaceChild("a", CubeListBuilder.create(), PartPose.ZERO);
        for (int i = 0; i < COUNT; i++) add(a, "c_" + i, i);
        root.addOrReplaceChild("a", CubeListBuilder.create().texOffs(1, 2).addBox(0, 0, 0, 1, 1, 1), PartPose.offset(1, 2, 3));
        return LayerDefinition.create(mesh, 16, 16);
    }
    static void add(PartDefinition p, String n, int i) {
        p.addOrReplaceChild(n, CubeListBuilder.create().addBox(0.0F, 0.0F, 0.0F, i, 1.0F, 1.0F), PartPose.offset(i * 2, 0, 0));
    }
'@
    $layer = $interp.CallStatic($c, 'create', [System.Collections.Generic.List[object]]::new())
    $a = $layer.Mesh.Root.Get('a')
    Eq $a.Children.Count 3 'children kept'
    Eq $a.Cubes.Count 1
    Eq $a.Pose.Z 3
    Eq $a.Get('c_2').Cubes[0].W 2
}
Test 'switch expressions and seeded randoms in a builder' {
    $c = New-TestModel @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        RandomSource rand = RandomSource.create(42L);
        for (int i = 0; i < 2; ++i) {
            float len = rand.nextInt(10) + 8.0F;
            mesh.getRoot().addOrReplaceChild("t" + i, CubeListBuilder.create().addBox(0, 0, 0, 1, len, 1),
                switch (i) { case 0 -> PartPose.offset(1, 0, 0); default -> { yield PartPose.offset(2, 0, 0); } });
        }
        return LayerDefinition.create(mesh, 16, 16);
    }
'@
    $layer = $interp.CallStatic($c, 'create', [System.Collections.Generic.List[object]]::new())
    Eq $layer.Mesh.Root.Get('t0').Cubes[0].H 8 'first nextInt(10) of seed 42 is 0'
    Eq $layer.Mesh.Root.Get('t1').Pose.X 2
}

Write-Host '[geo-test] conversion'
# Minecraft's quadruped (as the vanilla pig, legHeight 6): Bedrock's geometry.pig is known.
$quad = $interp.CallStatic((New-TestModel @'
    public static LayerDefinition create() { return LayerDefinition.create(QuadrupedModel.createBodyMesh(6, CubeDeformation.NONE), 64, 32); }
'@), 'create', [System.Collections.Generic.List[object]]::new())
$pig = Convert-Layer $quad 'PigModel'
Test 'pivots and cube origins match Bedrock''s pig' {
    Eq ($pig.Bones['head'].pivot -join ',') '0,12,-6'
    Eq ($pig.Bones['head'].cubes[0].origin -join ',') '-4,8,-14'
    Eq ($pig.Bones['body'].pivot -join ',') '0,13,2'
    Eq ($pig.Bones['body'].cubes[0].origin -join ',') '-5,7,-5'
    Eq ($pig.Bones['leg0'].pivot -join ',') '-3,6,7'
    Eq ($pig.Bones['leg0'].cubes[0].origin -join ',') '-5,0,5'
    Eq ($pig.Bones['leg3'].pivot -join ',') '3,6,-5'
}
Test 'rotations are Java''s in degrees (pig body 90, 0, 0)' {
    Eq ($pig.Bones['body'].rotation -join ',') '90,0,0'
}
Test 'box UV and mirror' {
    Eq ($pig.Bones['body'].cubes[0].uv -join ',') '28,8'
    $h = Convert-Layer ($interp.CallStatic((New-TestModel @'
        public static LayerDefinition create() { return LayerDefinition.create(HumanoidModel.createMesh(new CubeDeformation(0.5F), 0.0F), 64, 32); }
'@), 'create', [System.Collections.Generic.List[object]]::new())) 'HumanoidModel'
    Eq $h.Bones['leftArm'].cubes[0].mirror $true
    Eq ($h.Bones['rightArm'].pivot -join ',') '-5,22,0'
    Eq ($h.Bones['rightArm'].cubes[0].origin -join ',') '-8,12,-2'
    Eq $h.Bones['rightArm'].cubes[0].inflate 0.5
    Eq $h.Bones['hat'].parent 'head' 'hat follows the head'
    Eq $h.Bones['hat'].cubes[0].inflate 1
}
Test 'fractional sizes get per-face UV with Java''s regions; mirror flips them' {
    $c = [Gf.Models.CubeDef]::new(); $c.U = 10; $c.V = 20; $c.W = 3; $c.H = 12; $c.D = 1.5
    $uv = [Gf.Models.Json]::Write([Gf.Models.Converter]::PerFaceUv($c)) | ConvertFrom-Json
    Eq ($uv.north.uv -join ',') '11.5,21.5'; Eq ($uv.north.uv_size -join ',') '3,12'
    Eq ($uv.east.uv -join ',') '10,21.5'; Eq ($uv.east.uv_size -join ',') '1.5,12'
    Eq ($uv.south.uv -join ',') '16,21.5'; Eq ($uv.west.uv -join ',') '14.5,21.5'
    Eq ($uv.up.uv -join ',') '11.5,20'; Eq ($uv.up.uv_size -join ',') '3,1.5'
    Eq ($uv.down.uv -join ',') '14.5,21.5'; Eq ($uv.down.uv_size -join ',') '3,-1.5'
    $c.Mirror = $true
    $m = [Gf.Models.Json]::Write([Gf.Models.Converter]::PerFaceUv($c)) | ConvertFrom-Json
    Eq ($m.north.uv -join ',') '14.5,21.5'; Eq ($m.north.uv_size -join ',') '-3,12'
    Eq ($m.east.uv -join ',') '16,21.5'; Eq ($m.east.uv_size -join ',') '-1.5,12'
    Eq ($m.west.uv -join ',') '11.5,21.5'; Eq ($m.up.uv -join ',') '14.5,20'
}
Test 'duplicate part names get unique bone names' {
    $c = New-TestModel @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        var a = mesh.getRoot().addOrReplaceChild("a", CubeListBuilder.create(), PartPose.ZERO);
        var b = mesh.getRoot().addOrReplaceChild("b", CubeListBuilder.create(), PartPose.ZERO);
        a.addOrReplaceChild("spike", CubeListBuilder.create(), PartPose.ZERO);
        b.addOrReplaceChild("spike", CubeListBuilder.create(), PartPose.ZERO);
        return LayerDefinition.create(mesh, 16, 16);
    }
'@
    $g = Convert-Layer ($interp.CallStatic($c, 'create', [System.Collections.Generic.List[object]]::new()))
    Eq (($g.Json.'minecraft:geometry'[0].bones.name) -join ',') 'a,spike,b,b_spike'
}
Test 'Bedrock euler angles of a rotation matrix round-trip' {
    foreach ($t in @(@(30, 0, 0), @(0, 45, 0), @(0, 0, -60), @(20, -35, 50), @(0, 180, 0))) {
        $j = [Gf.Models.M3]::Zyx($t[0] * [Math]::PI / 180, $t[1] * [Math]::PI / 180, $t[2] * [Math]::PI / 180)
        # a Java rotation acts on Bedrock coordinates as E R E (E = diag(1, -1, 1))
        $e = [Gf.Models.M3]::Diag(1, -1, 1)
        $eul = [Gf.Models.Converter]::BedrockEuler([Gf.Models.M3]::op_Multiply([Gf.Models.M3]::op_Multiply($e, $j), $e))
        $back = [Gf.Models.M3]::Zyx($eul.X * [Math]::PI / 180, $eul.Y * [Math]::PI / 180, $eul.Z * [Math]::PI / 180)
        for ($r = 0; $r -lt 3; $r++) { for ($k = 0; $k -lt 3; $k++) { Near $back.A[$r, $k] $j.A[$r, $k] 1e-6 "($($t -join ',')) [$r,$k]" } }
    }
}

Write-Host '[geo-test] the generator against TF 4.8'
$gen = [Gf.Models.Generator]::new((Get-GfTfJavaRoot), (Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw))
$gen.Run($null, $null, $null)
$files = $gen.Files
function Geo([string]$unit, [string]$name) { ($files["assets/gen/geo/$unit/rp/models/entity/$name.geo.json"] | ConvertFrom-Json).'minecraft:geometry'[0] }
function BoneOf($geo, [string]$name) { $geo.bones | Where-Object name -eq $name }
function BoneMap([string]$unit, [string]$name) { $files["assets/gen/geo/$unit/bones/$name.json"] | ConvertFrom-Json }
Test 'every TF layer is registered and described (101 layers)' {
    Eq $gen.Layers.Count 101
    Eq $gen.Errors.Count 0 ($gen.Errors -join '; ')
}
Test 'Hydra (HydraModel.create): body, legs, three heads with jaws and frills, necks, tail' {
    $g = Geo 'hydra' 'hydra'
    Eq $g.description.texture_width 512; Eq $g.description.texture_height 256
    Eq $g.bones.Count 25
    $body = BoneOf $g 'body'
    Eq ($body.pivot -join ',') '0,36,0'            # offset (0, -12, 0)
    Eq ($body.rotation -join ',') '70,0,0'         # 1.22173 rad
    Eq ($body.cubes[0].origin -join ',') '-48,-60,0'
    $head1 = BoneOf $g 'head_1'
    Eq ($head1.pivot -join ',') '-74,124,-32'      # offset (-74, -100, -32)
    $frill = BoneOf $g 'frill_1'
    Eq $frill.parent 'head_1'
    Eq ($frill.pivot -join ',') '-74,124,-42'      # (-74, -100, -42)
    Eq ($frill.rotation -join ',') '-30,0,0'       # -0.5235988 rad
    Eq (BoneOf $g 'neck_1c').parent 'neck_1b'
    Eq ((BoneOf $g 'neck_1c').pivot -join ',') '-74,104,-32' # -42-16-16, 24-(-48-16-16), 0-16-16
    Eq ((BoneOf $g 'tail_4').pivot -join ',') '0,18,204'      # z 108 + 3 * 32
    Eq (BoneOf $g 'leftLeg').cubes[0].mirror $true
}
Test 'Naga segment (NagaModel.create, naga_body): one 16x16x16 cube at the pivot' {
    $g = Geo 'naga' 'naga_body'
    Eq $g.bones.Count 1
    Eq ($g.bones[0].cubes[0].origin -join ',') '-8,20,-8'      # (-8, -12, -8) + 16 -> y 24 - 4
    Eq ($g.bones[0].cubes[0].size -join ',') '16,16,16'
}
Test 'Snow Queen (SnowQueenModel.create): crown spikes keep unique names under the head' {
    $g = Geo 'aurora' 'snow_queen'
    $names = $g.bones.name
    Eq ($names | Sort-Object -Unique).Count $names.Count 'unique bone names'
    $front0 = BoneOf $g 'crown_front_0'
    Eq $front0.parent 'crown'
    Eq ($front0.pivot -join ',') '0,34,-4'                     # head (0, -4, 0) + (0, -6, -4)
    $spike = BoneOf $g 'spike_4'
    Eq $spike.parent 'crown_front_0'
    Eq ($spike.rotation -join ',') '15,0,0'                    # 10 * 1.5 degrees
    Eq (BoneOf $g 'crown_front_1_spike_4').parent 'crown_front_1'
    Eq ((BoneOf $g 'crown_side_0').rotation -join ',') '0,90,0'
    Eq ((BoneOf $g 'rightArm').cubes[0].uv -join ',') '16,16' 'whole-pixel sizes keep box UV'
    Eq ((BoneOf $g 'rightArm').cubes[0].origin -join ',') '-7,12,-1.5'
    Eq ((BoneOf $g 'body').cubes[0].size -join ',') '8,23,4'
}
Test 'Death tome: 0.005-deep pages get per-face UV (Bedrock box UV would floor the depth)' {
    $g = Geo 'lich' 'death_tome'
    $page = BoneOf $g 'cover_right'
    Eq ($page.cubes[0].uv.north.uv -join ',') '0.005,0.005'
    Eq ($page.cubes[0].size -join ',') '6,10,0.005'
}
Test 'setupAnim -> Molang: head look and the quadruped walk' {
    $a = $files['assets/gen/geo/fauna/rp/animations/bunny.base.json'] | ConvertFrom-Json
    $bones = $a.animations.'animation.twilightforest.bunny.base'.bones
    Eq ($bones.head.rotation -join '|') 'query.target_x_rotation|query.target_y_rotation|0'
    Eq $bones.leg1.rotation[0] 'math.cos(query.modified_distance_moved * 38.1704) * query.modified_move_speed * 80.2141'
}
Test 'setupAnim -> Molang: the hat under the head is not turned a second time' {
    $a = $files['assets/gen/geo/caves/rp/animations/yeti.base.json'] | ConvertFrom-Json
    $bones = $a.animations.'animation.twilightforest.yeti.base'.bones
    Eq ($bones.head.rotation -join '|') 'query.target_x_rotation|query.target_y_rotation|0'
    if ($bones.PSObject.Properties.Name -contains 'hat') { throw 'the hat (a child of the head) has its own copy of the head rotation' }
}
# setupAnim of a test model (the whole class source, LGPL-free fixture) over a test mesh: the
# animation's bones, the statements and the result.
function Translate-Test([string]$classSrc, [string]$meshBody, $baseCases = $null) {
    $cls = [Gf.Models.Parser]::ParseUnit("package test; $classSrc", 'Test.java').Types[0]
    $mesh = $interp.CallStatic((New-TestModel $meshBody 'TestMesh'), 'create', [System.Collections.Generic.List[object]]::new())
    $tr = [Gf.Models.AnimTranslator]::new($cp, $interp)
    if ($baseCases) { $tr.BaseCases = $baseCases }
    $g = Convert-Layer $mesh $tr.VanillaBaseOf($cls)
    $res = $tr.Translate($cls, $g.Geo)
    $json = [Gf.Models.Json]::Write([Gf.Models.AnimTranslator]::AnimationJson('animation.test', $res)) | ConvertFrom-Json
    return @{
        Res = $res; Bones = $json.animations.'animation.test'.bones
        Translated = @($res.Statements | Where-Object Translated | ForEach-Object Java)
        Todo = @($res.Statements | Where-Object { -not $_.Translated })
    }
}
function TodoOf($t, [string]$javaStart) {
    $s = @($t.Todo | Where-Object { $_.Java.StartsWith($javaStart) })
    if ($s.Count -ne 1) { throw "expected one todo entry for '$javaStart', got $($s.Count) (todo: $(($t.Todo | ForEach-Object Java) -join ' | '))" }
    return $s[0]
}
$headMesh = @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        mesh.getRoot().addOrReplaceChild("head", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        return LayerDefinition.create(mesh, 16, 16);
    }
'@
$humanoidMesh = @'
    public static LayerDefinition create() { return LayerDefinition.create(HumanoidModel.createMesh(CubeDeformation.NONE, 0.0F), 64, 64); }
'@
Test 'setupAnim -> Molang: locals follow reassignments; a condition that does not translate keeps the base case' {
    $t = Translate-Test @'
public class TestAnim {
    private final ModelPart head;
    public TestAnim(ModelPart root) { this.head = root.getChild("head"); }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        float f = ageInTicks * 0.1F;
        f += 1.0F;
        this.head.xRot = f;
        float g = 2.0F;
        if (entity.isAngry()) { g = 3.0F; }
        this.head.yRot = g;
        this.head.zRot = 1.0F / 0.0F;
    }
}
'@ $headMesh
    $rot = $t.Bones.head.rotation
    Eq $rot[0] 'query.life_time * 114.5916 + 57.2958' 'f = ageInTicks * 0.1 + 1 (radians -> degrees)'
    Eq $rot[1] 114.5916 'g = 2 on the base path (entity.isAngry() counts as false)'
    Eq $rot[2] 0 'division by zero is left untranslated'
    $case = TodoOf $t 'if (entity.isAngry())'
    if (-not $case.Case) { throw 'the condition is not a case entry' }
    Eq ($case.Affects -join ',') 'head.yRot' 'the case names the channel that differs when it holds'
    if ($t.Translated -notcontains 'this.head.yRot = g;') { throw 'head.yRot = g is not translated' }
    Eq (TodoOf $t 'this.head.zRot').Reason 'head.zRot (line 11) is not translated: division by zero'
}
Test 'setupAnim -> Molang: a compound assignment after an untranslated one stays in todo (LoyalZombieModel pattern)' {
    # LoyalZombieModel: xRot = f2 (unknown), then xRot += attack swing; the += must not land on the default pose
    $t = Translate-Test @'
public class TestZombie extends HumanoidModel<Zombie> {
    public TestZombie(ModelPart root) { super(root); }
    public void setupAnim(Zombie e, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        float f = Mth.sin(this.attackTime * Mth.PI);
        this.rightArm.xRot = 0.5F;
        float f2 = e.getArmAngle();
        this.rightArm.xRot = f2;
        this.rightArm.xRot += f * 1.2F;
        this.leftArm.xRot = -f;
        if (e.isClimbing()) { this.leftArm.xRot = 1.0F; }
        this.leftArm.xRot -= 0.1F;
        this.leftArm.yRot = 0.1F;
        for (int i = 0; i < 2; i++) { this.leftArm.yRot += 0.1F; }
        this.leftArm.yRot += 0.2F;
    }
}
'@ $humanoidMesh
    if ($t.Bones.rightArm.rotation -and $t.Bones.rightArm.rotation[0] -ne 0) { throw "rightArm.xRot has a channel: $($t.Bones.rightArm.rotation[0])" }
    Eq (TodoOf $t 'this.rightArm.xRot += f').Reason 'previous value at line 7 not translated'
    Eq (TodoOf $t 'this.rightArm.xRot = f2').Reason "rightArm.xRot (line 7) is not translated: local 'f2' (line 6) is not translated"
    $lost = TodoOf $t 'this.rightArm.xRot = 0.5F'
    if ($lost.Reason -notlike 'its effect is not in the base animation: rightArm.xRot*') { throw "the overwritten statement is still translated: $($lost.Reason)" }
    # the base case of an untranslated condition: leftArm.xRot = -f - 0.1 (isClimbing counts as false)
    Eq $t.Bones.leftArm.rotation[0] 'math.sin(variable.attack_time * 180) * (-57.2958) - 5.7296'
    Eq ((TodoOf $t 'if (e.isClimbing())').Affects -join ',') 'leftArm.xRot'
    # a loop makes the value unknown: the later += stays in todo, the earlier = leaves the translated list
    Eq $t.Bones.leftArm.rotation[1] 0 'leftArm.yRot after the loop'
    Eq (TodoOf $t 'this.leftArm.yRot += 0.2F').Reason 'previous value at line 13 not translated'
    if ($t.Translated -contains 'this.leftArm.yRot = 0.1F;') { throw 'leftArm.yRot = 0.1 is still translated after the loop changed it' }
}
Test 'setupAnim -> Molang: the real LoyalZombieModel keeps the raised arms (-80 degrees: isAggressive counts as false)' {
    $m = BoneMap 'artifacts' 'loyal_zombie'
    $a = ($files['assets/gen/geo/artifacts/rp/animations/loyal_zombie.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.loyal_zombie.base'.bones
    foreach ($arm in 'rightArm', 'leftArm') {
        if ($a.$arm.rotation[0] -notmatch '- 80$') { throw "$arm.xRot does not hold -PI / 2.25 (-80 degrees): $($a.$arm.rotation[0])" }
        if ($a.$arm.rotation[0] -notmatch 'variable\.attack_time') { throw "$arm.xRot lost the attack swing" }
        if ($a.$arm.rotation[0] -notmatch 'query\.life_time') { throw "$arm.xRot lost bobArms" }
    }
    $flag = @($m.setupAnim.todo | Where-Object { $_.java -eq 'boolean flag = e.isAggressive();' })
    Eq ($flag[0].affects -join ',') 'leftArm.xRot,rightArm.xRot'
    $ov = @($m.setupAnim.overrides_vanilla | ForEach-Object { "$($_.bone).$($_.property)" })
    Eq ($ov -join ',') 'leftArm.xRot,leftArm.yRot,leftArm.zRot,rightArm.xRot,rightArm.yRot,rightArm.zRot' 'the arm axes replace the vanilla pose'
}
Test 'setupAnim -> Molang: the Alpha Yeti and the Yeti walk and swing their arms in the base animation; only the conditions are todo' {
    $m = BoneMap 'caves' 'alpha_yeti'
    Eq (@($m.setupAnim.todo | ForEach-Object java) -join ' | ') 'if (entity.isTired()) { | if (entity.isRampaging()) { | if (entity.isVehicle()) {'
    foreach ($s in 'this.rightLeg.xRot = Mth.cos(limbSwing * 0.6662F) * 1.4F * limbSwingAmount;', 'this.rightArm.xRot = -Mth.HALF_PI;', 'this.body.y = -6F;') {
        if ($m.setupAnim.translated -notcontains $s) { throw "alpha_yeti: '$s' is not translated" }
    }
    $a = ($files['assets/gen/geo/caves/rp/animations/alpha_yeti.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.alpha_yeti.base'.bones
    Eq $a.rightLeg.rotation[0] 'math.cos(query.modified_distance_moved * 38.1704) * query.modified_move_speed * 80.2141'
    if ($a.rightArm.rotation[0] -notmatch '- 90$') { throw "alpha_yeti rightArm.xRot is not -90 + swing: $($a.rightArm.rotation[0])" }
    $tired = @($m.setupAnim.todo | Where-Object { $_.java -eq 'if (entity.isTired()) {' })[0]
    if ($tired.affects -notcontains 'body.y' -or $tired.affects -notcontains 'rightArm.xRot') { throw "isTired affects $($tired.affects -join ',')" }
    $y = BoneMap 'caves' 'yeti'
    if ($y.setupAnim.translated -notcontains 'AnimationUtils.bobArms(this.rightArm, this.leftArm, ageInTicks);') { throw 'yeti: bobArms is not translated' }
    $ya = ($files['assets/gen/geo/caves/rp/animations/yeti.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.yeti.base'.bones
    Eq $ya.rightArm.rotation[0] 'math.cos(query.modified_distance_moved * 38.1704 + 180) * query.modified_move_speed * 57.2958 + math.sin(query.life_time * 76.7763) * 2.8648'
    foreach ($e in $y.setupAnim.todo) { if ($e.why -like 'its effect is not in the base animation*') { throw "yeti: $($e.java) lost" } }
}
Test 'setupAnim -> Molang: conditions that translate become ?:; vanilla super.setupAnim makes += a delta and = an override' {
    $t = Translate-Test @'
public class TestHumanoid extends HumanoidModel<Zombie> {
    public TestHumanoid(ModelPart root) { super(root); }
    public void setupAnim(Zombie e, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        super.setupAnim(e, limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch);
        this.rightArm.xRot += Mth.PI;
        this.leftArm.zRot = 0.5F;
        this.body.xRot *= 0.5F;
        if (this.young) { this.rightLeg.yRot = 1.0F; } else { this.rightLeg.yRot = 0.5F; }
    }
}
'@ $humanoidMesh
    Eq $t.Bones.rightArm.rotation[0] 180 'a delta on top of the vanilla pose'
    Eq $t.Bones.leftArm.rotation[2] 28.6479 'an absolute value (0.5 rad)'
    Eq $t.Bones.rightLeg.rotation[1] '(query.is_baby ? 1 : 0.5) * 57.2958'
    if (-not (TodoOf $t 'this.body.xRot *= 0.5F').Reason.Contains('needs the value the vanilla HumanoidModel.setupAnim gives')) { throw 'body.xRot *= is translated' }
    Eq (@($t.Res.OverridesVanilla.Keys) -join ',') 'leftArm.zRot,rightLeg.yRot'
}
Test 'setupAnim -> Molang: a vanilla value copied onto a bone no vanilla animation drives, and changed super arguments, stay todo' {
    $t = Translate-Test @'
public class TestCape extends HumanoidModel<Zombie> {
    private final ModelPart cape;
    public TestCape(ModelPart root) { super(root); this.cape = root.getChild("cape"); }
    public void setupAnim(Zombie e, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        limbSwingAmount *= 0.75F;
        super.setupAnim(e, limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch);
        this.cape.copyFrom(this.body);
    }
}
'@ @'
    public static LayerDefinition create() {
        MeshDefinition mesh = HumanoidModel.createMesh(CubeDeformation.NONE, 0.0F);
        mesh.getRoot().addOrReplaceChild("cape", CubeListBuilder.create().addBox(-5, 0, -1, 10, 16, 1), PartPose.ZERO);
        return LayerDefinition.create(mesh, 64, 64);
    }
'@
    if (-not (TodoOf $t 'this.cape.copyFrom').Reason.Contains('onto a bone the vanilla animations do not drive')) { throw 'the cape follows only the delta of the vanilla body pose' }
    if (-not (TodoOf $t 'super.setupAnim').Reason.Contains('it passes limbSwingAmount = query.modified_move_speed * 0.75')) { throw "the changed argument is not reported: $((TodoOf $t 'super.setupAnim').Reason)" }
}
Test 'setupAnim -> Molang: copyFrom copies the initial pose too; nested and loop-header assignments invalidate' {
    $t = Translate-Test @'
public class TestCopy {
    private final ModelPart a, b, c, d;
    public TestCopy(ModelPart root) { this.a = root.getChild("a"); this.b = root.getChild("b"); this.c = root.getChild("c"); this.d = root.getChild("d"); }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.a.xRot = 1.0F;
        this.a.copyFrom(this.b);
        this.c.xRot = this.d.xRot = 0.25F;
        this.c.yRot = 0.5F;
        for (this.c.yRot = 0.0F; this.d.yRot < 1.0F; this.d.yRot += 0.5F) { }
        this.c.yRot += 0.1F;
    }
}
'@ @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        PartDefinition root = mesh.getRoot();
        root.addOrReplaceChild("a", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.offsetAndRotation(0.0F, 2.0F, 0.0F, 0.1F, 0.0F, 0.0F));
        root.addOrReplaceChild("b", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.offsetAndRotation(0.0F, 5.0F, 0.0F, 0.3F, 0.0F, 0.0F));
        root.addOrReplaceChild("c", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        root.addOrReplaceChild("d", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        return LayerDefinition.create(mesh, 16, 16);
    }
'@
    # a takes b's initial pose (0.3 rad, y 5): 0.3 - 0.1 rad = 11.4592 degrees, y -(5 - 2)
    Near $t.Bones.a.rotation[0] 11.4592 2e-4 'a.xRot = b''s initial 0.3 rad'
    Eq ($t.Bones.a.position -join ',') '0,-3,0' 'a.y = b''s initial 5'
    Eq $t.Bones.c.rotation[0] 14.3239 'c.xRot = d.xRot = 0.25'
    Eq $t.Bones.d.rotation[0] 14.3239
    if ($t.Bones.c.rotation[1] -ne 0) { throw "c.yRot set in the for header is still translated: $($t.Bones.c.rotation[1])" }
    if ($t.Bones.d.rotation[1] -ne 0 -and $null -ne $t.Bones.d.rotation[1]) { throw "d.yRot changed in the for update is still translated: $($t.Bones.d.rotation[1])" }
    Eq (TodoOf $t 'this.c.yRot += 0.1F').Reason 'previous value at line 9 not translated'
}
Test 'setupAnim -> Molang: the re-parented hat gets its position relative to the head too' {
    $t = Translate-Test @'
public class TestHat extends HumanoidModel<Zombie> {
    public TestHat(ModelPart root) { super(root); }
    public void setupAnim(Zombie e, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.head.y = 4.2F;
        this.head.xRot = 0.5F;
        this.hat.copyFrom(this.head);
    }
}
'@ $humanoidMesh
    Eq ($t.Bones.head.position -join ',') '0,-4.2,0'
    if ($t.Bones.PSObject.Properties.Name -contains 'hat') { throw "the hat (under the head) moves a second time: $($t.Bones.hat | ConvertTo-Json -Compress)" }
}
Test 'setupAnim -> Molang: catalog baseCase values (TrollModel pattern: ArmPose.EMPTY is fixed, the main arm is a case)' {
    # TrollModel adds PI to an arm when the other arm pose is not EMPTY; TF's mobs keep EMPTY, so the base
    # animation must not raise the arms (the TrollModel pattern), and a comparison with EMPTY leaves no case
    $t = Translate-Test @'
public class TestTroll extends HumanoidModel<Zombie> {
    public TestTroll(ModelPart root) { super(root); }
    public void setupAnim(Zombie entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.rightArm.xRot = Mth.cos(limbSwing * 0.6662F + Mth.PI) * limbSwingAmount;
        this.leftArm.xRot = 0.0F;
        if (this.leftArmPose != ArmPose.EMPTY) { this.rightArm.xRot += Mth.PI; }
        if (this.rightArmPose != HumanoidModel.ArmPose.EMPTY) { this.leftArm.xRot += Mth.PI; }
        boolean two = this.rightArmPose.isTwoHanded();
        if (two) { this.body.xRot = 1.0F; }
        if (entity.getMainArm() == HumanoidArm.RIGHT) { this.head.zRot = 0.5F; }
        this.poseRightArm(entity);
        if (JappaPackReloadListener.INSTANCE.isJappaPackLoaded()) { this.leftLeg.xRot = 1.0F; }
    }
}
'@ $humanoidMesh $gen.BaseCases
    Eq $t.Bones.rightArm.rotation[0] 'math.cos(query.modified_distance_moved * 38.1704 + 180) * query.modified_move_speed * 57.2958' 'no PI: the left arm pose is EMPTY'
    if ($t.Bones.PSObject.Properties.Name -contains 'leftArm') { throw "leftArm moved: $($t.Bones.leftArm | ConvertTo-Json -Compress)" }
    if ($t.Bones.PSObject.Properties.Name -contains 'body' -or $t.Bones.PSObject.Properties.Name -contains 'leftLeg') { throw 'EMPTY.isTwoHanded() / the Jappa pack counted as true' }
    Eq $t.Bones.head.rotation[2] 28.6479 'the right main arm (HumanoidArm.RIGHT)'
    $cases = @($t.Todo | Where-Object Case)
    Eq (@($cases | ForEach-Object Java) -join ' | ') 'if (entity.getMainArm() == HumanoidArm.RIGHT) { this.head.zRot = 0.5F; }' 'only the main arm is a case'
    if ($cases[0].Reason -notlike '*counts entity.getMainArm() as HumanoidArm.RIGHT (Mob.finalizeSpawn*') { throw "reason: $($cases[0].Reason)" }
    Eq ($cases[0].Affects -join ',') 'head.zRot'
    if ($t.Translated -notcontains 'this.poseRightArm(entity);') { throw 'poseRightArm with the fixed EMPTY arm pose is not translated' }
    if (-not ($t.Res.Notes | Where-Object { $_ -like 'catalog baseCase: this.leftArmPose is ArmPose.EMPTY on every frame*at line 6 (no case entry)' })) { throw "no note on the fixed arm pose: $($t.Res.Notes -join ' / ')" }
    # without the catalog the arm pose is not known: the != counts as true (the old base case), a case entry
    $u = Translate-Test @'
public class TestTroll2 extends HumanoidModel<Zombie> {
    public TestTroll2(ModelPart root) { super(root); }
    public void setupAnim(Zombie entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.rightArm.xRot = 0.0F;
        if (this.leftArmPose != ArmPose.EMPTY) { this.rightArm.xRot += Mth.PI; }
    }
}
'@ $humanoidMesh
    if (-not (TodoOf $u 'if (this.leftArmPose').Case) { throw 'without baseCase the arm pose condition is not a case' }
}
Test 'setupAnim -> Molang: the real Troll and Upper Goblin Knight take ArmPose.EMPTY (arms at their sides, the shield arm held)' {
    $a = ($files['assets/gen/geo/caves/rp/animations/troll.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.troll.base'.bones
    # attackTime 0: the walk swing only (TF: no PI from the arm poses); attacking: x - PI * (1 - attackTime)
    Eq $a.rightArm.rotation[0] '((variable.attack_time > 0) ? math.cos(query.modified_distance_moved * 38.1704 + 180) * query.modified_move_speed - variable.attack_time * (-3.1416) - 3.1416 : math.cos(query.modified_distance_moved * 38.1704 + 180) * query.modified_move_speed) * 57.2958 + math.sin(query.life_time * 76.7763) * 2.8648'
    $m = BoneMap 'caves' 'troll'
    foreach ($e in $m.setupAnim.todo) { if ($e.java -like '*ArmPose*') { throw "troll: $($e.java) is still a case" } }
    $u = BoneMap 'stronghold' 'upper_goblin_knight'
    foreach ($e in $u.setupAnim.todo) { if ($e.java -like '*ArmPose*') { throw "upper_goblin_knight: $($e.java) is still a case" } }
    $shield = @($u.setupAnim.todo | Where-Object { $_.java -eq 'boolean hasShield = entity.hasShield();' })[0]
    if ($shield.why -notlike '*counts entity.hasShield() as true (*') { throw "hasShield: $($shield.why)" }
    $ua = ($files['assets/gen/geo/stronghold/rp/animations/upper_goblin_knight.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.upper_goblin_knight.base'.bones
    # with the shield the left arm swings 0.2 (leftConstraint), and no '* 0.5 - PI / 10' of a non-EMPTY arm pose
    if ($ua.leftArm.rotation[0] -notmatch '\* 0\.2 ' -or $ua.leftArm.rotation[0] -match '- 18$') { throw "upper_goblin_knight leftArm.xRot: $($ua.leftArm.rotation[0])" }
    $lower = BoneMap 'stronghold' 'lower_goblin_knight'
    $ride = @($lower.setupAnim.todo | Where-Object { $_.java -eq 'if (entity.isVehicle()) {' })[0]
    if ($ride.why -notlike '*counts entity.isVehicle() as true (LowerGoblinKnight.finalizeSpawn*') { throw "lower knight ridden: $($ride.why)" }
}
Test 'setupAnim -> Molang: a branch that returns early is not taken, and the rest of the method is its case' {
    $t = Translate-Test @'
public class TestReturn {
    private final ModelPart head;
    public TestReturn(ModelPart root) { this.head = root.getChild("head"); }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        if (!entity.isAlive()) return;
        this.head.xRot = 1.0F;
        if (entity.isAngry()) { this.head.yRot = 0.5F; } else { return; }
        this.head.zRot = 0.25F;
    }
}
'@ $headMesh
    Eq ($t.Bones.head.rotation -join ',') '57.2958,28.6479,14.3239' 'every statement after the returns is on the base path'
    $alive = TodoOf $t 'if (!entity.isAlive())'
    if (-not $alive.Case -or $alive.Reason -notlike '*counts !entity.isAlive() as false*the if branch returns early*') { throw "isAlive: $($alive.Reason)" }
    Eq ($alive.Affects -join ',') 'head.xRot,head.yRot,head.zRot' 'what the method sets after it'
    $angry = TodoOf $t 'if (entity.isAngry())'
    if ($angry.Reason -notlike '*counts !(entity.isAngry()) as false*runs the if branch*the else branch returns early*') { throw "isAngry: $($angry.Reason)" }
    Eq ($angry.Affects -join ',') 'head.yRot,head.zRot'
}
Test 'setupAnim -> Molang: an assignment inside a condition runs first; behind && it makes its target unknown' {
    $t = Translate-Test @'
public class TestCondAssign {
    private final ModelPart head, body;
    public TestCondAssign(ModelPart root) { this.head = root.getChild("head"); this.body = root.getChild("body"); }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        if ((this.head.yRot = ageInTicks) > 2.0F) { this.head.zRot = 1.0F; }
        this.head.xRot = this.head.yRot * 2.0F;
        if (entity.isAngry() && (this.body.xRot = 1.0F) > 0.0F) { }
        this.body.xRot += 0.5F;
    }
}
'@ @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        mesh.getRoot().addOrReplaceChild("head", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        mesh.getRoot().addOrReplaceChild("body", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        return LayerDefinition.create(mesh, 16, 16);
    }
'@
    Eq $t.Bones.head.rotation[1] 'query.life_time * 1145.9156' 'head.yRot = ageInTicks (the assignment in the condition)'
    Eq $t.Bones.head.rotation[0] 'query.life_time * 2291.8312' 'a later read sees the assigned value'
    Eq $t.Bones.head.rotation[2] '((query.life_time * 20 > 2) ? 1 : 0) * 57.2958' 'the condition reads the assigned value'
    if ($t.Bones.PSObject.Properties.Name -contains 'body') { throw "body.xRot (assigned behind &&) has a channel: $($t.Bones.body | ConvertTo-Json -Compress)" }
    $andWhy = (TodoOf $t 'if (entity.isAngry() &&').Reason
    if ($andWhy -notlike '*an assignment inside the condition (this.body.xRot = 1F)*unknown afterwards*') { throw "no report of the assignment behind the &&: $andWhy" }
    Eq (TodoOf $t 'this.body.xRot += 0.5F').Reason 'previous value at line 7 not translated'
}
Test 'setupAnim -> Molang: one branch keeps the vanilla pose: the base animation takes the one that sets it (LichModel pattern)' {
    $t = Translate-Test @'
public class TestLichArm extends HumanoidModel<Zombie> {
    public TestLichArm(ModelPart root) { super(root); }
    public void setupAnim(Zombie entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        super.setupAnim(entity, limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch);
        if (entity.tickCount > 0) {
            this.leftArm.zRot = 0.5F;
            this.leftArm.xRot = 1.0F;
        } else {
            this.leftArm.xRot = 0.0F;
        }
    }
}
'@ $humanoidMesh
    Eq $t.Bones.leftArm.rotation[2] 28.6479 'the if branch (0.5 rad), not dropped'
    Eq $t.Bones.leftArm.rotation[0] '((query.life_time * 20 > 0) ? 1 : 0) * 57.2958'
    $c = TodoOf $t 'if (entity.tickCount > 0)'
    if (-not $c.Case -or $c.Reason -notlike '*leftArm.zRot: the base animation takes the if branch, which sets it (query.life_time * 20 > 0 holds on every frame after the first)*') { throw "reason: $($c.Reason)" }
    Eq ($c.Affects -join ',') 'leftArm.zRot'
    if (@($t.Res.OverridesVanilla.Keys) -notcontains 'leftArm.zRot') { throw 'leftArm.zRot is not in overrides_vanilla' }
    $lich = ($files['assets/gen/geo/lich/rp/animations/lich.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.lich.base'.bones
    Eq $lich.leftArm.rotation[2] 'math.cos(query.life_time * 297.938) * (-8.5944) + 25.7831' 'LichModel: 0.5 - (cos(age * 0.26) * 0.15 + 0.05)'
}
Test 'catalog baseCase: every entry is well-formed and used by a model' {
    $errs = [System.Collections.Generic.List[string]]::new()
    $bad = [Gf.Models.Generator]::ParseBaseCases(([System.Text.Json.JsonDocument]::Parse('{"values":[{"scope":"X","expr":"entity.a()","value":true},{"scope":"X","expr":"entity.b()","value":1,"why":"w"},{"scope":"X","expr":"entity.b()","value":2,"why":"w"}]}')).RootElement, $errs)
    Eq $bad.Count 1 'the entry without why and the duplicate are rejected'
    Eq $errs.Count 2
    $texts = [System.Collections.Generic.List[string]]::new()
    foreach ($k in $files.Keys) {
        if ($k -notlike 'assets/gen/geo/*/bones/*.json') { continue }
        $sa = ($files[$k] | ConvertFrom-Json).setupAnim
        if (-not $sa) { continue }
        foreach ($n in @($sa.notes)) { if ($n) { $texts.Add($n) } }
        foreach ($e in @($sa.todo)) { if ($e.why) { $texts.Add($e.why) } }
    }
    $all = $texts -join "`n"
    $modelSrc = (Get-ChildItem (Join-Path (Get-GfTfJavaRoot) 'twilightforest/client/model/entity') -Filter *.java | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n"
    foreach ($b in $gen.BaseCases) {
        $needle = if ($b.Fixed) { "catalog baseCase: $($b.Expr) is $($b.ValueText) on every frame" } else { " as $($b.ValueText) ($($b.Why))" }
        if ($all.Contains($needle)) { continue }
        # read only off the base path (this.rightArmPose.isTwoHanded() for a left-handed mob): TF's models
        # must still use it
        $tail = ($b.Expr -replace '^(entity|this)\.', '') -replace '\(\*\)$', '('
        if (-not $modelSrc.Contains($tail)) { throw "baseCase $($b.Scope) / $($b.Expr) is not used by any model" }
    }
}
Test 'catalog.json carries the LGPL attribution of the TF code its extracts translate' {
    $catalog = Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw | ConvertFrom-Json
    $attr = @($catalog.attribution)
    if (-not ($attr -match 'Copyright \(c\) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors\.')) { throw 'no copyright line' }
    if (-not ($attr -match 'GNU Lesser General Public License v2\.1 or later')) { throw 'no licence line' }
    $raw = Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw
    $sources = @([regex]::Matches($raw, '"source":\s*"([^"]*)"') | ForEach-Object { $_.Groups[1].Value })
    if ($sources.Count -lt 20) { throw "only $($sources.Count) extract sources" }
    $classes = @($sources | ForEach-Object { [regex]::Matches($_, '\b(TrophyRenderer|ISTER|\w+Model(?=\.renderTrophy)|\w+Item(?=[,:)]))\b') | ForEach-Object Value }) + 'UrGhastModel' | Sort-Object -Unique
    foreach ($c in $classes) { if (-not ($attr -match "/$c\.java$")) { throw "the attribution does not name $c.java" } }
}
Test 'Matches: -Java YetiModel does not select AlphaYetiModel''s layers (whole class names in the supplier)' {
    if (-not [Gf.Models.Generator]::SupplierNames('event.registerLayerDefinition(TFModelLayers.YETI, YetiModel::create);', 'YetiModel')) { throw 'YetiModel::create' }
    if ([Gf.Models.Generator]::SupplierNames('event.registerLayerDefinition(TFModelLayers.ALPHA_YETI, AlphaYetiModel::create);', 'YetiModel')) { throw 'AlphaYetiModel::create matches YetiModel' }
    if ([Gf.Models.Generator]::SupplierNames('event.registerLayerDefinition(TFModelLayers.CARMINITE_GHASTLING, TFGhastModel::create);', 'GhastModel')) { throw 'TFGhastModel matches GhastModel' }
    if (-not [Gf.Models.Generator]::SupplierNames('() -> LayerDefinition.create(ArcticArmorModel.addPieces(x), 64, 32)', 'ArcticArmorModel')) { throw 'ArcticArmorModel.addPieces' }
    $yeti = @($gen.Select('YetiModel', $null))
    $alpha = @($yeti | Where-Object { $_ -like '*alpha_yeti*' })
    if ($alpha) { throw "-Java YetiModel selects $($alpha -join ', ')" }
    if ($yeti -notcontains 'assets/gen/geo/caves/rp/models/entity/yeti.geo.json') { throw '-Java YetiModel misses yeti.geo.json' }
}
Test 'worn extracts never reuse the wearer''s bone names' {
    $g = Geo 'building' 'naga_trophy_worn'
    Eq (($g.bones.name) -join ',') 'naga_trophy_worn,naga_trophy_worn_head'
    Eq $g.bones[0].parent 'head'
    $crown = Geo 'artifacts' 'mystic_crown_worn'
    Eq (($crown.bones.name) -join ',') 'mystic_crown_worn,mystic_crown_worn_hat'
}
Test 'worn trophies use their own item model''s head display (alpha yeti: 0 2 0 x1.5, Ur-Ghast: 0 18 0)' {
    $d = (BoneMap 'building' 'alpha_yeti_trophy_worn').item_display[0]
    Eq $d.file 'src/generated/resources/assets/twilightforest/models/item/alpha_yeti_trophy.json'
    Eq ($d.translation -join ',') '0,2,0'; Eq ($d.scale -join ',') '1.5,1.5,1.5'; Eq ($d.rotation -join ',') '180,0,180'
    $u = (BoneMap 'building' 'ur_ghast_trophy_worn').item_display[0]
    Eq ($u.translation -join ',') '0,18,0'; Eq ($u.scale -join ',') '1.6,1.6,1.6'
    $n = (BoneMap 'building' 'naga_trophy_worn').item_display[0]
    Eq $n.file 'src/main/resources/assets/twilightforest/models/item/template_trophy.json' 'naga_trophy.json has no display: its parent'
    Eq ($n.translation -join ',') '0,6,0'
    # CustomHeadLayer (0.625) * head display: the Ur-Ghast body pivot is 12 px * 0.625 = 7.5 px higher than with the template's 6
    Eq ((BoneOf (Geo 'building' 'ur_ghast_trophy_worn') 'ur_ghast_trophy_worn_body').pivot -join ',') '0,27.25,0'
    # alpha yeti: scale 1.5 instead of the template's 1.6 and 4 px lower in the display's space (was 29.75 / 9.6 wide)
    $h = BoneOf (Geo 'building' 'alpha_yeti_trophy_worn') 'alpha_yeti_trophy_worn_head'
    Eq ($h.pivot -join ',') '0,27.375,0'
    Eq ($h.cubes[0].size -join ',') '9,10.125,6.75'
}
Test 'a missing thirdperson_lefthand falls back to the same model''s righthand (ItemTransforms)' {
    $u = (BoneMap 'building' 'ur_ghast_trophy_held_left').item_display[0]
    Eq $u.context 'thirdperson_lefthand'; Eq $u.from 'thirdperson_righthand'
    Eq ($u.translation -join ',') '0,4,3.6' 'ur_ghast_trophy.json thirdperson_righthand'
    $s = (BoneMap 'weapons' 'knightmetal_shield_held_left').item_display[0]
    Eq $s.from 'thirdperson_lefthand'; Eq ($s.translation -join ',') '10.51,6,12'
}
Test 'held forms: the left hand mirrors the right one (ItemTransform leftHand, ItemInHandLayer)' {
    $r = Geo 'building' 'naga_trophy_held'; $l = Geo 'building' 'naga_trophy_held_left'
    Eq $r.bones[0].parent 'rightArm'; Eq $l.bones[0].parent 'leftArm'
    Near $l.bones[0].rotation[0] $r.bones[0].rotation[0]
    Near $l.bones[0].rotation[1] (-$r.bones[0].rotation[1]); Near $l.bones[0].rotation[2] (-$r.bones[0].rotation[2])
    Near $l.bones[1].pivot[0] (-$r.bones[1].pivot[0]); Near $l.bones[1].pivot[1] $r.bones[1].pivot[1]; Near $l.bones[1].pivot[2] $r.bones[1].pivot[2]
}
Test 'one attachable per item draws each form in its slot (head, main hand, off hand)' {
    $a = ($files['assets/gen/geo/building/rp/attachables/knight_phantom_trophy.json'] | ConvertFrom-Json).'minecraft:attachable'.description
    Eq (@($a.geometry.PSObject.Properties.Value) -join ',') 'geometry.twilightforest.knight_phantom_trophy_worn,geometry.twilightforest.knight_phantom_trophy_worn_armor,geometry.twilightforest.knight_phantom_trophy_held,geometry.twilightforest.knight_phantom_trophy_held_armor,geometry.twilightforest.knight_phantom_trophy_held_left,geometry.twilightforest.knight_phantom_trophy_held_left_armor'
    $cond = @($a.render_controllers | ForEach-Object { $_.PSObject.Properties.Value })
    Eq ($cond -join '|') "c.item_slot == 'head'|c.item_slot == 'head'|c.item_slot == 'main_hand'|c.item_slot == 'main_hand'|c.item_slot == 'off_hand'|c.item_slot == 'off_hand'"
    Eq $a.materials.enchanted 'entity_alphatest_glint'
    $s = ($files['assets/gen/geo/weapons/rp/attachables/knightmetal_shield.json'] | ConvertFrom-Json).'minecraft:attachable'.description
    Eq (@($s.geometry.PSObject.Properties.Value) -join ',') 'geometry.twilightforest.knightmetal_shield_held,geometry.twilightforest.knightmetal_shield_held_left'
}
Test 'the Ur-Ghast trophies wave their tentacles (waveTentacles on the trophy clock, 0 at tick 0)' {
    foreach ($n in 'ur_ghast_trophy', 'ur_ghast_wall_trophy', 'ur_ghast_trophy_worn', 'ur_ghast_trophy_held', 'ur_ghast_trophy_held_left') {
        $a = ($files["assets/gen/geo/building/rp/animations/$n.base.json"] | ConvertFrom-Json).animations."animation.twilightforest.$n.base"
        if (-not $a) { throw "no animation for $n" }
        Eq @($a.bones.PSObject.Properties).Count 36 "$n animates 9 tentacles x 4 parts"
    }
    $b = ($files['assets/gen/geo/building/rp/animations/ur_ghast_trophy.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.ur_ghast_trophy.base'.bones
    # tentacle 0: xRot = 0.2 + cos(0.9 t / 2 * 0.3335) * 0.15 (t ticks = life_time * 20), minus the bind pose 0.35 rad
    Eq $b.tentacle_0.rotation[0] 'math.cos(query.life_time * 171.9733) * 8.5944 - 8.5943'
    $att = ($files['assets/gen/geo/building/rp/attachables/ur_ghast_trophy.json'] | ConvertFrom-Json).'minecraft:attachable'.description
    Eq $att.animations.held 'animation.twilightforest.ur_ghast_trophy_held.base'
}
Test 'every converted entity layer has a base animation (a stub when nothing translates)' {
    $index = $files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
    $anims = @{}; foreach ($a in $index.animations) { $anims[$a.identifier] = $a }
    $entities = @($index.geometries | Where-Object { $_.kind -eq 'entity' -and -not $_.vanilla })
    if ($entities.Count -lt 50) { throw "only $($entities.Count) entity layers" }
    foreach ($g in $entities) {
        $id = $g.identifier -replace '^geometry\.', 'animation.'
        if (-not $anims.ContainsKey("$id.base")) { throw "no base animation for $($g.identifier)" }
        Eq $g.animation "$id.base" $g.identifier
    }
    $stub = ($files['assets/gen/geo/weapons/rp/animations/chain.base.json'] | ConvertFrom-Json).animations.'animation.twilightforest.chain.base'
    Eq $stub.loop $true; Eq (@($stub.bones.PSObject.Properties.Name) -join ',') 'chain' 'a stub (Bedrock rejects an empty bones object)'
    Eq ($stub.bones.chain.rotation -join ',') '0,0,0'
}
Test 'setupAnim -> Molang: part.x / part.y / part.z become position deltas (x as is, y negated)' {
    $c = New-TestModel @'
    private final ModelPart moved;
    public TestPos(ModelPart root) { this.moved = root.getChild("moved"); }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.moved.x = 7.0F;
        this.moved.y = 2.0F;
        this.moved.z = -3.0F;
    }
'@ 'TestPos'
    $mesh = $interp.CallStatic((New-TestModel @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        mesh.getRoot().addOrReplaceChild("moved", CubeListBuilder.create().addBox(-1, 0, -1, 2, 2, 2), PartPose.offset(0.0F, 5.0F, 0.0F));
        return LayerDefinition.create(mesh, 16, 16);
    }
'@), 'create', [System.Collections.Generic.List[object]]::new())
    $g = Convert-Layer $mesh
    $res = [Gf.Models.AnimTranslator]::new($cp, $interp).Translate($c, $g.Geo)
    $json = [Gf.Models.Json]::Write([Gf.Models.AnimTranslator]::AnimationJson('animation.test', $res)) | ConvertFrom-Json
    # Bedrock geometry x is Java's x (pig check above); the animation's position channel moves along the
    # geometry's own axes (checked in game: the e2e preview's x-sign calibration actor)
    Eq ($json.animations.'animation.test'.bones.moved.position -join ',') '7,3,-3' 'x as is, y = -(2 - 5), z as is'
}
Test 'Select: -Java / -Item pick the files of one model class / item from the full run' {
    $hydra = @($gen.Select('HydraModel', $null))
    if (-not ($hydra -contains 'assets/gen/geo/hydra/rp/models/entity/hydra.geo.json')) { throw 'HydraModel does not select hydra.geo.json' }
    $other = @($hydra | Where-Object { $_ -notlike 'assets/gen/geo/hydra/*' })
    if ($other) { throw "HydraModel selects files of other units: $($other -join ', ')" }
    $shield = @($gen.Select($null, 'twilightforest:knightmetal_shield'))
    Eq ($shield -join ',') 'assets/gen/geo/weapons/bones/knightmetal_shield.json,assets/gen/geo/weapons/bones/knightmetal_shield_held.json,assets/gen/geo/weapons/bones/knightmetal_shield_held_left.json,assets/gen/geo/weapons/rp/attachables/knightmetal_shield.json,assets/gen/geo/weapons/rp/models/entity/knightmetal_shield.geo.json,assets/gen/geo/weapons/rp/models/entity/knightmetal_shield_held.geo.json,assets/gen/geo/weapons/rp/models/entity/knightmetal_shield_held_left.geo.json'
}
Test 'Write-GfModelsFiles: -Unit with a -Java / -Item selection writes only the selected files of that partition' {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) "gf-geo-test-$PID"
    try {
        $r = @{ Files = [ordered]@{}; Selected = @($gen.Select($null, 'twilightforest:fiery_helmet')) }
        foreach ($k in $files.Keys) { $r.Files[$k] = $files[$k] }
        $w = Write-GfModelsFiles $r -Unit 'armory' -Out $tmp
        $written = @(Get-ChildItem $tmp -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($tmp, $_.FullName).Replace('\', '/') } | Sort-Object)
        Eq ($written -join ',') 'assets/gen/geo/armory/bones/fiery_armor.helmet.json,assets/gen/geo/armory/rp/attachables/fiery_helmet.json,assets/gen/geo/armory/rp/models/entity/fiery_armor.helmet.geo.json'
        Eq $w.Problems 0
    } finally { if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp } }
}
Test 'trophies: the floor extract turns 180 degrees about y and sits on the floor' {
    $g = Geo 'building' 'naga_trophy'
    Eq ($g.bones[0].rotation -join ',') '0,180,0'
    Eq ($g.bones[1].cubes[0].origin -join ',') '-4,0,-4'
    Eq ($g.bones[1].cubes[0].size -join ',') '8,8,8'
    Eq ($g.bones[1].cubes[0].uv.north.uv_size -join ',') '16,16' 'scaled cubes keep the texture regions'
}
Test 'armor: per-slot geometry with the player''s bone names' {
    $g = Geo 'armory' 'arctic_armor.helmet'
    Eq (($g.bones.name | Sort-Object) -join ',') 'bottom_hood,hat,head,left_hood,right_hood,top_hood'
    Eq (BoneOf $g 'head').cubes[0].inflate 1
    $l = Geo 'armory' 'yeti_armor.leggings'
    Eq ((@($l.bones | Where-Object { -not $_.parent }).name | Sort-Object) -join ',') 'body,leftLeg,rightLeg'
}

if (-not $SkipTree) {
    Write-Host '[geo-test] every generated file (all partitions)'
    $index = $files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
    function Read-Gen([string]$path) { if (-not $files.ContainsKey($path)) { throw "not generated: $path" }; $files[$path] | ConvertFrom-Json }
    $geoIds = @{}
    Test 'every geometry is valid Bedrock geometry 1.12.0' {
        foreach ($e in $index.geometries | Where-Object { -not $_.vanilla }) {
            $d = Read-Gen "assets/gen/geo/$($e.unit)/rp/$($e.file)"
            Eq $d.format_version '1.12.0' $e.file
            $g = $d.'minecraft:geometry'[0]
            Eq $g.description.identifier $e.identifier $e.file
            $geoIds[$e.identifier] = $g
            $w = $g.description.texture_width; $h = $g.description.texture_height
            if ($w -le 0 -or $h -le 0) { throw "$($e.file): texture size $w x $h" }
            $names = @{}
            foreach ($b in $g.bones) {
                if ($names.ContainsKey($b.name.ToLowerInvariant())) { throw "$($e.file): duplicate bone $($b.name)" }
                $names[$b.name.ToLowerInvariant()] = $true
            }
            foreach ($b in $g.bones) {
                if ($b.parent -and -not $names.ContainsKey($b.parent.ToLowerInvariant()) -and -not ($e.external_parents -contains $b.parent)) { throw "$($e.file): bone $($b.name) has an unknown parent $($b.parent)" }
                foreach ($c in @($b.cubes)) {
                    if (-not $c) { continue }
                    foreach ($s in $c.size) { if ($s -lt 0) { throw "$($e.file): negative size in $($b.name)" } }
                    if ($c.uv -is [array]) {
                        if ($c.uv[0] -lt 0 -or $c.uv[1] -lt 0 -or $c.uv[0] -gt $w -or $c.uv[1] -gt $h) { throw "$($e.file): uv $($c.uv -join ',') outside the texture in $($b.name)" }
                    } else {
                        foreach ($f in $c.uv.PSObject.Properties) {
                            $u0 = $f.Value.uv[0]; $v0 = $f.Value.uv[1]; $u1 = $u0 + $f.Value.uv_size[0]; $v1 = $v0 + $f.Value.uv_size[1]
                            foreach ($u in $u0, $u1) { if ($u -lt -0.001 -or $u -gt $w + 0.001) { throw "$($e.file): $($f.Name) u $u outside the texture in $($b.name)" } }
                            foreach ($v in $v0, $v1) { if ($v -lt -0.001 -or $v -gt $h + 0.001) { throw "$($e.file): $($f.Name) v $v outside the texture in $($b.name)" } }
                        }
                    }
                }
            }
        }
    }
    Test 'every base animation names bones of its geometry' {
        foreach ($a in $index.animations) {
            $unitDir = "assets/gen/geo/$($a.unit)/rp/"
            $d = Read-Gen ($unitDir + $a.file)
            $anim = $d.animations.($a.identifier)
            if (-not $anim) { throw "$($a.file): no $($a.identifier)" }
            if (-not @($anim.bones.PSObject.Properties).Count) { throw "$($a.file): no bones (Bedrock rejects an empty bones object)" }
            $geoId = $a.identifier -replace '^animation\.', 'geometry.' -replace '\.base$', ''
            $g = $geoIds[$geoId]
            foreach ($b in $anim.bones.PSObject.Properties.Name) { if (-not ($g.bones.name -contains $b)) { throw "$($a.file): bone $b is not in $geoId" } }
        }
    }
    Test 'every attachable names an existing geometry and a texture' {
        foreach ($a in $index.attachables) {
            $d = Read-Gen "assets/gen/geo/$($a.unit)/rp/$($a.file)"
            $desc = $d.'minecraft:attachable'.description
            Eq $desc.identifier $a.item
            foreach ($g in $desc.geometry.PSObject.Properties.Value) {
                if (-not $geoIds.ContainsKey($g) -and $g -notlike 'geometry.humanoid.armor.*') { throw "$($a.file): unknown geometry $g" }
            }
            if (-not $desc.textures.default) { throw "$($a.file): no texture" }
        }
    }
    Test 'the generator is deterministic' {
        $again = [Gf.Models.Generator]::new((Get-GfTfJavaRoot), (Get-Content (Join-Path $PSScriptRoot 'catalog.json') -Raw))
        $again.Run($null, $null, $null)
        Eq $again.Files.Count $files.Count
        foreach ($k in $files.Keys) { if ($again.Files[$k] -cne $files[$k]) { throw "$k differs between two runs" } }
    }
    Test 'convert.ps1 -Check reproduces the committed tree' {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'convert.ps1') -Check -Quiet | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'convert.ps1 -Check failed (run pwsh tools/models/convert.ps1)' }
    }
}

Write-Host "[geo-test] $($script:pass) passed, $($script:fail.Count) failed"
if ($script:fail.Count) { $script:fail | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }; exit 1 }
exit 0
