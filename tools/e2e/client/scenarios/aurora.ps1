# Aurora palace, Snow Queen and mushroom tower (U28), client: screenshots of what U28 adds.
#   palace-*        the aurora palace placed with /twilightforest place (the aurora tint on its walls:
#                   ColorHandler's per-position colour with the client mod, the tint method's colour
#                   on vanilla clients), its bridge and the Snow Queen's tower
#   palace-wing-floor, palace-boss-room   inside a wing (floor decorators, chests) and the queen's room
#   blocks          aurora block (TF's animated variants), pillar, slab and auroralized glass
#   beacon-beam     a beacon's beam through auroralized glass (white up to the glass, then the glass's
#                   aurora colour), next to a vanilla beacon under red stained glass
#   block-items     the four block items (TF's item colours)
#   snow-queen, snow-guardian, ice-cores, ice-crystal   the mobs (the queen with her ice shields)
#   ice-core-dying-1..3  the dying unstable ice core (swell and white flash)
#   mushroom-tower  the mushroom tower
#   reload-blocks   (local) the blocks again after Save & Quit and back in (a second world)
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
# next to it a vanilla beacon under red stained glass (the engine's own beam: the reference for the
# white section below the glass)
Invoke-Chat "/fill $($cx - 1) $($by) $($bz + 8) $($cx + 1) $($by) $($bz + 10) iron_block"
Invoke-Chat "/setblock $cx $($by + 1) $($bz + 9) beacon"
Invoke-Chat "/setblock $cx $($by + 3) $($bz + 9) red_stained_glass"
Invoke-Teleport "/tp @s $($cx + 10) $($by + 4) $($bz + 7) facing $cx $($by + 5) $($bz + 7)"
Start-Sleep -Seconds 6
Save-Screenshot 'beacon-beam' | Out-Null
# the client-only beam hook (BDS never computes beam sections): the client mod recoloured this beam,
# white from the beacon up to the glass as Java (the engine's sections count the beacon block)
if ($script:Mode -ne 'remote-vanilla') {
    $beam = Wait-LogLine "^(?!\[host\]).*aurora: beacon beam at $cx $($by + 1) $($bz + 5) coloured" 20 -Since $beaconMark
    Write-Host "[aurora] $beam"
    try { Write-Host "[aurora] $(Wait-LogLine "^(?!\[host\]).*aurora: vanilla beacon beam at $cx $($by + 1) $($bz + 9)" 10 -Since $beaconMark)" } catch { Write-Host "[aurora] no vanilla beam sections logged ($_)" }
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
# a hollow 3 x 4 x 3 shell of aurora blocks around the player: the crosshair is on an aurora block
# (within reach) whichever way the camera points
# (go there first: /fill needs the chunk loaded)
Invoke-Teleport "/tp @s $($mx + 0.5) $($my + 6) $($mz + 0.5)"
Start-Sleep -Seconds 2
Invoke-Chat "/fill $($mx - 1) $my $($mz - 1) $($mx + 1) $($my + 3) $($mz + 1) twilightforest:aurora_block hollow"
Invoke-SelfTest 'aurora.client_miner' | Out-Null
Invoke-Chat '/gamemode survival'
Invoke-Teleport "/tp @s $($mx + 0.5) $($my + 1) $($mz + 0.5) 0 0"
Start-Sleep -Seconds 2
$mineMark = Get-LogMark
Focus-Game
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
# the dying unstable ice core (UnstableIceCoreRenderer): swollen 1.4 x 1.1 with the sin(t x 100)
# wobble, flashing white every other 2 ticks, for 60 ticks before it explodes
Invoke-Chat '/gamerule mobgriefing false'
$dx = 4400; $dz = 4400; $dy = -60
Invoke-Teleport "/tp @s $($dx + 4) $($dy) $($dz + 0.5) facing $($dx + 0.5) $($dy + 1) $($dz + 0.5)"
Start-Sleep -Seconds 2
Invoke-Chat "/summon twilightforest:unstable_ice_core $($dx + 0.5) $($dy) $($dz + 0.5) facing @p"
Start-Sleep -Seconds 1
$dyingMark = Get-LogMark
Invoke-Chat '/damage @e[type=twilightforest:unstable_ice_core] 100'
Wait-LogLine 'aurora: unstable ice core dying' 10 -Since $dyingMark | Out-Null
Start-Sleep -Milliseconds 400
foreach ($k in 1..3) { Save-Screenshot "ice-core-dying-$k" | Out-Null; Start-Sleep -Milliseconds 130 }
Start-Sleep -Seconds 3
Invoke-Chat '/kill @e[type=twilightforest:unstable_ice_core]'
Invoke-Chat '/gamerule mobgriefing true'

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
    # the client mod draws the untinted textures with the per-position colour (vanilla clients: the
    # baked textures of the block definitions)
    Wait-LogLine '^(?!\[host\]).*aurora: client materials swapped to the per-position tint' 5 | Out-Null
}

# ---- a second world in the same game session (local) -------------------------------------------
# The block registry is the new world's: the aurora block's variants (and F7's block of it) resolve
# again, so placed and /setblock aurora blocks still get their position's variant.
if ($script:Mode -eq 'local') {
    Focus-Game
    try { Invoke-BeDDAction pause | Out-Null } catch { Press-Key ESC }
    Start-Sleep -Seconds 2
    # Save & Quit: the fourth button of the pause menu's left column (as the item scenario)
    Move-Mouse 0.293 0.667 -Relative
    Start-Sleep -Milliseconds 300
    Click 0.293 0.667 -Relative
    Start-Sleep -Seconds 15
    $mark = Get-LogMark
    Focus-Game
    Click 0.173 0.43 -Relative
    Wait-LogLine 'Player Spawned' 90 -Since $mark | Out-Null
    Wait-LogLine 'GloamingForest world ready' 60 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Wait-HudReady
    $r = Invoke-SelfTest 'aurora.aurora_block_variant'
    if ($r.Fail -gt 0) { throw 'aurora.aurora_block_variant failed in the reopened world' }
    Invoke-Teleport "/tp @s $($bx + 3) $($by + 2) $($bz - 4) facing $($bx + 3) $($by + 1) $bz"
    Start-Sleep -Seconds 4
    Save-Screenshot 'reload-blocks' | Out-Null
}
