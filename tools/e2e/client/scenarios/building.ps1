# Building (U5) on the client: the building.* selftests (local: the integrated server; remote: the
# BDS host), then the showcase (building.showcase.ps1: /function gfbuilding/showcase) photographed
# row by row for the review against TF's textures and models:
#   out/building/trophies.png        the nine floor trophies, facing the camera
#   out/building/wall_trophies.png   the nine wall trophies on a wall
#   out/building/rotations.png       the naga trophy in its 16 rotations (left to right 0..15)
#   out/building/spawners.png        the nine boss spawners and the five miniature structures
#   out/building/nagastone.png       nagastone, pillars, etched, stairs, spiral, twisted pillar, rope, iron ladders
#   out/building/blocks.png          castle, rune, maze, under, tower, dead rock and storage blocks
#   out/building/hotbar.png          the nine trophy items in the hotbar
#   out/building/held-first-person.png, held-third-person.png, worn.png   a trophy held and worn
#   out/building/banners.png         TF's nine banner patterns on standing banners
# The remote-vanilla client draws the same blocks from the host's resource packs (no client mod).
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
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
    @{ name = 'blocks'; tp = '/tp @s 27 -58 128 180 12' }
)
foreach ($s in $shots) {
    Invoke-Teleport $s.tp
    Start-Sleep -Seconds 3
    Save-Screenshot $s.name | Out-Null
    Write-Step "building: $($s.name)"
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
# while the lapis marker of the showcase is there (local: the client mod's registry and textures;
# remote-vanilla: a client without the patterns draws each as a plain field)
Invoke-Chat '/setblock 16 -61 140 lapis_block'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "building.banner_loom"'
$shown = Wait-LogLine 'building: TF banners shown at (-?\d+) (-?\d+) (-?\d+)' 120 -Since $mark
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
Invoke-Chat '/setblock 16 -61 140 grass_block'

Invoke-Chat '/clear @s'
Invoke-Chat '/function gfbuilding/clear'
Start-Sleep -Seconds 2
Save-Screenshot 'selftest' | Out-Null
