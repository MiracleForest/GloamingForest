<#
.SYNOPSIS
  U19 (questcamps): builds the Questing Ram's client assets from F1's / F2's generated files.

.DESCRIPTION
  TF's QuestRamModel renders each of the 16 wool segments tinted with Sheep#getColor of its dye and
  shows only the colours the ram collected, then a glowing-lines layer scaled 1.025. Bedrock has no
  per-bone tint, so this script writes:
    - assets/pack/rp/textures/entity/questcamps/tf-derived/questram_tinted.png: TF's questram.png
      (CC BY-NC-SA 4.0, R3) with 16 tinted copies of the wool segment's UV box appended below it
      (128 x 256), and its SOURCES.json entry;
    - assets/pack/rp/models/entity/questcamps/quest_ram_tinted.geo.json: F2's geometry with each
      segment pointing at its tinted copy;
    - assets/pack/rp/models/entity/questcamps/quest_ram_lines.geo.json: F2's geometry scaled 1.025
      about the model origin (24 px above the feet), for the lines layer;
    - assets/pack/rp/animations/questcamps/quest_ram.pose.animation.json: QuestRamModel
      #prepareMobModel (the body stretched by the number of colours, the segments lined up in
      colorOrder) from the packed entity property twilightforest:color_flags.
  Run it again when F1 / F2 regenerate their files.
#>
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
Add-Type -AssemblyName System.Drawing

$colorOrder = @(0, 8, 7, 15, 14, 1, 4, 5, 13, 3, 9, 11, 10, 2, 6, 12)
$diffuse = @(0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52, 0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21)
function Get-SheepColor([int]$i) {
    if ($i -eq 0) { return 0xE6E6E6 }
    $c = $diffuse[$i]
    $r = [Math]::Floor((($c -shr 16) -band 0xFF) * 0.75); $g = [Math]::Floor((($c -shr 8) -band 0xFF) * 0.75); $b = [Math]::Floor(($c -band 0xFF) * 0.75)
    return ([int]$r -shl 16) -bor ([int]$g -shl 8) -bor [int]$b
}

# ---- the tinted texture -------------------------------------------------------------------------------------
$srcRel = 'assets/gen/import/questcamps/rp/textures/twilightforest/entity/questram.png'
$src = [System.Drawing.Bitmap]::new((Join-Path $root $srcRel))
$out = [System.Drawing.Bitmap]::new(128, 256, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
for ($x = 0; $x -lt 128; $x++) { for ($y = 0; $y -lt 128; $y++) { $out.SetPixel($x, $y, $src.GetPixel($x, $y)) } }
$uv = @{}
for ($i = 0; $i -lt 16; $i++) {
    $col = $i % 3; $row = [Math]::Floor($i / 3)
    $ox = $col * 42; $oy = 128 + $row * 17
    $uv[$i] = @($ox, $oy)
    $tint = Get-SheepColor $i
    $tr = ($tint -shr 16) -band 0xFF; $tg = ($tint -shr 8) -band 0xFF; $tb = $tint -band 0xFF
    for ($x = 0; $x -lt 40; $x++) {
        for ($y = 0; $y -lt 17; $y++) {
            $p = $src.GetPixel($x, 104 + $y)
            $n = [System.Drawing.Color]::FromArgb($p.A, [int][Math]::Floor($p.R * $tr / 255), [int][Math]::Floor($p.G * $tg / 255), [int][Math]::Floor($p.B * $tb / 255))
            $out.SetPixel($ox + $x, $oy + $y, $n)
        }
    }
}
$texDir = Join-Path $root 'assets/pack/rp/textures/entity/questcamps/tf-derived'
New-Item -ItemType Directory -Force $texDir | Out-Null
$out.Save((Join-Path $texDir 'questram_tinted.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$src.Dispose(); $out.Dispose()
$sources = [ordered]@{ 'questram_tinted.png' = 'src/main/resources/assets/twilightforest/textures/entity/questram.png' }
[IO.File]::WriteAllText((Join-Path $texDir 'SOURCES.json'), (($sources | ConvertTo-Json) + "`n"), [Text.UTF8Encoding]::new($false))

# ---- the geometries -------------------------------------------------------------------------------------------
$geoSrc = Get-Content (Join-Path $root 'assets/gen/geo/questcamps/rp/models/entity/quest_ram.geo.json') -Raw | ConvertFrom-Json -AsHashtable
$modelDir = Join-Path $root 'assets/pack/rp/models/entity/questcamps'
New-Item -ItemType Directory -Force $modelDir | Out-Null
function Save-Json($obj, [string]$path) { [IO.File]::WriteAllText($path, (($obj | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false)) }

$tinted = $geoSrc | ConvertTo-Json -Depth 30 | ConvertFrom-Json -AsHashtable
$g = $tinted['minecraft:geometry'][0]
$g.description.identifier = 'geometry.twilightforest.quest_ram_tinted'
$g.description.texture_height = 256
foreach ($b in $g.bones) {
    if ($b.name -match '^segment(\d+)$') { foreach ($c in $b.cubes) { $c.uv = @($uv[[int]$Matches[1]][0], $uv[[int]$Matches[1]][1]) } }
}
Save-Json $tinted (Join-Path $modelDir 'quest_ram_tinted.geo.json')

$lines = $geoSrc | ConvertTo-Json -Depth 30 | ConvertFrom-Json -AsHashtable
$l = $lines['minecraft:geometry'][0]
$l.description.identifier = 'geometry.twilightforest.quest_ram_lines'
$k = 1.025
function Scale-Point($p) { return @([Math]::Round($p[0] * $k, 4), [Math]::Round(24 + ($p[1] - 24) * $k, 4), [Math]::Round($p[2] * $k, 4)) }
foreach ($b in $l.bones) {
    if ($b.ContainsKey('pivot')) { $b.pivot = Scale-Point $b.pivot }
    foreach ($c in @($b['cubes'] | Where-Object { $_ })) {
        $c.origin = Scale-Point $c.origin
        $c.size = @([Math]::Round($c.size[0] * $k, 4), [Math]::Round($c.size[1] * $k, 4), [Math]::Round($c.size[2] * $k, 4))
        if ($c.ContainsKey('pivot')) { $c.pivot = Scale-Point $c.pivot }
    }
}
Save-Json $lines (Join-Path $modelDir 'quest_ram_lines.geo.json')

# ---- the pose animation (QuestRamModel#prepareMobModel) ----------------------------------------------------------
$animDir = Join-Path $root 'assets/pack/rp/animations/questcamps'
New-Item -ItemType Directory -Force $animDir | Out-Null
$bones = [ordered]@{
    head            = [ordered]@{ position = @(0, 0, '-v.qr_count') }
    neck            = [ordered]@{ position = @(0, 0, '-v.qr_count') }
    front_torso     = [ordered]@{ position = @(0, 0, '-v.qr_count') }
    back_torso      = [ordered]@{ position = @(0, 0, 'v.qr_count') }
    left_back_leg   = [ordered]@{ position = @(0, 0, 'v.qr_count') }
    right_back_leg  = [ordered]@{ position = @(0, 0, 'v.qr_count') }
    left_front_leg  = [ordered]@{ position = @(0, 0, '-0.5 - v.qr_count') }
    right_front_leg = [ordered]@{ position = @(0, 0, '-0.5 - v.qr_count') }
}
for ($k2 = 0; $k2 -lt 16; $k2++) {
    $c = $colorOrder[$k2]
    $before = @($colorOrder[0..($k2 - 1)] | Where-Object { $k2 -gt 0 } | ForEach-Object { "v.qr_b$_" })
    $prefix = if ($before.Count) { '(' + ($before -join ' + ') + ')' } else { '0' }
    $bones["segment$c"] = [ordered]@{ position = @(0, 0, "2 * $prefix - v.qr_count") }
}
$anim = [ordered]@{
    format_version = '1.8.0'
    animations     = [ordered]@{
        'animation.twilightforest.quest_ram.pose' = [ordered]@{ loop = $true; bones = $bones }
    }
}
Save-Json $anim (Join-Path $animDir 'quest_ram.pose.animation.json')

# the pre-animation script of the client entity: the flag bits and their count
$script = [System.Collections.Generic.List[string]]::new()
$script.Add("v.qr_flags = query.property('twilightforest:color_flags');")
for ($i = 0; $i -lt 16; $i++) { $script.Add("v.qr_b$i = math.mod(math.floor(v.qr_flags / $([Math]::Pow(2, $i))), 2);") }
$script.Add('v.qr_count = ' + ((0..15 | ForEach-Object { "v.qr_b$_" }) -join ' + ') + ';')
$visibility = [ordered]@{ '*' = $true }
for ($i = 0; $i -lt 16; $i++) { $visibility["segment$i"] = "v.qr_b$i == 1" }
$entity = [ordered]@{
    format_version            = '1.10.0'
    'minecraft:client_entity' = [ordered]@{
        description = [ordered]@{
            identifier         = 'twilightforest:quest_ram'
            materials          = [ordered]@{ default = 'entity_alphatest'; lines = 'entity_alphablend' }
            textures           = [ordered]@{ default = 'textures/entity/questcamps/tf-derived/questram_tinted'; lines = 'textures/twilightforest/entity/questram_lines' }
            geometry           = [ordered]@{ default = 'geometry.twilightforest.quest_ram_tinted'; lines = 'geometry.twilightforest.quest_ram_lines' }
            render_controllers = @('controller.render.twilightforest.quest_ram', 'controller.render.twilightforest.quest_ram_lines')
            animations         = [ordered]@{ base = 'animation.twilightforest.quest_ram.base'; pose = 'animation.twilightforest.quest_ram.pose' }
            scripts            = [ordered]@{ pre_animation = $script.ToArray(); animate = @('base', 'pose') }
            spawn_egg          = [ordered]@{ base_color = '#fefeee'; overlay_color = '#33aadd' }
        }
    }
}
$entityDir = Join-Path $root 'assets/pack/rp/entity/questcamps'
New-Item -ItemType Directory -Force $entityDir | Out-Null
Save-Json $entity (Join-Path $entityDir 'quest_ram.entity.json')

$parts = @(@{ '*' = $true }) + @(0..15 | ForEach-Object { @{ "segment$_" = "v.qr_b$_ == 1" } })
$controllers = [ordered]@{
    format_version     = '1.8.0'
    render_controllers = [ordered]@{
        'controller.render.twilightforest.quest_ram'       = [ordered]@{
            geometry        = 'Geometry.default'
            materials       = @(@{ '*' = 'Material.default' })
            textures        = @('Texture.default')
            part_visibility = $parts
        }
        'controller.render.twilightforest.quest_ram_lines' = [ordered]@{
            geometry        = 'Geometry.lines'
            materials       = @(@{ '*' = 'Material.lines' })
            textures        = @('Texture.lines')
            ignore_lighting = $true
            part_visibility = $parts
        }
    }
}
$rcDir = Join-Path $root 'assets/pack/rp/render_controllers/questcamps'
New-Item -ItemType Directory -Force $rcDir | Out-Null
Save-Json $controllers (Join-Path $rcDir 'quest_ram.render_controllers.json')
Write-Host '[questcamps] ram assets written'
