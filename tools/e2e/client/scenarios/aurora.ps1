# Aurora palace, Snow Queen and mushroom tower (U28), client: screenshots of what U28 adds.
#   palace-*        the aurora palace placed with /twilightforest place (the aurora tint on its walls:
#                   ColorHandler's per-position colour with the client mod, the tint method's colour
#                   on vanilla clients), its bridge and the Snow Queen's tower
#   blocks          aurora block, pillar, slab and auroralized glass
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
Start-Sleep -Seconds 3
# the main tower floats at y + 40 (x + 11 .. x + 21): look at it from the south-east, then from below
Set-Camera ($x + 60) ($y + 75) ($z + 60) "$($x + 16) $($y + 55) $($z + 16)"
Save-Screenshot 'palace-main' | Out-Null
Set-Camera ($x - 30) ($y + 60) ($z + 40) "$($x + 5) $($y + 50) $($z + 16)"
Save-Screenshot 'palace-wall' | Out-Null
Set-Camera ($x + 16.5) ($y + 46) ($z + 16.5) "$($x + 16) $($y + 46) $($z + 25)"
Save-Screenshot 'palace-inside' | Out-Null

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

# ---- mobs -----------------------------------------------------------------------------------------
function Show-Mob([string]$id, [string]$shot, [double]$dy = 0, [double]$dist = 4) {
    $mx = 4400; $mz = 4400; $my = -60
    Invoke-Teleport "/tp @s $($mx + $dist) $($my) $($mz + 0.5) facing $($mx + 0.5) $($my + 1 + $dy) $($mz + 0.5)"
    Start-Sleep -Seconds 2
    Invoke-Chat "/summon $id $($mx + 0.5) $($my + $dy) $($mz + 0.5) facing @p"
    Start-Sleep -Milliseconds 1200
    Save-Screenshot $shot | Out-Null
    Invoke-Chat "/kill @e[type=$id]"
}
Show-Mob 'twilightforest:snow_queen' 'snow-queen' 1 6
Show-Mob 'twilightforest:snow_guardian' 'snow-guardian'
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
