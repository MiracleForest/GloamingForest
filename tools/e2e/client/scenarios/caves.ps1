# Troll caves and yeti cave (U27), client: screenshots of what U27 adds (post-merge acceptance).
#   troll-cave-*    a troll cave placed with /gloamingforest place (hollowed caves, trollsteinn,
#                   bracket mushrooms, the giant obsidian vault with its chests)
#   yeti-cave       the yeti cave (ice stalactites, the alpha yeti spawner)
#   trollsteinn-lit / trollsteinn-dark   the trollsteinn faces by light (lit texture next to a glowstone,
#                   the dark one in a closed stone room) and, with the client mod, the purple sparkle
#   roots           trollvidr, unripe trollber and trollber hanging under stone
#   mushglooms      huge mushgloom cap and stem blocks
#   block-items     the six block items
#   troll, troll-rock, yeti, yeti-angry, alpha-yeti, alpha-yeti-rampage, alpha-yeti-tired,
#   thrown-block, falling-ice, thrown-block-flight, falling-ice-flight   the mobs and their states (the angry yeti's eyes and arms, the alpha
#                   yeti's raised arms and snow ring, its tired pose and splash)
# Out: out/caves[-<mode>]/*.png - open each one and compare it with upstream's textures and models.
if ($script:Mode -eq 'local') { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/difficulty normal'

function Set-Camera([double]$cx, [double]$cy, [double]$cz, [string]$facing) {
    Invoke-Teleport "/tp @s $cx $cy $cz"
    Invoke-Chat "/setblock $([math]::Floor($cx)) $([math]::Floor($cy) - 1) $([math]::Floor($cz)) glass"
    Invoke-Teleport "/tp @s $cx $cy $cz facing $facing"
    Start-Sleep -Seconds 3
}

# ---- the troll cave ------------------------------------------------------------------------------
$x = 5000; $z = 5000; $y = -40
Invoke-Teleport "/tp @s $($x) $($y + 20) $($z)"
Start-Sleep -Seconds 4
$mark = Get-LogMark
Invoke-Chat "/gloamingforest place gloamingforest:troll_cave $x $y $z"
Wait-LogLine "landmark: placed gloamingforest:troll_cave at $x $y $z" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Invoke-Chat '/effect @s night_vision 600 0 true'
# inside the main cave (30 x 20 x 30 from (-15, -20, -15) around y + 11), looking at its walls
Set-Camera ($x + 0.5) ($y + 1) ($z + 0.5) "$($x + 12) $($y + 2) $($z + 12)"
Save-Screenshot 'troll-cave-main' | Out-Null
Set-Camera ($x + 0.5) ($y + 1) ($z + 0.5) "$($x - 12) $($y + 5) $($z - 12)"
Save-Screenshot 'troll-cave-wall' | Out-Null
# the vault in the middle of the floor (giant obsidian), from above
Set-Camera ($x + 6.5) ($y - 3) ($z + 6.5) "$($x) $($y - 9) $($z)"
Save-Screenshot 'troll-cave-vault' | Out-Null

# ---- the yeti cave ---------------------------------------------------------------------------------
$yx = 5400; $yz = 5000; $yy = -60
Invoke-Teleport "/tp @s $($yx) $($yy + 40) $($yz)"
Start-Sleep -Seconds 4
$mark = Get-LogMark
Invoke-Chat "/gloamingforest place gloamingforest:yeti_cave $yx $yy $yz"
Wait-LogLine "landmark: placed gloamingforest:yeti_cave at $yx $yy $yz" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Set-Camera ($yx + 8.5) ($yy + 8) ($yz + 8.5) "$($yx + 8) $($yy + 18) $($yz + 20)"
Save-Screenshot 'yeti-cave' | Out-Null

# ---- blocks ------------------------------------------------------------------------------------------
$bx = 5800; $bz = 5000; $by = -60
Invoke-Teleport "/tp @s $($bx + 3) $($by + 2) $($bz - 4) facing $($bx + 3) $($by + 1) $bz"
Start-Sleep -Seconds 3
Invoke-Chat "/setblock $($bx) $($by + 1) $($bz) gloamingforest:trollsteinn"
Invoke-Chat "/setblock $($bx + 1) $($by + 1) $($bz) glowstone"
Invoke-Chat "/setblock $($bx + 2) $($by + 1) $($bz) gloamingforest:trollsteinn"
Invoke-Chat '/gamerule randomtickspeed 1000'
Start-Sleep -Seconds 3
Invoke-Chat '/gamerule randomtickspeed 3'
Save-Screenshot 'trollsteinn-lit' | Out-Null
# a closed stone room: the dark faces and the sparkle
Invoke-Chat "/fill $($bx + 5) $($by) $($bz - 3) $($bx + 11) $($by + 5) $($bz + 3) stone hollow"
Invoke-Chat "/setblock $($bx + 8) $($by + 2) $($bz) gloamingforest:trollsteinn"
Invoke-Teleport "/tp @s $($bx + 8.5) $($by + 1) $($bz - 2) facing $($bx + 8.5) $($by + 2.5) $($bz + 0.5)"
Invoke-Chat '/gamerule randomtickspeed 1000'
Start-Sleep -Seconds 3
Invoke-Chat '/gamerule randomtickspeed 3'
Save-Screenshot 'trollsteinn-dark' | Out-Null
if ($script:Mode -ne 'remote-vanilla') { Wait-LogLine '^(?!\[host\]).*caves: the client draws the trollsteinn sparkles' 10 | Out-Null }
# troll roots under stone, huge mushgloom blocks
$rx = $bx + 20
Invoke-Teleport "/tp @s $($rx + 3) $($by + 2) $($bz - 4) facing $($rx + 3) $($by + 2) $bz"
Start-Sleep -Seconds 2
Invoke-Chat "/fill $($rx) $($by + 4) $($bz) $($rx + 6) $($by + 4) $($bz) stone"
Invoke-Chat "/setblock $($rx + 1) $($by + 3) $($bz) gloamingforest:trollvidr"
Invoke-Chat "/setblock $($rx + 3) $($by + 3) $($bz) gloamingforest:unripe_trollber"
Invoke-Chat "/setblock $($rx + 5) $($by + 3) $($bz) gloamingforest:trollber"
Start-Sleep -Seconds 2
Save-Screenshot 'roots' | Out-Null
Invoke-Chat "/setblock $($rx + 1) $($by + 1) $($bz + 2) gloamingforest:huge_mushgloom"
Invoke-Chat "/setblock $($rx + 3) $($by + 1) $($bz + 2) gloamingforest:huge_mushgloom_stem"
Start-Sleep -Seconds 1
Save-Screenshot 'mushglooms' | Out-Null
Invoke-Chat '/clear @s'
foreach ($i in 'trollsteinn', 'trollvidr', 'unripe_trollber', 'trollber', 'huge_mushgloom', 'huge_mushgloom_stem') { Invoke-Chat "/give @s gloamingforest:$i" }
Start-Sleep -Seconds 1
Save-Screenshot 'block-items' | Out-Null

# ---- mobs ------------------------------------------------------------------------------------------
function Show-Mob([string]$id, [string]$shot, [double]$dy = 0, [double]$dist = 5, [double]$wait = 1.2, [string]$then = '') {
    $mx = 6200; $mz = 5000; $my = -60
    Invoke-Teleport "/tp @s $($mx + $dist) $($my) $($mz + 0.5) facing $($mx + 0.5) $($my + 1.5 + $dy) $($mz + 0.5)"
    Start-Sleep -Seconds 2
    Invoke-Chat "/summon $id $($mx + 0.5) $($my + $dy) $($mz + 0.5) facing @p"
    if ($then) { Invoke-Chat $then }
    Start-Sleep -Milliseconds ([int]($wait * 1000))
    Save-Screenshot $shot | Out-Null
    Invoke-Chat "/kill @e[type=$id]"
}
Invoke-Chat '/gamerule mobgriefing false'
Show-Mob 'gloamingforest:troll' 'troll' 0 5
Show-Mob 'gloamingforest:yeti' 'yeti' 0 5
# a hit makes the yeti angry (its eyes, arms forward)
Show-Mob 'gloamingforest:yeti' 'yeti-angry' 0 5 1.5 '/damage @e[type=gloamingforest:yeti,c=1] 1 entity_attack entity @p'
Show-Mob 'gloamingforest:alpha_yeti' 'alpha-yeti' 0 9 1.5
Show-Mob 'gloamingforest:thrown_block' 'thrown-block' 1 3
Show-Mob 'gloamingforest:falling_ice' 'falling-ice' 1 3
# the troll's held rock (granite: render controller troll_rock, the cube over its head as Java's
# riding ThrownBlock) and the alpha yeti's rampage / tired poses: the BP's gf:show_* events set the
# synced properties the native code sets in a fight
Show-Mob 'gloamingforest:troll' 'troll-rock' 0 5 1.5 '/event entity @e[type=gloamingforest:troll,c=1] gf:show_rock'
Show-Mob 'gloamingforest:alpha_yeti' 'alpha-yeti-rampage' 0 9 1.5 '/event entity @e[type=gloamingforest:alpha_yeti,c=1] gf:show_rampage'
Show-Mob 'gloamingforest:alpha_yeti' 'alpha-yeti-tired' 0 9 1.5 '/event entity @e[type=gloamingforest:alpha_yeti,c=1] gf:show_tired'
# the thrown block and the falling ice in flight (summoned 4 blocks up: they fall through the shot)
Show-Mob 'gloamingforest:thrown_block' 'thrown-block-flight' 4 4 0.4
Show-Mob 'gloamingforest:falling_ice' 'falling-ice-flight' 4 4 0.4
# the selftests' fights (rock pick-up and throw, rampage and tired)
if ($script:Mode -ne 'remote-vanilla') {
    Invoke-SelfTest 'caves.troll_rock' | Out-Null
    Invoke-SelfTest 'caves.alpha_yeti' | Out-Null
}
Invoke-Chat '/gamerule mobgriefing true'
