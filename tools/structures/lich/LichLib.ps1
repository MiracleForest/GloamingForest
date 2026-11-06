# U21 (lich): shared shapes of the lich tower templates (original GloamingForest designs, rule R11/R19).
# Dot-sourced by the *.gfs.ps1 scripts of this folder. The tower is a 13 x 13 stone brick shaft with
# cut corners; wings are bridges of stone and twilight oak leading to rooms.

$script:Wall    = 'minecraft:stone_bricks'
$script:Accent  = 'minecraft:polished_andesite'
$script:Chisel  = 'minecraft:chiseled_stone_bricks'
$script:Air     = 'minecraft:air'

# Whether (x, z) is on the outline of a W x W square with its corners cut by `cut` cells.
function Test-LichRing([int]$x, [int]$z, [int]$w, [int]$cut = 1) {
    $m = $w - 1
    $inside = Test-LichInside $x $z $w $cut
    if (-not $inside) { return $false }
    # an outline cell has a neighbour outside the shape
    foreach ($d in @(@(1, 0), @(-1, 0), @(0, 1), @(0, -1))) {
        $nx = $x + $d[0]; $nz = $z + $d[1]
        if ($nx -lt 0 -or $nz -lt 0 -or $nx -gt $m -or $nz -gt $m) { return $true }
        if (-not (Test-LichInside $nx $nz $w $cut)) { return $true }
    }
    return $false
}

# Whether (x, z) is inside a W x W square with its corners cut diagonally by `cut` cells.
function Test-LichInside([int]$x, [int]$z, [int]$w, [int]$cut = 1) {
    $m = $w - 1
    if ($x -lt 0 -or $z -lt 0 -or $x -gt $m -or $z -gt $m) { return $false }
    $dx = [Math]::Min($x, $m - $x); $dz = [Math]::Min($z, $m - $z)
    return ($dx + $dz) -ge $cut
}

# A disc-like layer: `ring` blocks on the outline, `fill` inside (empty strings skip).
function Set-LichLayer($t, [int]$y, [int]$w, [int]$cut, [string]$ring, [string]$fill, [int]$ox = 0, [int]$oz = 0) {
    for ($x = 0; $x -lt $w; $x++) {
        for ($z = 0; $z -lt $w; $z++) {
            if (-not (Test-LichInside $x $z $w $cut)) { continue }
            $b = if (Test-LichRing $x $z $w $cut) { $ring } else { $fill }
            if ($b) { Set-GftBlock $t ($x + $ox) $y ($z + $oz) $b }
        }
    }
}

# The tower's wall course at height y: stone bricks with an andesite band every `band` blocks and
# chiseled pillars at the cut corners.
function Set-LichWallCourse($t, [int]$y, [int]$w, [int]$cut, [bool]$band) {
    $ring = if ($band) { $script:Accent } else { $script:Wall }
    Set-LichLayer $t $y $w $cut $ring $script:Air
}

# A doorway in a wall at (x, y, z): two air cells.
function Set-LichDoor($t, [int]$x, [int]$y, [int]$z) {
    Set-GftBlock $t $x $y $z $script:Air
    Set-GftBlock $t $x ($y + 1) $z $script:Air
}

# The inside perimeter path of the shaft (clockwise from the north-west), one cell in from the wall.
function Get-LichPerimeter([int]$w, [int]$cut) {
    $cells = [System.Collections.Generic.List[object]]::new()
    $m = $w - 2
    # walk the square ring at distance 1 from the edge, skipping cells outside the cut shape
    for ($x = 1; $x -le $m; $x++) { $cells.Add(@($x, 1)) }
    for ($z = 2; $z -le $m; $z++) { $cells.Add(@($m, $z)) }
    for ($x = $m - 1; $x -ge 1; $x--) { $cells.Add(@($x, $m)) }
    for ($z = $m - 1; $z -ge 2; $z--) { $cells.Add(@(1, $z)) }
    return @($cells | Where-Object { (Test-LichInside $_[0] $_[1] $w $cut) -and -not (Test-LichRing $_[0] $_[1] $w $cut) })
}
