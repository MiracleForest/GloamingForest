# U19 (questcamps): shared helpers of the quest grove, camp and template feature designs (original
# GloamingForest templates, rule R11/R19; TF's .nbt files are never read). Dot-sourced by the
# *.gfs.ps1 scripts of this folder.
#
# Namespaces: the camp's templates, pools and connector names are TF's own twilightforest:camp/...
# ids (R1): each template stands in for TF's template definition of the same id, whose pools
# (twilightforest:camp/<pool>) assemble knows from the imported definitions.
#
# Camp jigsaw convention (docs/units/questcamps.md):
#   - every template's layer 0 is the "ground air" level (the first layer above the terrain), except
#     the paths, whose layer 0 is under the surface, layer 1 the path surface and layer 2 the ground
#     air level (the gravity processor puts layer 1 on the terrain's top block);
#   - connectors sit on the template's outline at the ground air level, their front pointing out of
#     the template; a child's seed connector is named after the pool family it fills:
#       twilightforest:camp/structure_start  the start's seed (front down, layer 0 center)
#       twilightforest:camp/tent             a tent's entrance
#       twilightforest:camp/path             a path's (or main path's) entry
#       twilightforest:camp/deco             a deco's (or rack's) front
#     and every parent connector targets one of these names.

$script:QcName = @{
    start = 'twilightforest:camp/structure_start'
    tent  = 'twilightforest:camp/tent'
    path  = 'twilightforest:camp/path'
    deco  = 'twilightforest:camp/deco'
}
$script:QcPool = @{
    tent     = 'twilightforest:camp/tent'
    main     = 'twilightforest:camp/main_path'
    path     = 'twilightforest:camp/path'
    rackPath = 'twilightforest:camp/rack_path'
    deco     = 'twilightforest:camp/deco'
}

# A connector of a parent leading into pool `$Kind` (tent, main, path, rackPath, deco).
function Add-QcExit($t, [int]$x, [int]$y, [int]$z, [string]$front, [string]$kind) {
    $target = switch ($kind) {
        'tent' { $script:QcName.tent }
        'deco' { $script:QcName.deco }
        default { $script:QcName.path }
    }
    Add-GftJigsaw $t $x $y $z -Front $front -Pool $script:QcPool[$kind] -Name 'twilightforest:camp/exit' -Target $target
}

# The seed connector of a child template (where its parent attaches).
function Add-QcSeed($t, [int]$x, [int]$y, [int]$z, [string]$front, [string]$name) {
    Add-GftJigsaw $t $x $y $z -Front $front -Pool 'minecraft:empty' -Name $name -Target ''
}

# Fills the box with air (clears vegetation inside a template).
function Clear-QcBox($t, [int]$x1, [int]$y1, [int]$z1, [int]$x2, [int]$y2, [int]$z2) {
    Set-GftBox $t $x1 $y1 $z1 $x2 $y2 $z2 'minecraft:air'
}

# A text layer with a key; '.' air, ' ' unchanged (structure void).
function Set-QcRows($t, [int]$y, [hashtable]$key, [string[]]$rows) {
    $k = @{ '.' = 'minecraft:air' }
    foreach ($e in $key.GetEnumerator()) { $k[$e.Key] = $e.Value }
    Set-GftLayer $t -Y $y -Key $k -Rows $rows
}

function Get-QcState([string]$name, [hashtable]$props) { return @{ Name = $name; Properties = $props } }

# A data marker (TF STRUCTURE_BLOCK DATA) on an air cell.
function Add-QcData($t, [int]$x, [int]$y, [int]$z, [string]$label) {
    Set-GftBlock $t $x $y $z 'minecraft:air'
    Add-GftDataMarker $t $x $y $z $label
}
