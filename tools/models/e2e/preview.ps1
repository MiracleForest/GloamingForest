<#
.SYNOPSIS
  E2E helper of the model converter (unit F2): puts every converted model into a deployed mod and
  builds a preview pack that shows each one on a test actor.

.DESCRIPTION
  Runs tools/models/convert.ps1 for every partition in memory and, below -ModDir (a deployed
  GloamingForest folder, never the repository):

  - copies every partition's rp/** into resource_packs/GloamingForest_RP/ and data/** into data/
    (what the pack holds once every W1 unit regenerated its partition), so the game loads all
    geometry, base animations and attachables (content log) and the models.* selftests see them;
  - with -Preview, writes the test-only packs GloamingForest_ModelPreview_BP / _RP: one actor
    gfpreview:<geometry> per converted model (its TF texture, its base animation, scaled to about
    1.6 blocks, name tag), test items gfpreview:<item> with copies of the generated attachables
    (armor, Travellers' Gear, worn trophies, mystic crown, knightmetal shield) for armor stands,
    and functions gfpreview:shot_NN that summon one screenshot's actors in front of a fixed
    camera. The TF textures are copied from the shared checkout (CC BY-NC-SA 4.0) into the
    deployed test pack only; nothing is written to the repository.

  Writes <OutDir>/preview-shots.json (the shots and what they show) for the scenario.

.EXAMPLE
  pwsh tools/models/e2e/preview.ps1 -ModDir $env:APPDATA\...\mods\GloamingForest -Preview -OutDir tools/e2e/client/out/models
#>
param(
    [Parameter(Mandatory)][string]$ModDir,
    [switch]$Preview,
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Import-Module (Join-Path $root 'tools/models/lib/Models.psm1') -Force

if (-not (Test-Path (Join-Path $ModDir 'resource_packs/GloamingForest_RP/manifest.json'))) { throw "not a deployed GloamingForest folder: $ModDir" }
$r = Invoke-GfModelsGenerator
if ($r.Errors.Count) { throw "converter errors: $($r.Errors -join '; ')" }

# ---- every partition into the deployed mod --------------------------------------------------------
$rp = Join-Path $ModDir 'resource_packs/GloamingForest_RP'
$copied = 0
foreach ($path in $r.Files.Keys) {
    if ($path -notmatch '^assets/gen/geo/[^/]+/(rp|data)/(.+)$') { continue }
    $dest = if ($Matches[1] -eq 'rp') { Join-Path $rp $Matches[2] } else { Join-Path $ModDir "data/$($Matches[2])" }
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    [IO.File]::WriteAllText($dest, $r.Files[$path], [Text.UTF8Encoding]::new($false))
    $copied++
}
Write-Host "[geo-e2e] $copied generated file(s) put into $ModDir"
if (-not $Preview) { exit 0 }

# ---- the preview packs ---------------------------------------------------------------------------
$index = $r.Files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
$tfTextures = Join-Path (Split-Path -Parent (Get-GfTfJavaRoot)) 'resources/assets/twilightforest/textures'
$bp = Join-Path $ModDir 'behavior_packs/GloamingForest_ModelPreview_BP'
$prp = Join-Path $ModDir 'resource_packs/GloamingForest_ModelPreview_RP'
foreach ($d in $bp, $prp) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
function Write-J([string]$path, $obj) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
$stamp = [int](([DateTimeOffset]::UtcNow.ToUnixTimeSeconds() / 60) % 30000) + 2
$bpUuid = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b01'; $bpMod = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b02'
$rpUuid = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b03'; $rpMod = '6f1c1d52-7a5b-4c51-9a0e-2f4d6a3c8b04'
Write-J (Join-Path $bp 'manifest.json') ([ordered]@{ format_version = 2; header = [ordered]@{ name = 'GloamingForest model preview (test)'; description = 'F2 e2e: converted models on test actors'; uuid = $bpUuid; version = @(1, 0, $stamp); min_engine_version = @(1, 21, 0) }; modules = @([ordered]@{ type = 'data'; uuid = $bpMod; version = @(1, 0, $stamp) }) })
Write-J (Join-Path $prp 'manifest.json') ([ordered]@{ format_version = 2; header = [ordered]@{ name = 'GloamingForest model preview (test)'; description = 'F2 e2e: converted models on test actors'; uuid = $rpUuid; version = @(1, 0, $stamp); min_engine_version = @(1, 21, 0) }; modules = @([ordered]@{ type = 'resources'; uuid = $rpMod; version = @(1, 0, $stamp) }) })

$copiedTex = @{}
function Copy-Texture([string]$bedrockPath) {
    # textures/twilightforest/<tf path> -> the TF checkout; vanilla textures are the game's own
    if ($bedrockPath -notlike "$($index.texture_prefix)*") { return }
    if ($copiedTex.ContainsKey($bedrockPath)) { return }
    $rel = $bedrockPath.Substring($index.texture_prefix.Length)
    $src = Join-Path $tfTextures "$rel.png"
    if (-not (Test-Path $src)) { throw "TF texture not found: $src" }
    $dst = Join-Path $prp "$bedrockPath.png"
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item $src $dst
    $copiedTex[$bedrockPath] = $true
}

# actors: every geometry that stands on its own (entity, block entity and item models, trophy
# extracts, vanilla-model layers with their TF texture); worn / held extracts and armor go on stands
$actors = [System.Collections.Generic.List[object]]::new()
foreach ($g in $index.geometries) {
    if ($g.kind -in 'armor', 'worn', 'held') { continue }
    $tex = @($g.textures)[0]
    if (-not $tex) { $tex = 'textures/entity/steve' }
    Copy-Texture $tex
    $name = if ($g.vanilla) { $g.layer } else { $g.identifier -replace '^geometry\.twilightforest\.', '' }
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
    $actors.Add([pscustomobject]@{ kind = 'actor'; id = $id; name = $name; geometry = $g.identifier; unit = $g.unit })
}

# stands: armor sets and worn / held items (test items wear copies of the generated attachables)
$slotOf = @{ helmet = 'slot.armor.head'; chestplate = 'slot.armor.chest'; leggings = 'slot.armor.legs'; boots = 'slot.armor.feet'; head = 'slot.armor.head'; main_hand = 'slot.weapon.mainhand' }
$stands = [ordered]@{}
foreach ($a in $index.attachables) {
    $path = "assets/gen/geo/$($a.unit)/rp/$($a.file)"
    $att = $r.Files[$path] | ConvertFrom-Json -AsHashtable
    $short = $a.item.Substring($a.item.IndexOf(':') + 1)
    $id = "gfpreview:$short"
    $att['minecraft:attachable']['description']['identifier'] = $id
    foreach ($t in $att['minecraft:attachable']['description']['textures'].Values) { Copy-Texture $t }
    Write-J (Join-Path $prp "attachables/$short.json") $att
    $components = [ordered]@{ 'minecraft:max_stack_size' = 1 }
    if ($a.slot -ne 'main_hand') { $components['minecraft:wearable'] = [ordered]@{ slot = $slotOf[$a.slot]; protection = 0 } }
    Write-J (Join-Path $bp "items/$short.json") ([ordered]@{ format_version = '1.21.0'; 'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = $id; menu_category = [ordered]@{ category = 'none' } }; components = $components } })
    # one stand per armor material / per worn or held item
    $group = if ($a.slot -in 'helmet', 'chestplate', 'leggings', 'boots') {
        if ($short -like 'travellers_*') { if ($short -in 'travellers_gloves', 'travellers_wings') { 'travellers_gloves_wings' } else { 'travellers_goggles_vest_belt_boots' } }
        else { $short -replace '_(helmet|chestplate|leggings|boots)$', '' }
    } else { $short }
    if (-not $stands.Contains($group)) { $stands[$group] = [System.Collections.Generic.List[object]]::new() }
    $stands[$group].Add([pscustomobject]@{ item = $id; slot = $slotOf[$a.slot] })
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
    $shown = @()
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $x = $slots[$i]; $e = $entries[$i]
        $pos = "$x -60 0"
        $look = "$($camera.x) -59 $($camera.z)"
        if ($e.kind -eq 'actor') {
            $lines.Add("summon $($e.id) $pos facing $look minecraft:entity_spawned `"$($e.name)`"")
            $shown += $e.name
        } else {
            $lines.Add("summon armor_stand $pos facing $look minecraft:entity_spawned `"$($e.group)`"")
            foreach ($it in $e.items) { $lines.Add("replaceitem entity @e[type=armor_stand,x=$x,y=-60,z=0,r=0.5] $($it.slot) 0 $($it.item)") }
            $shown += $e.group
        }
    }
    [IO.File]::WriteAllText((Join-Path $fn "$name.mcfunction"), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    $script:shots.Add([pscustomobject]@{ name = $name; label = $label; shows = $shown })
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
Write-Host "[geo-e2e] preview: $($actors.Count) actors, $($standList.Count) stands, $($shots.Count) shots, $($copiedTex.Count) TF textures"
exit 0
