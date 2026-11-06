# Building (U5) on the client: the building.* selftests (local: the integrated server; remote: the
# BDS host), then the showcase (building.showcase.ps1: /function gfbuilding/showcase) photographed
# row by row for the review against TF's textures and models:
#   out/building/trophies.png        the nine floor trophies, facing the camera
#   out/building/wall_trophies.png   the nine wall trophies on a wall
#   out/building/rotations.png       the naga trophy in its 16 rotations (left to right 0..15)
#   out/building/spawners.png        the nine boss spawners and the five miniature structures
#   out/building/nagastone.png       nagastone, pillars, etched, stairs, spiral, twisted pillar, rope, iron ladders
#   out/building/blocks.png          castle, rune, maze, under, tower, dead rock and storage blocks
#   out/building/stairs.png          encased / bold castle brick stairs, mossy / cracked nagastone stairs
#   out/building/ur_ghast-a.png, ur_ghast-b.png, ur_ghast-wall.png   the placed ur-ghast trophies close
#                                    up (client mod: the tentacles wave between a and b)
#   out/building/hotbar.png          the nine trophy items in the hotbar
#   out/building/patterns-hotbar.png the nine banner pattern items in the hotbar
#   out/building/held-first-person.png, held-third-person.png, worn.png   a trophy held and worn
#   out/building/banners.png         TF's nine banner patterns on standing banners
# The remote-vanilla client draws the same blocks from the host's resource packs (no client mod).
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
# TF's banner patterns in the registry (the client mod: at install, after the game's own patterns):
# remote-mod also compares the client's ids with the BDS host's (banners carry the nameID string,
# but the loom screen and the renderer index the registry)
if ($script:Mode -ne 'remote-vanilla') {
    $ids = Wait-LogLine '^(?!.*\[host\]).*building: banner pattern ids' 60
    if ($ids -notmatch 'tf_naga=\d+') { throw "the client mod did not register TF's banner patterns: $ids" }
    Write-Step "building: client $($ids -replace '^.*banner pattern ids', 'banner pattern ids')"
    if ($script:Mode -eq 'remote-mod') {
        $hostIds = Wait-LogLine '\[host\].*building: banner pattern ids' 60
        $a = $ids -replace '^.*banner pattern ids', ''
        $b = $hostIds -replace '^.*banner pattern ids', ''
        if ($a -ne $b) { throw "banner pattern ids differ: client$a, host$b" }
    }
}
$r = Invoke-SelfTest 'building.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) building selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected at least 14 building selftests to pass, got $($r.Pass)" }

Invoke-Chat '/gamerule sendcommandfeedback false'
Invoke-Chat '/gamerule showcoordinates false'
Invoke-Chat '/time set noon'
Invoke-Chat '/weather clear'
Invoke-Chat '/gamemode creative'
# setblock only reaches loaded chunks: the showcase runs from both halves of its rows
foreach ($z in 40, 100) {
    Invoke-Teleport "/tp @s 30 -60 $z"
    Start-Sleep -Seconds 3
    Invoke-Chat '/function gfbuilding/showcase'
    Start-Sleep -Seconds 3
}
Invoke-Chat '/gamemode spectator'
Start-Sleep -Seconds 2

$shots = @(
    @{ name = 'trophies'; tp = '/tp @s 28 -58 25 180 25' },
    @{ name = 'wall_trophies'; tp = '/tp @s 28 -58.5 46 180 10' },
    @{ name = 'rotations'; tp = '/tp @s 27.5 -56 64 180 40' },
    @{ name = 'spawners'; tp = '/tp @s 28 -57 89 180 22' },
    @{ name = 'nagastone'; tp = '/tp @s 30 -57 108 180 18' },
    @{ name = 'blocks'; tp = '/tp @s 27 -58 128 180 12' },
    @{ name = 'stairs'; tp = '/tp @s 38.5 -58.5 127 180 25' }
)
foreach ($s in $shots) {
    Invoke-Teleport $s.tp
    Start-Sleep -Seconds 3
    Save-Screenshot $s.name | Out-Null
    Write-Step "building: $($s.name)"
}

# the placed ur-ghast trophies close up, twice 1.5 s apart: with the client mod the tentacles wave
# (TrophyRenderer with the block entity's animation progress; a block renderer draws them), the
# block geometry has the head only (what a vanilla client sees)
Invoke-Teleport '/tp @s 26.5 -59.2 22.6 180 30'
Start-Sleep -Seconds 3
Save-Screenshot 'ur_ghast-a' | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'ur_ghast-b' | Out-Null
Invoke-Teleport '/tp @s 26.5 -58.6 43.4 180 20'
Start-Sleep -Seconds 3
Save-Screenshot 'ur_ghast-wall' | Out-Null
if ($script:Mode -ne 'remote-vanilla') {
    $anim = Wait-LogLine '^(?!.*\[host\]).*building: ur-ghast trophy animation ready \((\d+) bones, (\d+) drawn\)' 30
    if (-not $anim -or $anim -notmatch 'ready \((\d+) bones, (\d+) drawn\)' -or [int]$Matches[2] -lt 37) { throw "the ur-ghast trophy animation did not load its geometry: $anim" }
    Write-Step "building: $($anim -replace '^.*building: ', '')"
}
# the client mod's rope (Scaffolding and the tightrope collision provider) on the block types of the
# world it joined: remote-mod has no onWorldReady on the client
if ($script:Mode -ne 'remote-vanilla') {
    $rope = Wait-LogLine '^(?!.*\[host\]).*building: block types refreshed \(.*\): rope scaffolding (true|false), rope collision provider (true|false)' 30
    if ($rope -notmatch 'rope scaffolding true, rope collision provider true') { throw "the client's rope type is not set up: $rope" }
    Write-Step "building: client $($rope -replace '^.*building: ', '')"
}

# the trophy items in the hotbar (client mod: the head turns over the trophy backdrop)
Invoke-Chat '/gamemode creative'
Invoke-Chat '/clear @s'
foreach ($b in 'naga', 'lich', 'hydra', 'ur_ghast', 'knight_phantom', 'snow_queen', 'minoshroom', 'alpha_yeti', 'quest_ram') {
    Invoke-Chat "/give @s twilightforest:${b}_trophy"
}
Invoke-Teleport '/tp @s 28 -58 25 180 25'
Start-Sleep -Seconds 2
Save-Screenshot 'hotbar' | Out-Null
# the nine banner pattern items
Invoke-Chat '/clear @s'
foreach ($b in 'naga', 'lich', 'hydra', 'ur_ghast', 'knight_phantom', 'snow_queen', 'minoshroom', 'alpha_yeti', 'quest_ram') {
    Invoke-Chat "/give @s twilightforest:${b}_banner_pattern"
}
Start-Sleep -Seconds 2
Save-Screenshot 'patterns-hotbar' | Out-Null

# a trophy held (first person, then third person from the front) and worn on the head
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s twilightforest:naga_trophy'
Invoke-Teleport '/tp @s 28 -58 25 180 10'
Start-Sleep -Seconds 2
Save-Screenshot 'held-first-person' | Out-Null
Invoke-Chat '/camera @s set minecraft:third_person_front'
Start-Sleep -Seconds 3
Save-Screenshot 'held-third-person' | Out-Null
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.armor.head 0 twilightforest:minoshroom_trophy'
Start-Sleep -Seconds 2
Save-Screenshot 'worn' | Out-Null
Invoke-Chat '/camera @s clear'
Invoke-Chat '/clear @s'
Start-Sleep -Seconds 1

# TF's banner patterns on placed banners: building.banner_loom keeps its nine banners up for 20 s
# while the player carries the gf_building_banners tag (local: the client mod's registry and
# textures; remote-vanilla: a client without the patterns draws each as a plain field)
Invoke-Chat '/tag @s add gf_building_banners'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "building.banner_loom"'
$shown = Wait-LogLine 'building: TF banners shown at (-?\d+) (-?\d+) (-?\d+)' 120 -Since $mark
if ($shown -notmatch 'shown at (-?\d+) (-?\d+) (-?\d+)') { throw "building.banner_loom did not put up its banners: $shown" }
if ($shown -match 'shown at (-?\d+) (-?\d+) (-?\d+)') {
    $bx = [int]$Matches[1] + 4; $by = [int]$Matches[2] + 1; $bz = [int]$Matches[3] + 6
    Invoke-Chat '/gamemode spectator'
    Invoke-Teleport "/tp @s $bx $by $bz 180 10"
    Start-Sleep -Seconds 4
    Save-Screenshot 'banners' | Out-Null
    Invoke-Chat '/gamemode creative'
}
$done = Wait-LogLine 'SELFTEST DONE pass=\d+ fail=\d+' 120 -Since $mark
if ($done -notmatch 'fail=0') { throw "building.banner_loom failed: $done" }
Invoke-Chat '/tag @s remove gf_building_banners'

# the loom screen of a real loom (the real CraftLoom request with tf_naga is building.loom_request:
# the client's request bytes read and validated by the integrated or BDS server)
Invoke-Chat '/gamemode creative'
Invoke-Chat '/clear @s'
# there first: fill and setblock reach only loaded chunks (the banners stood far from here)
Invoke-Teleport '/tp @s 40.5 -60 150.5 180 35'
Start-Sleep -Seconds 3
Invoke-Chat '/fill 38 -60 147 42 -57 153 air'
Invoke-Chat '/setblock 40 -60 148 loom'
Invoke-Chat '/give @s banner 1 15'
Invoke-Chat '/give @s red_dye 1'
Invoke-Chat '/give @s twilightforest:naga_banner_pattern 1'
Invoke-Teleport '/tp @s 40.5 -60 150.5 180 35'
Start-Sleep -Seconds 2
Save-Screenshot 'loom-before' | Out-Null
# building.loom_fill waits for the loom screen and puts the banner, the dye and TF's pattern item
# into the loom's slots: the screen then lists TF's naga pattern (the client mod's registry)
Invoke-Chat '/tag @s add gf_building_loom'
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "building.loom_fill"'
Focus-Game
Press-Key '1'
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'loom-open' | Out-Null
Write-Step "building: loom screen $(Get-ClientScreen)"
$filled = Wait-LogLine 'building: loom slots of .* filled' 45 -Since $mark
if (-not $filled) { throw 'building.loom_fill did not fill the loom' }
Start-Sleep -Seconds 2
Save-Screenshot 'loom-filled' | Out-Null
Close-Screen
$done = Wait-LogLine 'SELFTEST DONE pass=\d+ fail=\d+' 60 -Since $mark
if ($done -notmatch 'fail=0') { throw "building.loom_fill failed: $done" }
Invoke-Chat '/tag @s remove gf_building_loom'
Invoke-Chat '/clear @s'

# a shield with TF's naga pattern in the off-hand, from the front (Bedrock draws shield patterns
# from the banner textures)
Invoke-Chat '/clear @s'
Invoke-Chat '/tag @s add gf_building_shield'
Invoke-Chat '/twilightforest selftest "building.shield_pattern"'
Start-Sleep -Seconds 2
Invoke-Teleport '/tp @s 40.5 -60 150.5 0 10'
Invoke-Chat '/camera @s set minecraft:third_person_front'
Start-Sleep -Seconds 3
Save-Screenshot 'shield' | Out-Null
Invoke-Chat '/camera @s clear'
Invoke-Chat '/clear @s'
Invoke-Chat '/tag @s remove gf_building_shield'

Invoke-Chat '/clear @s'
Invoke-Chat '/function gfbuilding/clear'
Start-Sleep -Seconds 2
Save-Screenshot 'selftest' | Out-Null
