# Aurora palace, Snow Queen and mushroom tower (U28), client: screenshots of what U28 adds.
#   palace-*        the aurora palace placed with /twilightforest place (the aurora tint on its walls:
#                   ColorHandler's per-position colour with the client mod, the tint method's colour
#                   on vanilla clients), its bridge and the Snow Queen's tower
#   palace-wing-floor, palace-boss-room   inside a wing (floor decorators, chests) and the queen's room
#   blocks          aurora block (TF's animated variants), pillar, slab and auroralized glass
#   beacon-beam     a beacon's beam through auroralized glass (the glass's aurora colour)
#   block-items     the four block items (TF's item colours)
#   snow-queen, snow-guardian, ice-cores, ice-crystal   the mobs (the queen with her ice shields)
#   mushroom-tower  the mushroom tower
# Out: out/aurora[-<mode>]/*.png - open each one and compare it with TF's textures and models.
if ($script:Mode -eq 'local') { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/difficulty normal'

# the camera: a glass block under the player (creative, not flying), then the view
function Set-Camera([double]$cx, [double]$cy, [double]$cz, [string]$facing) {
    Invoke-Teleport "/tp @s $cx $cy $cz"
    Invoke-Chat "/setblock $([math]::Floor($cx)) $([math]::Floor($cy) - 1) $([math]::Floor($cz)) glass"
    Invoke-Teleport "/tp @s $cx $cy $cz facing $facing"
    Start-Sleep -Seconds 3
}

# ---- the aurora palace ------------------------------------------------------------------------
# load the chunks first (the start generates in the loaded chunks), then place it
$x = 4000; $z = 4000; $y = -60
Invoke-Teleport "/tp @s $($x + 16) $($y + 60) $($z + 16)"
Start-Sleep -Seconds 4
$mark = Get-LogMark
Invoke-Chat "/twilightforest place twilightforest:aurora_palace $x $y $z"
Wait-LogLine "landmark: placed twilightforest:aurora_palace at $x $y $z" 60 -Since $mark | Out-Null
$bossView = Wait-LogLine 'aurora: snow queen room view (-?\d+) (-?\d+) (-?\d+) facing (-?\d+) (-?\d+) (-?\d+)' 10 -Since $mark
$wingView = Wait-LogLine 'aurora: palace wing view (-?\d+) (-?\d+) (-?\d+) facing (-?\d+) (-?\d+) (-?\d+)' 10 -Since $mark
Start-Sleep -Seconds 3
# the main tower floats at y + 40 (x + 11 .. x + 21): look at it from the south-east, then from below
Set-Camera ($x + 60) ($y + 75) ($z + 60) "$($x + 16) $($y + 55) $($z + 16)"
Save-Screenshot 'palace-main' | Out-Null
Set-Camera ($x - 30) ($y + 60) ($z + 40) "$($x + 5) $($y + 50) $($z + 16)"
Save-Screenshot 'palace-wall' | Out-Null
Set-Camera ($x + 16.5) ($y + 46) ($z + 16.5) "$($x + 16) $($y + 46) $($z + 25)"
Save-Screenshot 'palace-inside' | Out-Null
# R19: inside the palace: a wing's lowest floor (floor decorators, chests) and the Snow Queen's room
function Show-View([string]$line, [string]$shot) {
    if ($line -notmatch 'view (-?\d+) (-?\d+) (-?\d+) facing (-?\d+) (-?\d+) (-?\d+)') { throw "no view in '$line'" }
    $v = $Matches
    # the chunks there generate (with the palace's pieces) once the player is near
    Invoke-Teleport "/tp @s $([int]$v[1] + 0.5) $([int]$v[2] + 30) $([int]$v[3] + 0.5)"
    Start-Sleep -Seconds 5
    Invoke-Teleport "/tp @s $([int]$v[1] + 0.5) $([int]$v[2]) $([int]$v[3] + 0.5) facing $([int]$v[4] + 0.5) $([int]$v[5] + 0.5) $([int]$v[6] + 0.5)"
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
}
Show-View $wingView 'palace-wing-floor'
Show-View $bossView 'palace-boss-room'

# ---- blocks -------------------------------------------------------------------------------------
$bx = 4200; $bz = 4200; $by = -60
Invoke-Teleport "/tp @s $($bx + 3) $($by + 2) $($bz - 4) facing $($bx + 3) $($by + 1) $bz"
Start-Sleep -Seconds 3
Invoke-Chat "/fill $($bx - 2) $($by) $($bz) $($bx + 8) $($by + 3) $($bz + 1) twilightforest:aurora_block"
Invoke-Chat "/setblock $($bx + 1) $($by + 1) $($bz - 1) twilightforest:aurora_pillar"
Invoke-Chat "/setblock $($bx + 3) $($by + 1) $($bz - 1) twilightforest:aurora_slab"
Invoke-Chat "/setblock $($bx + 5) $($by + 1) $($bz - 1) twilightforest:auroralized_glass"
Start-Sleep -Seconds 2
Save-Screenshot 'blocks' | Out-Null
# a beacon under auroralized glass: the beam takes the glass's aurora colour (getBeaconColorMultiplier)
$cx = $bx + 14
$beaconMark = Get-LogMark
Invoke-Chat "/fill $($cx - 1) $($by) $($bz + 4) $($cx + 1) $($by) $($bz + 6) iron_block"
Invoke-Chat "/setblock $cx $($by + 1) $($bz + 5) beacon"
Invoke-Chat "/setblock $cx $($by + 3) $($bz + 5) twilightforest:auroralized_glass"
Invoke-Teleport "/tp @s $($cx + 8) $($by + 6) $($bz + 5) facing $cx $($by + 8) $($bz + 5)"
Start-Sleep -Seconds 6
Save-Screenshot 'beacon-beam' | Out-Null
# the client-only beam hook (BDS never computes beam sections): the client mod recoloured this beam
if ($script:Mode -ne 'remote-vanilla') {
    Wait-LogLine "^(?!\[host\]).*aurora: beacon beam at $cx $($by + 1) $($bz + 5) coloured" 20 -Since $beaconMark | Out-Null
}
# the block items in the hotbar (ColorHandler's item colours: hue 0.45, the pale colour)
Invoke-Chat '/clear @s'
foreach ($i in 'aurora_block', 'aurora_pillar', 'aurora_slab', 'auroralized_glass') { Invoke-Chat "/give @s twilightforest:$i" }
Start-Sleep -Seconds 1
Save-Screenshot 'block-items' | Out-Null

# ---- aurora block mining (AuroraBrickBlock#getDestroyProgress: 0.1 a tick with progress_glacier) -------
# a held left button breaks it in about 10 ticks: on the integrated server (local), and on BDS with
# the companion's own client-side progress (remote-mod) without the break being rolled back
$mx = 4300; $mz = 4300; $my = -60
Invoke-Chat '/clear @s'
Invoke-Chat "/setblock $mx $($my + 1) $($mz + 2) twilightforest:aurora_block"
Invoke-Teleport "/tp @s $($mx + 0.5) $my $($mz + 0.5) facing $($mx + 0.5) $($my + 1.5) $($mz + 2.5)"
Invoke-SelfTest 'aurora.client_miner' | Out-Null
Invoke-Chat '/gamemode survival'
Start-Sleep -Seconds 2
$mineMark = Get-LogMark
Focus-Game; Assert-GameForeground
[BsE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 1500; [BsE2E.Native]::MouseButton(0, $true)
try {
    $mined = Wait-LogLine 'aurora: aurora block broken after (\d+) ticks of mining' 10 -Since $mineMark
    Write-Host "[aurora] $mined"
    if ($mined -match 'after (\d+) ticks' -and [int]$Matches[1] -gt 14 -and $script:Mode -ne 'remote-vanilla') { throw "the aurora block took $($Matches[1]) ticks" }
} catch {
    if ($script:Mode -ne 'remote-vanilla') { throw }
    Write-Host "[aurora] remote-vanilla: no sped-up break ($_)"
}
Start-Sleep -Seconds 2
# the block stays broken (no rollback by the server)
Save-Screenshot 'mining' | Out-Null
Invoke-Chat '/gamemode creative'

# ---- mobs -----------------------------------------------------------------------------------------
function Show-Mob([string]$id, [string]$shot, [double]$dy = 0, [double]$dist = 4, [double]$wait = 1.2) {
    $mx = 4400; $mz = 4400; $my = -60
    Invoke-Teleport "/tp @s $($mx + $dist) $($my) $($mz + 0.5) facing $($mx + 0.5) $($my + 1 + $dy) $($mz + 0.5)"
    Start-Sleep -Seconds 2
    Invoke-Chat "/summon $id $($mx + 0.5) $($my + $dy) $($mz + 0.5) facing @p"
    Start-Sleep -Milliseconds ([int]($wait * 1000))
    Save-Screenshot $shot | Out-Null
    Invoke-Chat "/kill @e[type=$id]"
}
Show-Mob 'twilightforest:snow_queen' 'snow-queen' 1 6
# its gear (a TF sword, chestplate and helmet) reaches the client with the equipment packets
Show-Mob 'twilightforest:snow_guardian' 'snow-guardian' 0 3 2.5
Show-Mob 'twilightforest:stable_ice_core' 'ice-core-stable'
Show-Mob 'twilightforest:unstable_ice_core' 'ice-core-unstable'
Show-Mob 'twilightforest:ice_crystal' 'ice-crystal'

# ---- the mushroom tower -------------------------------------------------------------------------
$tx = 4600; $tz = 4600; $ty = -60
Invoke-Teleport "/tp @s $($tx + 20) $($ty + 30) $($tz + 20)"
Start-Sleep -Seconds 4
$mark = Get-LogMark
Invoke-Chat "/twilightforest place twilightforest:mushroom_tower $tx $ty $tz"
Wait-LogLine "landmark: placed twilightforest:mushroom_tower at $tx $ty $tz" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Set-Camera ($tx + 70) ($ty + 40) ($tz + 70) "$($tx + 20) $($ty + 15) $($tz + 20)"
Save-Screenshot 'mushroom-tower' | Out-Null

if ($script:Mode -ne 'remote-vanilla') {
    Wait-LogLine '^(?!\[host\]).*aurora: aurora block colours registered' 5 | Out-Null
}
