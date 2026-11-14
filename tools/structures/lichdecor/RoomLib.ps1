# U22 (lichdecor): shared shapes of the lich tower room interiors, roofs, beards and decor
# (original GloamingForest designs, rule R11/R19). Dot-sourced by the *.gfs.ps1 scripts below this
# folder. The junction names are U21's (docs/units/lich.md, src/feature/lich/Tower.cpp):
#
#   room      "gf:lich_tower/room" facing north at (c, 1, 0), the doorway of the north wall
#   exits     "gf:lich_tower/bridge_out" -> "gf:lich_tower/bridge", facing out of the east / south /
#             west wall at its centre (y 1)
#   roof      "gf:lich_tower/roof_up" -> "gf:lich_tower/roof" on the top layer centre (facing north,
#             so the room can take a side roof); roofs: "gf:lich_tower/roof" facing down at (c, 0, c)
#   beard     "gf:lich_tower/beard_down" -> "gf:lich_tower/beard" facing down at the floor centre;
#             beards: "gf:lich_tower/beard" facing up at their top layer centre
#   decor     "gf:lich_tower/decor_floor" -> "gf:lich_tower/decor" facing up at the floor centre;
#             decor: "gf:lich_tower/decor" facing down at its bottom centre
#   ladders   "gf:lich_tower/ladder_up" -> "gf:ladder_below/<n>" facing up at (x, 1, z), and in the
#             rooms of the ladder_<n> pools "gf:ladder_below/<n>" facing down at (x, 0, z); the
#             (x, z) of each offset is fixed per size (Get-LadderSpot) and kept clear in every room
#
# A room of size n (3, 5, 7, 9) is n x n inside, w = n + 2 outside: floor at y 0, ceiling at y h - 1.

$script:NS = 'gloamingforest:lich_tower/'

# Ladder offsets of each size (Tower.cpp ladderPlacements) and their spots: the interior row by the
# south wall, the ladder facing north (hung on the south wall), never the centre column.
$script:LadderOffsets = @{ 5 = @(0, 2); 7 = @(0, 1, 3, 4); 9 = @(1, 2, 4, 5) }
$script:LadderX = @{
    5 = @{ 0 = 1; 2 = 5 }
    7 = @{ 0 = 1; 1 = 2; 3 = 6; 4 = 7 }
    9 = @{ 1 = 2; 2 = 3; 4 = 7; 5 = 8 }
}
function Get-LadderSpot([int]$n, [int]$offset) { return @($script:LadderX[$n][$offset], $n) }

$script:Slab    = @{ type = 'bottom' }
$script:TopSlab = @{ type = 'top' }

function Get-RoomKey([int]$x, [int]$y, [int]$z) { return "$x,$y,$z" }

# A room shell: floor, walls with chiseled corners, ceiling, doorways and every junction.
#   -Exits      east / south / west
#   -LadderBelow offsets of the ladder_<n> pools the room is in (the room is entered from below)
#   -NoLadderUp  no ladder-up connectors
#   -Decor      a centre decor connector (the centre 3 x 3 stays free)
function New-LichRoom {
    param(
        [Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][int]$N, [int]$Height = 0,
        [string[]]$Exits = @(), [int[]]$LadderBelow = @(), [switch]$NoLadderUp, [switch]$Decor,
        [string]$Floor = 'minecraft:stone_bricks', [string]$Wall = 'minecraft:stone_bricks',
        [string]$Corner = 'minecraft:chiseled_stone_bricks', [string]$Ceiling = 'minecraft:stone_bricks',
        [string]$Band = 'minecraft:polished_andesite', [switch]$NoWindows
    )
    $w = $N + 2; $h = if ($Height) { $Height } else { $N + 2 }; $c = [int][Math]::Floor($w / 2)
    $size = "${N}x${N}"
    $t = New-Gft -Id "$($script:NS)$size/$Name" -Size $w, $h, $w -Out "lich/rooms/$size/$Name"
    $t | Add-Member -NotePropertyName Protected -NotePropertyValue @{}
    $t | Add-Member -NotePropertyName N -NotePropertyValue $N
    $t | Add-Member -NotePropertyName W -NotePropertyValue $w
    $t | Add-Member -NotePropertyName H -NotePropertyValue $h
    $t | Add-Member -NotePropertyName C -NotePropertyValue $c

    Set-GftBox $t 0 0 0 ($w - 1) 0 ($w - 1) $Floor
    Set-GftBox $t 0 1 0 ($w - 1) ($h - 2) ($w - 1) $Wall
    Set-GftBox $t 1 1 1 ($w - 2) ($h - 2) ($w - 2) 'minecraft:air'
    Set-GftBox $t 0 ($h - 1) 0 ($w - 1) ($h - 1) ($w - 1) $Ceiling
    foreach ($x in 0, ($w - 1)) { foreach ($z in 0, ($w - 1)) { Set-GftBox $t $x 1 $z $x ($h - 2) $z $Corner } }
    if ($h -ge 6) {
        # a band course under the ceiling
        foreach ($i in 1..($w - 2)) {
            foreach ($p in @(@($i, 0), @($i, ($w - 1)), @(0, $i), @(($w - 1), $i))) { Set-GftBlock $t $p[0] ($h - 2) $p[1] $Band }
        }
    }

    # doorways and the cells inside them stay clear
    $doors = @(, @($c, 0, 0, 1))
    foreach ($e in $Exits) {
        switch ($e) {
            'east' { $doors += , @(($w - 1), $c, -1, 0) }
            'south' { $doors += , @($c, ($w - 1), 0, -1) }
            'west' { $doors += , @(0, $c, 1, 0) }
            default { throw "New-LichRoom ${Name}: bad exit $e" }
        }
    }
    foreach ($d in $doors) {
        Set-GftBlock $t $d[0] 1 $d[1] 'minecraft:air'; Set-GftBlock $t $d[0] 2 $d[1] 'minecraft:air'
        for ($k = 1; $k -le [Math]::Min(2, ($N - 1)); $k++) {
            foreach ($y in 1, 2) { $t.Protected[(Get-RoomKey ($d[0] + $d[2] * $k) $y ($d[1] + $d[3] * $k))] = $true }
        }
    }
    Add-GftJigsaw $t $c 1 0 -Front north -Name 'gf:lich_tower/room'
    foreach ($e in $Exits) {
        $p = switch ($e) { 'east' { @(($w - 1), $c) } 'south' { @($c, ($w - 1)) } 'west' { @(0, $c) } }
        Add-GftJigsaw $t $p[0] 1 $p[1] -Front $e -Name 'gf:lich_tower/bridge_out' -Target 'gf:lich_tower/bridge'
    }

    # windows in the walls without a doorway
    if (-not $NoWindows) {
        $spots = if ($N -le 3) { @($c) } else { @(($c - 2), ($c + 2)) }
        foreach ($s in $spots) {
            if ('south' -notin $Exits -or $s -ne $c) { Set-GftBlock $t $s 2 ($w - 1) 'minecraft:iron_bars' }
            if ('east' -notin $Exits -or $s -ne $c) { Set-GftBlock $t ($w - 1) 2 $s 'minecraft:iron_bars' }
            if ('west' -notin $Exits -or $s -ne $c) { Set-GftBlock $t 0 2 $s 'minecraft:iron_bars' }
        }
    }

    Add-GftJigsaw $t $c ($h - 1) $c -Front north -Top up -Name 'gf:lich_tower/roof_up' -Target 'gf:lich_tower/roof'
    if ($N -ge 5) { Add-GftJigsaw $t $c 0 $c -Front down -Top north -Name 'gf:lich_tower/beard_down' -Target 'gf:lich_tower/beard' }
    if ($Decor) {
        Add-GftJigsaw $t $c 0 $c -Front up -Top north -Name 'gf:lich_tower/decor_floor' -Target 'gf:lich_tower/decor'
        foreach ($dx in -1..1) { foreach ($dz in -1..1) { foreach ($y in 1..($h - 2)) { $t.Protected[(Get-RoomKey ($c + $dx) $y ($c + $dz))] = $true } } }
    }

    # the ladder columns of the size stay clear in every room
    if ($script:LadderOffsets.ContainsKey($N)) {
        foreach ($o in $script:LadderOffsets[$N]) {
            $s = Get-LadderSpot $N $o
            foreach ($y in 1..($h - 2)) { $t.Protected[(Get-RoomKey $s[0] $y $s[1])] = $true }
            if (-not $NoLadderUp) { Add-GftJigsaw $t $s[0] 1 $s[1] -Front up -Top north -Name 'gf:lich_tower/ladder_up' -Target "gf:ladder_below/$o" }
        }
        foreach ($o in $LadderBelow) {
            if ($o -notin $script:LadderOffsets[$N]) { throw "New-LichRoom ${Name}: ladder offset $o is not one of size $N" }
            $s = Get-LadderSpot $N $o
            Add-GftJigsaw $t $s[0] 0 $s[1] -Front down -Top north -Name "gf:ladder_below/$o"
        }
    }
    return $t
}

function Test-RoomFree($t, [int]$x, [int]$y, [int]$z) {
    if ($x -lt 1 -or $z -lt 1 -or $x -gt $t.N -or $z -gt $t.N -or $y -lt 1 -or $y -gt $t.H - 2) { return $false }
    return -not $t.Protected.ContainsKey((Get-RoomKey $x $y $z))
}

# A block inside the room, unless the cell is kept clear.
function Set-RoomBlock($t, [int]$x, [int]$y, [int]$z, [string]$block, [hashtable]$props) {
    if (Test-RoomFree $t $x $y $z) { Set-GftBlock $t $x $y $z $block $props }
}

# A data marker of LichTowerWingRoom (docs/spec/lich.md 4.4) inside the room; its cell is air.
function Add-RoomMarker($t, [int]$x, [int]$y, [int]$z, [string]$label) {
    if (-not (Test-RoomFree $t $x $y $z)) { return }
    # one marker per cell: the first one wins
    if (-not ($t.PSObject.Properties.Name -contains 'Marked')) { $t | Add-Member -NotePropertyName Marked -NotePropertyValue @{} }
    $key = Get-RoomKey $x $y $z
    if ($t.Marked.ContainsKey($key)) { return }
    $t.Marked[$key] = $true
    Set-GftBlock $t $x $y $z 'minecraft:air'
    Add-GftDataMarker $t $x $y $z $label
}

# The cells along the inside of the walls at height y (clockwise from the north-west).
function Get-RoomRing($t, [int]$inset = 0) {
    $a = 1 + $inset; $b = $t.N - $inset
    $cells = [System.Collections.Generic.List[object]]::new()
    if ($a -gt $b) { return @() }
    for ($x = $a; $x -le $b; $x++) { $cells.Add(@($x, $a, 'south')) }
    for ($z = $a + 1; $z -le $b; $z++) { $cells.Add(@($b, $z, 'west')) }
    for ($x = $b - 1; $x -ge $a; $x--) { $cells.Add(@($x, $b, 'north')) }
    for ($z = $b - 1; $z -gt $a; $z--) { $cells.Add(@($a, $z, 'east')) }
    return $cells.ToArray()
}

# The facing pointing into the room from a wall-side cell.
function Get-Inward($t, [int]$x, [int]$z) {
    if ($z -eq 1) { return 'south' }
    if ($z -eq $t.N) { return 'north' }
    if ($x -eq 1) { return 'east' }
    return 'west'
}

function Get-RoomCorners($t, [int]$inset = 0) {
    $a = 1 + $inset; $b = $t.N - $inset
    return @(@($a, $a), @($b, $a), @($a, $b), @($b, $b))
}

# ---- furnishings ------------------------------------------------------------------------------------

# Shelving along every wall from y 1 to y top (markers: a random shelf per cell).
function Add-RoomShelves($t, [int]$top = 3, [string]$label = 'bookshelf|canopy_shelf', [int]$every = 1) {
    $i = 0
    foreach ($cell in (Get-RoomRing $t)) {
        $i++
        if ($every -gt 1 -and ($i % $every) -ne 0) { continue }
        for ($y = 1; $y -le [Math]::Min($top, $t.H - 2); $y++) { Add-RoomMarker $t $cell[0] $y $cell[1] $label }
    }
}

# Chiseled canopy shelves on the wall cells of one side (facing into the room).
function Add-RoomChiseledShelves($t, [string]$side, [int]$y = 2) {
    foreach ($cell in (Get-RoomRing $t)) {
        $in = Get-Inward $t $cell[0] $cell[1]
        if ($in -eq $side) { Add-RoomMarker $t $cell[0] $y $cell[1] "chiseled_canopy_shelf@$in" }
    }
}

# Candles on the corners (data markers with a count range).
function Add-RoomCandles($t, [int]$y = 1, [string]$label = 'candle:1-3', [int]$inset = 0) {
    foreach ($p in (Get-RoomCorners $t $inset)) { Add-RoomMarker $t $p[0] $y $p[1] $label }
}

# Hanging ropes / chains from the ceiling (one per column is kept by the room).
function Add-RoomHangers($t, [string]$kind = 'rope', [string]$label = 'candle:1-2') {
    $y = $t.H - 2
    foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomMarker $t $p[0] $y $p[1] "${kind}50%1-2>$label" }
}

# A counter of polished andesite with markers on top along one wall.
function Add-RoomCounter($t, [string]$side, [string[]]$labels, [string]$block = 'minecraft:polished_andesite') {
    $cells = @(Get-RoomRing $t | Where-Object { (Get-Inward $t $_[0] $_[1]) -eq $side })
    $i = 0
    foreach ($cell in $cells) {
        if (-not (Test-RoomFree $t $cell[0] 1 $cell[1]) -or -not (Test-RoomFree $t $cell[0] 2 $cell[1])) { continue }
        Set-RoomBlock $t $cell[0] 1 $cell[1] $block
        if ($labels.Count) { Add-RoomMarker $t $cell[0] 2 $cell[1] $labels[$i % $labels.Count] }
        $i++
    }
}

# A carpet runner across the floor (coronation carpet by default).
function Add-RoomCarpet($t, [string]$block = 'gloamingforest:coronation_carpet', [int]$inset = 1) {
    for ($x = 1 + $inset; $x -le $t.N - $inset; $x++) {
        for ($z = 1 + $inset; $z -le $t.N - $inset; $z++) {
            if (Test-RoomFree $t $x 1 $z) { Set-GftBlock $t $x 1 $z $block }
        }
    }
}

# A pillar from the floor to the ceiling.
function Add-RoomPillar($t, [int]$x, [int]$z, [string]$block = 'gloamingforest:twisted_stone_pillar') {
    for ($y = 1; $y -le $t.H - 2; $y++) { Set-RoomBlock $t $x $y $z $block }
}

# A mezzanine of canopy slabs at height y around the walls (depth cells), with a vanilla ladder up.
function Add-RoomMezzanine($t, [int]$y, [int]$depth = 1, [string]$rail = 'gloamingforest:wrought_iron_fence') {
    for ($x = 1; $x -le $t.N; $x++) {
        for ($z = 1; $z -le $t.N; $z++) {
            $edge = [Math]::Min([Math]::Min($x - 1, ($z - 1)), [Math]::Min($t.N - $x, $t.N - $z))
            if ($edge -lt $depth) { Set-RoomBlock $t $x $y $z 'gloamingforest:canopy_slab' $script:TopSlab }
            elseif ($edge -eq $depth -and $rail) { Set-RoomBlock $t $x ($y + 1) $z $rail @{ post = 'post' } }
        }
    }
}

# A small stone altar with candles on it.
function Add-RoomAltar($t, [int]$x, [int]$z, [string]$label = 'candle:2-4') {
    Set-RoomBlock $t $x 1 $z 'minecraft:chiseled_stone_bricks'
    Add-RoomMarker $t $x 2 $z $label
}

# ---- roofs, beards, decor -----------------------------------------------------------------------------

function New-LichRoof([int]$N, [string]$Name, [int]$Height) {
    $w = $N + 2; $c = [int][Math]::Floor($w / 2)
    $size = "${N}x${N}"
    $t = New-Gft -Id "$($script:NS)$size/$Name" -Size $w, $Height, $w -Out "lich/rooms/$size/$Name"
    $t | Add-Member -NotePropertyName W -NotePropertyValue $w
    $t | Add-Member -NotePropertyName C -NotePropertyValue $c
    Add-GftJigsaw $t $c 0 $c -Front down -Top north -Name 'gf:lich_tower/roof'
    return $t
}

function New-LichBeard([int]$N, [string]$Name, [int]$Height) {
    $w = $N + 2; $c = [int][Math]::Floor($w / 2)
    $size = "${N}x${N}"
    $t = New-Gft -Id "$($script:NS)$size/$Name" -Size $w, $Height, $w -Out "lich/rooms/$size/$Name"
    $t | Add-Member -NotePropertyName W -NotePropertyValue $w
    $t | Add-Member -NotePropertyName C -NotePropertyValue $c
    Add-GftJigsaw $t $c ($Height - 1) $c -Front up -Top north -Name 'gf:lich_tower/beard'
    return $t
}

# A square ring (inset k) of one layer.
function Set-SquareRing($t, [int]$y, [int]$k, [string]$block, [hashtable]$props) {
    $a = $k; $b = $t.W - 1 - $k
    if ($a -gt $b) { return }
    for ($i = $a; $i -le $b; $i++) {
        Set-GftBlock $t $i $y $a $block $props; Set-GftBlock $t $i $y $b $block $props
        Set-GftBlock $t $a $y $i $block $props; Set-GftBlock $t $b $y $i $block $props
    }
}

# Stairs around a square ring (inset k), each facing in towards the centre.
function Set-StairRing($t, [int]$y, [int]$k, [string]$block, [string]$half = 'bottom') {
    $a = $k; $b = $t.W - 1 - $k
    if ($a -gt $b) { return }
    if ($a -eq $b) { Set-GftBlock $t $a $y $a ($block -replace '_stairs$', '_slab') @{ type = $half }; return }
    for ($i = $a; $i -le $b; $i++) {
        Set-GftBlock $t $i $y $a $block @{ facing = 'south'; half = $half }
        Set-GftBlock $t $i $y $b $block @{ facing = 'north'; half = $half }
    }
    for ($i = $a + 1; $i -lt $b; $i++) {
        Set-GftBlock $t $a $y $i $block @{ facing = 'east'; half = $half }
        Set-GftBlock $t $b $y $i $block @{ facing = 'west'; half = $half }
    }
}
