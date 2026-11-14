<#
.SYNOPSIS
  E2E helper of the model converter (unit F2): builds the test-only packs that show every converted
  model on a test actor (client scenario 'models').

.DESCRIPTION
  Below -ModDir (a deployed GloamingForest folder, never the repository), whose pack already holds
  every converted geometry, base animation and attachable (assets/gen/geo/**), writes the packs
  GloamingForest_ModelPreview_BP / _RP: one actor gfpreview:<geometry> per converted model (its upstream
  texture, its base animation, scaled to about 1.6 blocks, name tag), test items gfpreview:item_<item>
  with copies of the generated attachables (armor, Travellers' Gear, worn trophies, mystic crown,
  knightmetal shield) for armor stands, and functions gfpreview:shot_NN that summon one screenshot's
  actors in front of a fixed camera. The upstream textures are copied from the shared checkout
  (CC BY-NC-SA 4.0) into the deployed test pack only; nothing is written to the repository.

  Writes <OutDir>/preview-shots.json (the shots and what they show) for the scenario.

.EXAMPLE
  pwsh tools/models/e2e/preview.ps1 -ModDir $env:APPDATA\...\mods\GloamingForest -OutDir tools/e2e/client/out/models
#>
param(
    [Parameter(Mandatory)][string]$ModDir,
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Import-Module (Join-Path $root 'tools/models/lib/Models.psm1') -Force

if (-not (Test-Path (Join-Path $ModDir 'resource_packs/GloamingForest_RP/manifest.json'))) { throw "not a deployed GloamingForest folder: $ModDir" }
$r = Invoke-GfModelsGenerator
if ($r.Errors.Count) { throw "converter errors: $($r.Errors -join '; ')" }

# ---- the preview packs ---------------------------------------------------------------------------
$index = $r.Files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
$gloamingTextures = Join-Path (Split-Path -Parent (Get-GfGloamingJavaRoot)) 'resources/assets/gloamingforest/textures'
$bp = Join-Path $ModDir 'behavior_packs/GloamingForest_ModelPreview_BP'
$prp = Join-Path $ModDir 'resource_packs/GloamingForest_ModelPreview_RP'
foreach ($d in $bp, $prp) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
function Write-J([string]$path, $obj) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
# a new version every run (a cached copy of an older preview pack must never be used)
$stamp = [int]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() % 30000) + 2
$bpUuid = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b01'; $bpMod = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b02'
$rpUuid = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b03'; $rpMod = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b04'
Write-J (Join-Path $bp 'manifest.json') ([ordered]@{ format_version = 2; header = [ordered]@{ name = 'GloamingForest model preview (test)'; description = 'F2 e2e: converted models on test actors'; uuid = $bpUuid; version = @(1, 0, $stamp); min_engine_version = @(1, 21, 0) }; modules = @([ordered]@{ type = 'data'; uuid = $bpMod; version = @(1, 0, $stamp) }) })
Write-J (Join-Path $prp 'manifest.json') ([ordered]@{ format_version = 2; header = [ordered]@{ name = 'GloamingForest model preview (test)'; description = 'F2 e2e: converted models on test actors'; uuid = $rpUuid; version = @(1, 0, $stamp); min_engine_version = @(1, 21, 0) }; modules = @([ordered]@{ type = 'resources'; uuid = $rpMod; version = @(1, 0, $stamp) }) })

$copiedTex = @{}
function Copy-Texture([string]$bedrockPath) {
    # textures/gloamingforest/<upsrc path> -> the upstream checkout; vanilla textures are the game's own
    if ($bedrockPath -notlike "$($index.texture_prefix)*") { return }
    if ($copiedTex.ContainsKey($bedrockPath)) { return }
    $rel = $bedrockPath.Substring($index.texture_prefix.Length)
    $src = Join-Path $gloamingTextures "$rel.png"
    if (-not (Test-Path $src)) { throw "upstream texture not found: $src" }
    $dst = Join-Path $prp "$bedrockPath.png"
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item $src $dst
    $copiedTex[$bedrockPath] = $true
}

# actors: every geometry that stands on its own (entity, block entity and item models, trophy
# extracts, vanilla-model layers with their upstream texture); worn / held extracts and armor go on stands
$actors = [System.Collections.Generic.List[object]]::new()
foreach ($g in $index.geometries) {
    if ($g.kind -in 'armor', 'worn', 'held') { continue }
    $tex = @($g.textures)[0]
    # an armor layer is drawn with the wearer's armor texture: the catalog's preview texture (vanilla iron)
    if (-not $tex -and $g.preview -and $g.preview.texture) { $tex = $g.preview.texture }
    if (-not $tex) { $tex = 'textures/entity/steve' }
    Copy-Texture $tex
    $name = if ($g.vanilla) { $g.layer } else { $g.identifier -replace '^geometry\.gloamingforest\.', '' }
    $id = 'gfpreview:' + ($name -replace '[^a-z0-9_]', '_')
    # scale to about 1.6 blocks (visible bounds carry a margin of one block)
    $scale = 1.0
    if ($g.visible_bounds) {
        $w = [Math]::Max(0.25, [double]$g.visible_bounds[0] - 1); $h = [Math]::Max(0.25, [double]$g.visible_bounds[1] - 1)
        $scale = [Math]::Round([Math]::Min(3.0, 1.6 / [Math]::Max($w, $h)), 3)
    }
    $desc = [ordered]@{
        identifier = $id
        materials = [ordered]@{ default = 'entity_alphatest' }
        textures = [ordered]@{ default = $tex }
        geometry = [ordered]@{ default = $g.identifier }
        scripts = [ordered]@{ scale = "$scale" }
        render_controllers = @('controller.render.default')
    }
    if ($g.animation) {
        $desc.animations = [ordered]@{ base = $g.animation }
        $desc.scripts.animate = @('base')
    }
    Write-J (Join-Path $prp "entity/$($id.Substring(10)).entity.json") ([ordered]@{ format_version = '1.10.0'; 'minecraft:client_entity' = [ordered]@{ description = $desc } })
    Write-J (Join-Path $bp "entities/$($id.Substring(10)).json") ([ordered]@{
        format_version = '1.20.0'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = $id; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
            components = [ordered]@{
                'minecraft:type_family' = [ordered]@{ family = @('gfpreview') }
                'minecraft:physics' = [ordered]@{ has_gravity = $false; has_collision = $false }
                'minecraft:collision_box' = [ordered]@{ width = 0.6; height = 0.6 }
                'minecraft:pushable' = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:nameable' = [ordered]@{ always_show = $true; allow_name_tag_renaming = $false }
            }
        }
    })
    $actors.Add([pscustomobject]@{ kind = 'actor'; id = $id; name = $name; geometry = $g.identifier; unit = $g.unit; frame = $g.frame })
}

# ---- the x-sign check: a Java part.x assignment through AnimTranslator against the geometry's x ----
# A test-only model (written here, LGPL-free): a white cube at the origin, a red one at x 8..12 and a
# blue one above the origin that setupAnim moves by moved.x = 10. Blue lands on red when the
# animation's position x runs along the geometry's x (docs/spec/models.md, setupAnim -> Molang).
$cp = [Gf.Models.ClassPath]::new((Get-GfGloamingJavaRoot)); $interp = [Gf.Models.Interp]::new($cp)
$xsignSrc = @'
package gfpreview; public class XSign {
    private final ModelPart moved;
    public XSign(ModelPart root) { this.moved = root.getChild("moved"); }
    public static LayerDefinition create() {
        MeshDefinition mesh = new MeshDefinition();
        PartDefinition root = mesh.getRoot();
        root.addOrReplaceChild("origin", CubeListBuilder.create().texOffs(0, 0).addBox(-2.0F, -4.0F, -2.0F, 4.0F, 4.0F, 4.0F), PartPose.offset(0.0F, 24.0F, 0.0F));
        root.addOrReplaceChild("plus_x", CubeListBuilder.create().texOffs(0, 8).addBox(8.0F, -4.0F, -2.0F, 4.0F, 4.0F, 4.0F), PartPose.offset(0.0F, 24.0F, 0.0F));
        root.addOrReplaceChild("moved", CubeListBuilder.create().texOffs(0, 16).addBox(-2.0F, -9.0F, -2.0F, 4.0F, 4.0F, 4.0F), PartPose.offset(0.0F, 24.0F, 0.0F));
        return LayerDefinition.create(mesh, 16, 32);
    }
    public void setupAnim(Entity entity, float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch) {
        this.moved.x = 10.0F;
    }
}
'@
$xsignCls = ([Gf.Models.Parser]::ParseUnit($xsignSrc, 'XSign.java')).Types[0]
$xsignMesh = $interp.CallStatic($xsignCls, 'create', [System.Collections.Generic.List[object]]::new())
$xsignOpt = [Gf.Models.ConvertOptions]::new(); $xsignOpt.Identifier = 'geometry.gfpreview.xsign'
$xsignGeo = [Gf.Models.Converter]::Convert($xsignMesh, $xsignOpt, $null, 1.0)
$xsignAnim = [Gf.Models.AnimTranslator]::new($cp, $interp).Translate($xsignCls, $xsignGeo)
New-Item -ItemType Directory -Force (Join-Path $prp 'models/entity'), (Join-Path $prp 'animations'), (Join-Path $prp 'textures/gfpreview') | Out-Null
[IO.File]::WriteAllText((Join-Path $prp 'models/entity/gfpreview_xsign.geo.json'), [Gf.Models.Json]::Write($xsignGeo.ToJson()), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $prp 'animations/gfpreview_xsign.animation.json'), [Gf.Models.Json]::Write([Gf.Models.AnimTranslator]::AnimationJson('animation.gfpreview.xsign', $xsignAnim)), [Text.UTF8Encoding]::new($false))
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::new(16, 32)
for ($y = 0; $y -lt 32; $y++) { for ($x = 0; $x -lt 16; $x++) {
    $c = if ($y -lt 8) { [System.Drawing.Color]::White } elseif ($y -lt 16) { [System.Drawing.Color]::FromArgb(255, 220, 30, 30) } else { [System.Drawing.Color]::FromArgb(255, 30, 60, 230) }
    $bmp.SetPixel($x, $y, $c) } }
$bmp.Save((Join-Path $prp 'textures/gfpreview/xsign.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
$xsign = [pscustomobject]@{ kind = 'actor'; id = 'gfpreview:xsign'; name = 'x-sign: blue above red'; geometry = 'geometry.gfpreview.xsign'; unit = 'models' }
Write-J (Join-Path $prp 'entity/xsign.entity.json') ([ordered]@{ format_version = '1.10.0'; 'minecraft:client_entity' = [ordered]@{ description = [ordered]@{
    identifier = $xsign.id; materials = [ordered]@{ default = 'entity_alphatest' }; textures = [ordered]@{ default = 'textures/gfpreview/xsign' }
    geometry = [ordered]@{ default = 'geometry.gfpreview.xsign' }; animations = [ordered]@{ base = 'animation.gfpreview.xsign' }
    scripts = [ordered]@{ scale = '2'; animate = @('base') }; render_controllers = @('controller.render.default') } } })
Write-J (Join-Path $bp 'entities/xsign.json') ([ordered]@{ format_version = '1.20.0'; 'minecraft:entity' = [ordered]@{
    description = [ordered]@{ identifier = $xsign.id; is_spawnable = $false; is_summonable = $true; is_experimental = $false }
    components = [ordered]@{ 'minecraft:type_family' = [ordered]@{ family = @('gfpreview') }; 'minecraft:physics' = [ordered]@{ has_gravity = $false; has_collision = $false }
        'minecraft:collision_box' = [ordered]@{ width = 0.6; height = 0.6 }; 'minecraft:pushable' = [ordered]@{ is_pushable = $false; is_pushable_by_piston = $false }
        'minecraft:nameable' = [ordered]@{ always_show = $true; allow_name_tag_renaming = $false } } } })
$actors.Insert(0, $xsign)

# ---- content-log canaries: two broken files the client must report (the scenario requires their
# lines, which proves the content log is captured, and fails on every other error or warning) ----
[IO.File]::WriteAllText((Join-Path $prp 'models/entity/gfpreview_canary.geo.json'), "{ `"format_version`": `"1.12.0`", `"minecraft:geometry`": [ { `"description`": { `"identifier`": `"geometry.gfpreview_canary`"", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $prp 'animations/gfpreview_canary.animation.json'), '{ "format_version": "1.8.0", "animations": { "animation.gfpreview_canary": { "loop": true, "bones": { "gfpreview_canary": { "rotation": ["math.cos(query.life_time *", 0, 0] } } } } }', [Text.UTF8Encoding]::new($false))

# stands: armor sets and worn / held items (test items wear copies of the generated attachables);
# an item drawn in several slots (trophies, the mystic crown: head + both hands; the shield: both
# hands) is put in every one of them on its own stand
$slotOf = @{ helmet = 'slot.armor.head'; chestplate = 'slot.armor.chest'; leggings = 'slot.armor.legs'; boots = 'slot.armor.feet'; head = 'slot.armor.head'; main_hand = 'slot.weapon.mainhand'; off_hand = 'slot.weapon.offhand' }
$stands = [ordered]@{}
foreach ($a in $index.attachables) {
    $path = "assets/gen/geo/$($a.unit)/rp/$($a.file)"
    $att = $r.Files[$path] | ConvertFrom-Json -AsHashtable
    $short = $a.item.Substring($a.item.IndexOf(':') + 1)
    # "item_": an attachable and a client entity with one identifier replace each other (naga_trophy is both)
    $id = "gfpreview:item_$short"
    $att['minecraft:attachable']['description']['identifier'] = $id
    foreach ($t in $att['minecraft:attachable']['description']['textures'].Values) { Copy-Texture $t }
    Write-J (Join-Path $prp "attachables/item_$short.json") $att
    $slots = @(if ($a.slots) { $a.slots } else { $a.slot })
    # an item without an icon is silently not registered (every command naming it fails to parse):
    # borrow a vanilla icon, as the Levitate items do with their own (format 1.21.70)
    $components = [ordered]@{ 'minecraft:icon' = 'apple'; 'minecraft:max_stack_size' = 1 }
    $worn = @($slots | Where-Object { $_ -notin 'main_hand', 'off_hand' })
    if ($worn) { $components['minecraft:wearable'] = [ordered]@{ slot = $slotOf[$worn[0]]; protection = 0 } }
    if ($slots -contains 'off_hand') { $components['minecraft:allow_off_hand'] = $true }
    Write-J (Join-Path $bp "items/item_$short.json") ([ordered]@{ format_version = '1.21.70'; 'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = $id; menu_category = [ordered]@{ category = 'equipment' } }; components = $components } })
    # Bedrock draws a minecraft:wearable item in the hands as its icon, not through the attachable:
    # the hand slots of an item that is also worn get a copy without wearable (same attachable)
    $handId = $id
    if ($worn -and ($slots -contains 'main_hand' -or $slots -contains 'off_hand')) {
        $handId = "$($id)_in_hand"
        $att['minecraft:attachable']['description']['identifier'] = $handId
        Write-J (Join-Path $prp "attachables/item_$($short)_in_hand.json") $att
        $hand = [ordered]@{ 'minecraft:icon' = 'apple'; 'minecraft:max_stack_size' = 1; 'minecraft:allow_off_hand' = $true }
        Write-J (Join-Path $bp "items/item_$($short)_in_hand.json") ([ordered]@{ format_version = '1.21.70'; 'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = $handId; menu_category = [ordered]@{ category = 'equipment' } }; components = $hand } })
    }
    # one stand per armor material / per worn or held item
    $group = if ($a.slot -in 'helmet', 'chestplate', 'leggings', 'boots') {
        if ($short -like 'travellers_*') { if ($short -in 'travellers_gloves', 'travellers_wings') { 'travellers_gloves_wings' } else { 'travellers_goggles_vest_belt_boots' } }
        else { $short -replace '_(helmet|chestplate|leggings|boots)$', '' }
    } else { $short }
    if (-not $stands.Contains($group)) { $stands[$group] = [System.Collections.Generic.List[object]]::new() }
    foreach ($s in $slots) { $stands[$group].Add([pscustomobject]@{ item = $(if ($s -in 'main_hand', 'off_hand') { $handId } else { $id }); slot = $slotOf[$s] }) }
}

# shots: actors four at a time on x = -3, -1, 1, 3 (z = 0, facing the camera at z = 6), then stands
$camera = @{ x = 0; y = -58.4; z = 6.5 }
$slots = @(-3.3, -1.1, 1.1, 3.3)
$shots = [System.Collections.Generic.List[object]]::new()
$fn = Join-Path $bp 'functions/gfpreview'
$n = 0
$clear = @('kill @e[family=gfpreview]', 'tp @e[type=armor_stand] 0 -200 0', 'kill @e[type=item]')
function Add-Shot($entries, [string]$label) {
    $script:n++
    $name = 'shot_{0:D2}' -f $script:n
    $lines = [System.Collections.Generic.List[string]]::new()
    $clear | ForEach-Object { $lines.Add($_) }
    # A function with one line that does not parse (an unknown item) is dropped whole: the commands
    # naming preview items run after the function instead, where a failure is reported on its own.
    $after = [System.Collections.Generic.List[string]]::new()
    $shown = @()
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $x = $slots[$i]; $e = $entries[$i]
        $pos = "$x -60 0"
        $look = "$($camera.x) -59 $($camera.z)"
        # a trophy extract (frame 'world') is the block's geometry at TrophyRenderer angle 0 (floor
        # rotation 0 and a wall trophy facing north alike): an actor facing the camera shows its back, so
        # it looks at the camera's mirror point behind it (turned 180 degrees) and shows the trophy's face
        if ($e.kind -eq 'actor' -and $e.frame -eq 'world') { $look = "$(2 * $x - $camera.x) -59 $(-$camera.z)" }
        if ($e.kind -eq 'actor') {
            $lines.Add("summon $($e.id) $pos facing $look minecraft:entity_spawned `"$($e.name)`"")
            $shown += $e.name
        } else {
            # no spawn event: armor stands have no minecraft:entity_spawned, and one bad line drops the
            # whole function; yaw 0 faces the camera (south), set again by tp (a summoned stand takes
            # the rotation of whoever ran the command)
            $lines.Add("summon armor_stand $pos 0 0")
            $lines.Add("tp @e[type=armor_stand,x=$x,y=-60,z=0,r=0.5] $pos 0 0")
            foreach ($it in $e.items) { $after.Add("replaceitem entity @e[type=armor_stand,x=$x,y=-60,z=0,r=0.5] $($it.slot) 0 $($it.item)") }
            $shown += $e.group
        }
    }
    [IO.File]::WriteAllText((Join-Path $fn "$name.mcfunction"), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $script:shots.Add([pscustomobject]@{ name = $name; label = $label; shows = $shown; commands = @($after) })
}
New-Item -ItemType Directory -Force $fn | Out-Null
[IO.File]::WriteAllText((Join-Path $fn 'clear.mcfunction'), ($clear -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
for ($i = 0; $i -lt $actors.Count; $i += 4) {
    $batch = @($actors | Select-Object -Skip $i -First 4)
    Add-Shot $batch ($batch.name -join ', ')
}
$standList = @($stands.Keys | ForEach-Object { [pscustomobject]@{ kind = 'stand'; group = $_; items = @($stands[$_]) } })
for ($i = 0; $i -lt $standList.Count; $i += 4) {
    $batch = @($standList | Select-Object -Skip $i -First 4)
    Add-Shot $batch ($batch.group -join ', ')
}
$plan = [ordered]@{ camera = $camera; actors = $actors.Count; stands = $standList.Count; shots = $shots }
if ($OutDir) { New-Item -ItemType Directory -Force $OutDir | Out-Null; Write-J (Join-Path $OutDir 'preview-shots.json') $plan }
Write-Host "[geo-e2e] preview: $($actors.Count) actors, $($standList.Count) stands, $($shots.Count) shots, $($copiedTex.Count) upstream textures"
exit 0
