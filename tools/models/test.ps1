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
function Convert-Layer($layer, [string]$id = 'geometry.test') {
    $opt = [Gf.Models.ConvertOptions]::new()
    $opt.Identifier = $id
    foreach ($kv in [Gf.Models.Converter]::HumanoidNames.GetEnumerator()) { $opt.Rename[$kv.Key] = $kv.Value }
    foreach ($kv in [Gf.Models.Converter]::QuadrupedNames.GetEnumerator()) { $opt.Rename[$kv.Key] = $kv.Value }
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
$pig = Convert-Layer $quad
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
'@), 'create', [System.Collections.Generic.List[object]]::new()))
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
Test 'setupAnim -> Molang: reassigned locals follow the last value, unknown ones are dropped' {
    $c = New-TestModel @'
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
'@ 'TestAnim'
    $mesh = $interp.CallStatic((New-TestModel @'
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        mesh.getRoot().addOrReplaceChild("head", CubeListBuilder.create().addBox(0, 0, 0, 1, 1, 1), PartPose.ZERO);
        return LayerDefinition.create(mesh, 16, 16);
    }
'@), 'create', [System.Collections.Generic.List[object]]::new())
    $g = Convert-Layer $mesh
    $res = [Gf.Models.AnimTranslator]::new($cp, $interp).Translate($c, $g.Geo)
    $json = [Gf.Models.Json]::Write([Gf.Models.AnimTranslator]::AnimationJson('animation.test', $res)) | ConvertFrom-Json
    $rot = $json.animations.'animation.test'.bones.head.rotation
    Eq $rot[0] 'query.life_time * 114.5916 + 57.2958' 'f = ageInTicks * 0.1 + 1 (radians -> degrees)'
    Eq $rot[1] 0 'g is unknown after the conditional reassignment'
    Eq $rot[2] 0 'division by zero is left untranslated'
}
Test 'worn extracts never reuse the wearer''s bone names' {
    $g = Geo 'building' 'naga_trophy_worn'
    Eq (($g.bones.name) -join ',') 'naga_trophy_worn,naga_trophy_worn_head'
    Eq $g.bones[0].parent 'head'
    $crown = Geo 'artifacts' 'mystic_crown'
    Eq (($crown.bones.name) -join ',') 'mystic_crown,mystic_crown_hat'
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
