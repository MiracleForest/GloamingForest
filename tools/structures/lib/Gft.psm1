# GloamingForest template DSL (contract "template", owner F7).
#
# TF's .nbt structure templates are All Rights Reserved (rule R10/R19): every template of
# GloamingForest is an original design written with this DSL in tools/structures/<unit>/**/*.gfs.ps1
# and built by tools/structures/build.ps1 into assets/structures/<unit>/**/*.gft.json.
#
# The .gft.json format (format "gloamingforest:template", format_version 1):
#   id        "twilightforest:lich_tower/root/foyer" (the TF template it stands in for)
#   size      [x, y, z]
#   palette   [{"Name": java block id, "Properties": {java property: value}}, ...]
#             "minecraft:structure_void" cells are not placed (they keep what is in the world)
#   blocks    run-length [[palette index, count], ...] over y, then z, then x (x fastest)
#   markers   [{"pos": [x, y, z], "marker": name, "data": {...}}]
#               "data" with data.metadata: a TF data marker (handled by the piece / its
#               template_marker_handler_list); any other name: block entity data of the block at
#               pos ("loot" {table}, "spawner" {entity, Delay, MinSpawnDelay, ...}, "sign" {lines})
#   jigsaw    [{"pos": [x, y, z], "front": dir, "top": dir, "pool", "name", "target", "priority"}]
#   entities  [{"pos": [x, y, z], "id": entity id, "data": {...}}]
#
# A script:
#   $t = New-Gft -Id 'twilightforest:camp/tent/solo_tent' -Size 7, 5, 7
#   Set-GftBox $t 0 0 0 6 0 6 'minecraft:oak_planks'
#   Set-GftLayer $t -Y 1 -Key @{ '#' = 'minecraft:light_gray_wool'; '.' = 'minecraft:air' } -Rows @(
#       '#######',
#       '#.....#' ...)
#   Set-GftBlock $t 3 1 0 'minecraft:oak_stairs' @{ facing = 'south'; half = 'bottom' }
#   Add-GftMarker $t 3 1 3 'loot' @{ table = 'twilightforest:camp_tent' }
#   Add-GftDataMarker $t 1 1 1 'tent_pot@north'
#   Add-GftJigsaw $t 3 0 6 -Front south -Pool 'twilightforest:camp/path' -Name 'twilightforest:camp/path' -Target 'twilightforest:camp/path'
#   Save-Gft $t            # hands the template to build.ps1 (once per template)

Set-StrictMode -Version Latest

$script:Saved = [System.Collections.Generic.List[object]]::new()

function New-Gft {
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][int[]]$Size,
        # what unset cells hold: structure_void (default: keep the world) or air
        [ValidateSet('structure_void', 'air')][string]$Fill = 'structure_void',
        # output path below assets/structures (default: the script's path below tools/structures)
        [string]$Out
    )
    if ($Size.Count -ne 3 -or ($Size | Where-Object { $_ -le 0 })) { throw "New-Gft ${Id}: size must be three positive integers" }
    if ($Id -notmatch '^[a-z0-9_.-]+:[a-z0-9_./-]+$') { throw "New-Gft: bad id '$Id'" }
    $fillState = @{ Name = "minecraft:$Fill" }
    $t = [pscustomobject]@{
        Id       = $Id
        Size     = $Size
        Palette  = [System.Collections.Generic.List[object]]::new()
        Keys     = @{}
        Cells    = [int[]]::new($Size[0] * $Size[1] * $Size[2])
        Markers  = [System.Collections.Generic.List[object]]::new()
        Jigsaws  = [System.Collections.Generic.List[object]]::new()
        Entities = [System.Collections.Generic.List[object]]::new()
        Out      = $Out
    }
    [void](Get-GftPaletteIndex $t $fillState)
    return $t
}

function ConvertTo-GftState([string]$Block, $Properties) {
    $state = [ordered]@{ Name = $Block }
    if ($Properties -and $Properties.Count) {
        $props = [ordered]@{}
        foreach ($k in ($Properties.Keys | Sort-Object)) {
            $v = $Properties[$k]
            $props[$k] = if ($v -is [bool]) { $v.ToString().ToLowerInvariant() } else { [string]$v }
        }
        $state.Properties = $props
    }
    return $state
}

function Get-GftStateKey($state) {
    $k = $state.Name
    if ($state.Contains('Properties')) { foreach ($p in $state.Properties.GetEnumerator()) { $k += "|$($p.Key)=$($p.Value)" } }
    return $k
}

function Get-GftPaletteIndex($t, $state) {
    if ($state -is [hashtable]) { $state = ConvertTo-GftState $state['Name'] $state['Properties'] }
    $key = Get-GftStateKey $state
    if (-not $t.Keys.ContainsKey($key)) {
        $t.Keys[$key] = $t.Palette.Count
        $t.Palette.Add($state)
    }
    return $t.Keys[$key]
}

function Test-GftInside($t, [int]$x, [int]$y, [int]$z) {
    return $x -ge 0 -and $y -ge 0 -and $z -ge 0 -and $x -lt $t.Size[0] -and $y -lt $t.Size[1] -and $z -lt $t.Size[2]
}

function Set-GftBlock {
    param($Template, [int]$X, [int]$Y, [int]$Z, [string]$Block, [hashtable]$Properties)
    if (-not (Test-GftInside $Template $X $Y $Z)) { throw "Set-GftBlock $($Template.Id): ($X, $Y, $Z) is outside the template" }
    $i = Get-GftPaletteIndex $Template (ConvertTo-GftState $Block $Properties)
    $Template.Cells[($Y * $Template.Size[2] + $Z) * $Template.Size[0] + $X] = $i
}

function Set-GftBox {
    param($Template, [int]$X1, [int]$Y1, [int]$Z1, [int]$X2, [int]$Y2, [int]$Z2, [string]$Block, [hashtable]$Properties, [switch]$Hollow, [string]$Inside)
    $i  = Get-GftPaletteIndex $Template (ConvertTo-GftState $Block $Properties)
    $ii = if ($Inside) { Get-GftPaletteIndex $Template (ConvertTo-GftState $Inside $null) } else { -1 }
    foreach ($y in ([Math]::Min($Y1, $Y2))..([Math]::Max($Y1, $Y2))) {
        foreach ($z in ([Math]::Min($Z1, $Z2))..([Math]::Max($Z1, $Z2))) {
            foreach ($x in ([Math]::Min($X1, $X2))..([Math]::Max($X1, $X2))) {
                if (-not (Test-GftInside $Template $x $y $z)) { throw "Set-GftBox $($Template.Id): ($x, $y, $z) is outside the template" }
                $edge = $x -eq $X1 -or $x -eq $X2 -or $y -eq $Y1 -or $y -eq $Y2 -or $z -eq $Z1 -or $z -eq $Z2
                $idx  = $i
                if (-not $edge -and $Hollow) { if ($ii -ge 0) { $idx = $ii } else { continue } }
                $Template.Cells[($y * $Template.Size[2] + $z) * $Template.Size[0] + $x] = $idx
            }
        }
    }
}

# One horizontal layer from text rows: row index = z, character index = x; ' ' leaves a cell as it is.
function Set-GftLayer {
    param($Template, [int]$Y, [hashtable]$Key, [string[]]$Rows)
    for ($z = 0; $z -lt $Rows.Count; $z++) {
        $row = $Rows[$z]
        for ($x = 0; $x -lt $row.Length; $x++) {
            $c = [string]$row[$x]
            if ($c -eq ' ') { continue }
            if (-not $Key.ContainsKey($c)) { throw "Set-GftLayer $($Template.Id): no block for '$c' (y $Y, row $z)" }
            $v = $Key[$c]
            if ($v -is [string]) { Set-GftBlock $Template $x $Y $z $v $null }
            else { Set-GftBlock $Template $x $Y $z $v['Name'] $v['Properties'] }
        }
    }
}

function Add-GftMarker {
    param($Template, [int]$X, [int]$Y, [int]$Z, [string]$Marker, [hashtable]$Data = @{})
    if (-not (Test-GftInside $Template $X $Y $Z)) { throw "Add-GftMarker $($Template.Id): ($X, $Y, $Z) is outside the template" }
    $d = [ordered]@{}
    foreach ($k in ($Data.Keys | Sort-Object)) { $d[$k] = $Data[$k] }
    $Template.Markers.Add([ordered]@{ pos = @($X, $Y, $Z); marker = $Marker; data = $d })
}

# A TF data marker (STRUCTURE_BLOCK DATA mode "metadata").
function Add-GftDataMarker {
    param($Template, [int]$X, [int]$Y, [int]$Z, [string]$Metadata)
    Add-GftMarker $Template $X $Y $Z 'data' @{ metadata = $Metadata }
}

function Add-GftJigsaw {
    param($Template, [int]$X, [int]$Y, [int]$Z,
        [Parameter(Mandatory)][ValidateSet('north', 'south', 'east', 'west', 'up', 'down')][string]$Front,
        [ValidateSet('north', 'south', 'east', 'west', 'up', 'down')][string]$Top,
        [string]$Pool = 'minecraft:empty', [string]$Name = '', [string]$Target = '', [int]$Priority = 0)
    if (-not (Test-GftInside $Template $X $Y $Z)) { throw "Add-GftJigsaw $($Template.Id): ($X, $Y, $Z) is outside the template" }
    if (-not $Top) { $Top = if ($Front -in 'up', 'down') { 'north' } else { 'up' } }
    $j = [ordered]@{ pos = @($X, $Y, $Z); front = $Front; top = $Top; pool = $Pool; name = $Name; target = $Target }
    if ($Priority) { $j.priority = $Priority }
    $Template.Jigsaws.Add($j)
}

function Add-GftEntity {
    param($Template, [double]$X, [double]$Y, [double]$Z, [string]$Entity, [hashtable]$Data = @{})
    $Template.Entities.Add([ordered]@{ pos = @($X, $Y, $Z); id = $Entity; data = $Data })
}

function Save-Gft {
    param($Template)
    $script:Saved.Add($Template)
}

function Get-GftSaved { return , $script:Saved.ToArray() }
function Clear-GftSaved { $script:Saved.Clear() }

# The .gft.json text of a template: stable key order, one palette entry / marker / connector per line.
function ConvertTo-GftJson {
    param($Template)
    $runs = [System.Collections.Generic.List[string]]::new()
    $cells = $Template.Cells
    $i = 0
    while ($i -lt $cells.Length) {
        $j = $i
        while ($j -lt $cells.Length -and $cells[$j] -eq $cells[$i]) { $j++ }
        $runs.Add("[$($cells[$i]), $($j - $i)]")
        $i = $j
    }
    $line = { param($o) ($o | ConvertTo-Json -Depth 10 -Compress) }
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append("{`n")
    [void]$sb.Append("  `"format`": `"gloamingforest:template`",`n")
    [void]$sb.Append("  `"format_version`": 1,`n")
    [void]$sb.Append("  `"id`": $(& $line $Template.Id),`n")
    [void]$sb.Append("  `"size`": [$($Template.Size -join ', ')],`n")
    [void]$sb.Append("  `"palette`": [`n")
    for ($k = 0; $k -lt $Template.Palette.Count; $k++) {
        [void]$sb.Append("    $(& $line $Template.Palette[$k])$(if ($k -lt $Template.Palette.Count - 1) { ',' })`n")
    }
    [void]$sb.Append("  ],`n")
    [void]$sb.Append("  `"blocks`": [")
    for ($k = 0; $k -lt $runs.Count; $k++) {
        if ($k % 16 -eq 0) { [void]$sb.Append("`n    ") } else { [void]$sb.Append(' ') }
        [void]$sb.Append($runs[$k])
        if ($k -lt $runs.Count - 1) { [void]$sb.Append(',') }
    }
    [void]$sb.Append("`n  ]")
    foreach ($section in @(@('markers', $Template.Markers), @('jigsaw', $Template.Jigsaws), @('entities', $Template.Entities))) {
        $items = $section[1]
        if ($items.Count -eq 0) { continue }
        [void]$sb.Append(",`n  `"$($section[0])`": [`n")
        for ($k = 0; $k -lt $items.Count; $k++) {
            [void]$sb.Append("    $(& $line $items[$k])$(if ($k -lt $items.Count - 1) { ',' })`n")
        }
        [void]$sb.Append('  ]')
    }
    [void]$sb.Append("`n}`n")
    return $sb.ToString()
}

Export-ModuleMember -Function New-Gft, Set-GftBlock, Set-GftBox, Set-GftLayer, Add-GftMarker, Add-GftDataMarker, Add-GftJigsaw, Add-GftEntity, Save-Gft,
    Get-GftSaved, Clear-GftSaved, ConvertTo-GftJson
