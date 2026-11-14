# Travellers' Gear movement (U12), client: a real player (tagged gf_e2e.tgmove, the only players the
# e2e selftests touch) wearing Travellers' wings (double jump, side step, gradual glide, agile
# ranger, built-in high jump), vest (built-in swift swim) and boots (water walk, unrestrained,
# built-in step up), equipped by the selftest tgmove.e2e.equip, uses them with real key presses and
# the client mod measures each move (one "tgmove: ..." line per move, src/client/tgmove), compared
# with upstream:
#   double jump  SPACE, then SPACE in the air: the second jump rises 0.62 (Jump Boost II) ->
#                about 2.5 blocks above where it started; the client computes the wings' DOUBLE_JUMP pose and
#                the double_jump particles fall from them (screenshots right after the jump)
#   sidestep     A, A within 4 ticks on the ground: a 1.6 push left, about 3.5 blocks
#   glide        SHIFT held while falling: y velocity x 5/6 each tick -> about 0.36 blocks per tick
#   water walk   the player stands on a water source at its half-block stable shape (y + 0.5)
#   step up      walking into a full block steps up 1.0 (step height + 0.5)
#   then each walk twice, with the gear and with tgmove.e2e.plain (no agile ranger, unrestrained or
#   vest; straight ahead instead), and compares the speeds:
#   agile ranger drawing a bow while walking: about full walking speed, not the bow's slowdown
#   unrestrained soul sand and cobweb at walking speed; the honey block's jump at full height
#   swift swim   swimming with Depth Strider III is faster than without
#   straight ahead walking forward x1.4, the strafe impulse / 1.4, the FOV compensated (the FOV
#                factor walking with straight ahead equals the one walking without it)
#   unrestrained packed ice and slime at walking speed, the slide after letting W go on ice as short
#                as on grass (friction 0.6, no slime step-on slowdown)
#   glide        W held while gliding with SHIFT: upstream lifts the sneak slowdown (speedUpControlled-
#                WhileSneaking), the glide moves at air speed (about 0.2 blocks per tick), not 0.3 of it
# remote-mod runs the same against BDS (the companion packets go over the network, the host logs
# the double jump and the sidestep); remote-vanilla checks the fallback: SPACE in the air makes the
# server push the double jump (SetActorMotionPacket) and the client stays connected.
# The wings: with the client mod the wings' attachable plays U12's animation (the pack patch adds it
# as the game reads the attachable; "pack patch: patched ...travellers_wings.json"), third person
# screenshots of the IDLE, DOUBLE_JUMP, SIDESTEP and FALL_SLOW poses, and a first person screenshot
# with no wings in view; remote-vanilla reads the attachable unchanged (no patch line, a clean
# content log with a wearer in view).
# The honey jump with unrestrained: 3 jumps, each 2.52 blocks high as upstream's (Jump Boost II, no honey
# jump factor), made up once (by the server copy, which tells the client mod).
# Screenshots: out/tgmove*/{start,wings-idle,double-jump-1,double-jump-2,double-jump-3,sidestep,
# glide,first-person,water-walk,step-up,bow}.png
$remote     = $script:Mode -ne 'local'
$hostPrefix = if ($remote) { '\[host\].*' } else { '' }
$client     = '^(?!\[host\]).*'

function Hold-Key([int]$vk, [int]$ms) {
    Assert-GameForeground
    [BsE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds $ms; [BsE2E.Native]::Key([uint16]$vk, $true)
}
$VK_SPACE = 0x20; $VK_SHIFT = 0x10; $VK_A = 0x41; $VK_W = 0x57; $VK_F5 = 0x74

function Invoke-E2eSelfTest([string]$name) {
    $mark = Get-LogMark
    Invoke-Chat "/gloamingforest selftest `"$name`""
    $done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 60 -Since $mark
    if ($done -notmatch 'pass=1 fail=0') { throw "${name}: $done" }
    if (-not (Get-LeviLog | Select-Object -Skip $mark | Select-String -Pattern 'equipped 1 players' -Quiet)) { throw "${name} equipped nobody (the gf_e2e.tgmove tag)" }
}

Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Save-Screenshot 'start' | Out-Null
if ($script:Mode -eq 'remote-vanilla') {
    if (Get-LeviLog | Select-String -Pattern "${client}pack patch: patched" -Quiet) { throw 'a vanilla client patched the wings attachable' }
} else {
    # the attachable is read at game start or when the world's packs load
    Wait-LogLine "${client}pack patch: patched .*travellers_wings\.json" 30 | Out-Null
}

# the test area: x 298..322, z 296..322 of the flat test world (grass at y -61)
Invoke-Chat '/gamemode survival'
Invoke-Chat '/effect @s resistance 600 4 true'
Invoke-Chat '/effect @s saturation 600 1 true'
Invoke-Teleport '/tp @s 310.5 -60 310.5 0 0' # load the area's chunks before filling it
Start-Sleep -Seconds 3
Invoke-Chat '/fill 298 -60 296 322 -40 322 air'
Invoke-Chat '/fill 298 -61 296 322 -61 322 grass_block'
Invoke-Chat '/setblock 305 -60 300 stone'
Invoke-Chat '/fill 310 -61 300 314 -61 306 water'
Invoke-Chat '/fill 316 -63 300 321 -61 316 water'      # the swimming pool, 3 deep
Invoke-Chat '/fill 316 -60 300 321 -60 316 glass'      # its lid: the swimmer stays under water (no water walking)
Invoke-Chat '/fill 298 -61 314 308 -61 315 soul_sand'
Invoke-Chat '/fill 298 -60 318 308 -59 319 web'
Invoke-Chat '/setblock 300 -61 322 honey_block'
Invoke-Chat '/fill 298 -61 308 315 -61 309 packed_ice' # unrestrained: friction 0.6 on ice
Invoke-Chat '/fill 298 -61 311 315 -61 312 slime'      # unrestrained: no slime slowdown
Invoke-Chat '/tag @s add gf_e2e.tgmove'
Invoke-E2eSelfTest 'tgmove.e2e.equip'
$results = [ordered]@{}
$script:tgFailures = @()
function Add-Failure([string]$m) { $script:tgFailures += $m; Write-Step "FAIL: $m" }

# the speed of the walk run `what` ("tgmove: speed <what> <v> blocks per tick") since $mark
function Measure-Walk([string]$what, [string]$tp, [int]$ms, [switch]$Bow, [switch]$Strafe) {
    Invoke-Teleport $tp
    Start-Sleep -Seconds 1
    Focus-Game
    $mark = Get-LogMark
    $script:walkMark = $mark
    if ($Bow) { [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 300 }
    try {
        [BsE2E.Native]::Key([uint16]$VK_W, $false)
        if ($Strafe) { Start-Sleep -Milliseconds 300; [BsE2E.Native]::Key([uint16]$VK_A, $false) }
        Start-Sleep -Milliseconds $ms
        if ($Bow) { Save-Screenshot 'bow' | Out-Null }
    } finally {
        if ($Strafe) { [BsE2E.Native]::Key([uint16]$VK_A, $true) }
        [BsE2E.Native]::Key([uint16]$VK_W, $true)
        if ($Bow) { [BsE2E.Native]::MouseButton(1, $true) }
    }
    $line = Wait-LogLine "${client}tgmove: speed $what ([0-9.]+) blocks per tick over (\d+) ticks" 15 -Since $mark
    if ($line -notmatch "speed $what ([0-9.]+) blocks per tick over (\d+)") { throw "no $what speed in '$line'" }
    [double]$Matches[1]
}
# the slide (blocks) after letting W go on $block since $mark
function Get-Slide([string]$block, [int]$mark) {
    $line = Wait-LogLine "${client}tgmove: slid ([0-9.]+) blocks on $block" 10 -Since $mark
    if ($line -notmatch 'slid ([0-9.]+) blocks') { throw "no slide on $block in '$line'" }
    [double]$Matches[1]
}
function Measure-HoneyJump {
    Invoke-Teleport '/tp @s 300.5 -60 322.5 180 0'
    Start-Sleep -Seconds 1
    Focus-Game
    $mark = Get-LogMark
    $script:honeyMark = $mark
    Hold-Key $VK_SPACE 80
    $line = Wait-LogLine "${client}tgmove: jump peak ([0-9.]+) blocks from minecraft:honey_block" 10 -Since $mark
    if ($line -notmatch 'peak ([0-9.]+) blocks') { throw "no honey jump in '$line'" }
    [double]$Matches[1]
}

# ---- the wings at rest, third person (upstream's idle pose: wings swept back and out) ----
Invoke-Teleport '/tp @s 300.5 -60 302.5 0 0'
Start-Sleep -Seconds 2
Focus-Game
Hold-Key $VK_F5 60 # F5: third person, the wings and their particles in view
Start-Sleep -Milliseconds 800
Save-Screenshot 'wings-idle' | Out-Null

# ---- double jump ----
Focus-Game
$mark = Get-LogMark
Hold-Key $VK_SPACE 80
Start-Sleep -Milliseconds 250
Hold-Key $VK_SPACE 60
Save-Screenshot 'double-jump-1' | Out-Null # the particles leave the wings within a few ticks
Save-Screenshot 'double-jump-2' | Out-Null
Start-Sleep -Milliseconds 100
Save-Screenshot 'double-jump-3' | Out-Null
Hold-Key $VK_F5 60; Start-Sleep -Milliseconds 200; Hold-Key $VK_F5 60 # back to first person
if ($script:Mode -eq 'remote-vanilla') {
    Wait-LogLine '\[host\].*tgmove: .* double jumped \(vy [0-9.]+, vanilla fallback\)' 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    if (-not (Get-GameProcess)) { throw 'the game closed after the double jump fallback' }
    $screen = Get-ClientScreen
    if ($screen -match 'disconnect') { throw "the client was disconnected after the double jump fallback ($screen)" }
    if (Get-LeviLog | Select-String -Pattern "${client}tgmove: double jump at" -Quiet) { throw 'a vanilla client ran the client mod' }
    # the glide fallback: sneaking in the air gets Slow Falling
    Invoke-Teleport '/tp @s 300.5 -60 310.5 0 0'
    $mark = Get-LogMark
    Invoke-Chat '/gloamingforest selftest "tgmove.e2e.lift"'
    Focus-Game
    [BsE2E.Native]::Key([uint16]$VK_SHIFT, $false)
    try {
        Wait-LogLine '\[host\].*tgmove: .* glides \(Slow Falling\)' 20 -Since $mark | Out-Null
    } finally { [BsE2E.Native]::Key([uint16]$VK_SHIFT, $true) }
    Write-Step 'vanilla fallbacks: double jump by motion packet and Slow Falling glide, no disconnect'
    Save-Screenshot 'glide' | Out-Null
    return
}
Wait-LogLine "${client}tgmove: double jump at y" 20 -Since $mark | Out-Null
$peak = Wait-LogLine "${client}tgmove: double jump peak ([0-9.]+) blocks" 20 -Since $mark
if ($peak -notmatch 'peak ([0-9.]+) blocks') { throw "no double jump peak in '$peak'" }
$h = [double]$Matches[1]
$results['double jump height'] = "$h blocks (upstream about 2.5)"
if ($h -lt 2.0 -or $h -gt 3.0) { Add-Failure "double jump peak $h blocks, upstream's is about 2.5 (0.62 with Jump Boost II)" }
try {
    $pose = Wait-LogLine "${client}tgmove: wings pose double_jump \(right ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)" 10 -Since $mark
    $results['wings pose'] = ($pose -replace '.*tgmove: ', '') + ' (upstream DOUBLE_JUMP target -22.9 -45.8 -5.7, eased in)'
} catch { Add-Failure "no DOUBLE_JUMP wings pose: $_" }
if ($remote) { Wait-LogLine '\[host\].*tgmove: .* double jumped \(vy' 20 -Since $mark | Out-Null }

# ---- sidestep (A twice within 4 ticks), third person: the wings' SIDESTEP pose ----
try {
Invoke-Teleport '/tp @s 300.5 -60 304.5 0 0'
Start-Sleep -Seconds 3 # more than the 40 tick cooldown since the double jump's test
Focus-Game
Hold-Key $VK_F5 60
Start-Sleep -Milliseconds 400
$mark = Get-LogMark
try {
    Hold-Key $VK_A 70
    Start-Sleep -Milliseconds 60
    Hold-Key $VK_A 70
    Start-Sleep -Milliseconds 100
    Save-Screenshot 'sidestep' | Out-Null
} finally { Hold-Key $VK_F5 60; Start-Sleep -Milliseconds 200; Hold-Key $VK_F5 60 }
$side = Wait-LogLine "${client}tgmove: sidestep (left|right)" 10 -Since $mark
if ($side -notmatch 'sidestep left') { Add-Failure "A A should sidestep left: '$side'" }
$moved = Wait-LogLine "${client}tgmove: sidestep moved ([0-9.]+) blocks" 10 -Since $mark
if ($moved -notmatch 'moved ([0-9.]+) blocks') { throw "no sidestep distance in '$moved'" }
$d = [double]$Matches[1]
$results['sidestep distance'] = "$d blocks (upstream 1.6 / 0.454 = 3.5)"
if ($d -lt 2.5 -or $d -gt 6.0) { Add-Failure "sidestep moved $d blocks, upstream's push 1.6 gives about 3.5" }
try {
    Wait-LogLine "${client}tgmove: wings pose sidestep" 5 -Since $mark | Out-Null
} catch { Add-Failure "no SIDESTEP wings pose: $_" }
if ($remote) { Wait-LogLine '\[host\].*tgmove: .* sidestepped left' 20 -Since $mark | Out-Null }
} catch { Add-Failure "sidestep: $_" }

# ---- gradual glide (SHIFT held while falling) ----
try {
# hold SHIFT on the ground, then the selftest tgmove.e2e.lift lifts the player 35 blocks (typing a
# command would release the key)
Invoke-Teleport '/tp @s 300.5 -60 310.5 0 0'
$mark = Get-LogMark
Invoke-Chat '/gloamingforest selftest "tgmove.e2e.lift"'
Focus-Game
Hold-Key $VK_F5 60 # third person: the wings' FALL_SLOW flap
[BsE2E.Native]::Key([uint16]$VK_SHIFT, $false)
try {
Wait-LogLine 'tgmove.e2e.lift: lifted 1' 20 -Since $mark | Out-Null
[BsE2E.Native]::Key([uint16]$VK_W, $false) # forward while gliding: upstream moves at air speed despite the sneak key
Start-Sleep -Milliseconds 1500
Save-Screenshot 'glide' | Out-Null
$glide = Wait-LogLine "${client}tgmove: glide ended after (\d+) ticks, sink ([0-9.]+) blocks per tick" 20 -Since $mark
} finally {
    [BsE2E.Native]::Key([uint16]$VK_W, $true)
    [BsE2E.Native]::Key([uint16]$VK_SHIFT, $true)
    Hold-Key $VK_F5 60; Start-Sleep -Milliseconds 200; Hold-Key $VK_F5 60
}
if ($glide -notmatch 'after (\d+) ticks, sink ([0-9.]+)') { throw "no glide line in '$glide'" }
$ticks = [int]$Matches[1]; $sink = [double]$Matches[2]
$results['glide sink rate'] = "$sink blocks/tick over $ticks ticks (upstream terminal 0.357)"
if ($ticks -lt 40) { Add-Failure "the glide lasted only $ticks ticks" }
if ($sink -lt 0.25 -or $sink -gt 0.40) { Add-Failure "glide sink $sink blocks per tick, upstream's terminal is 0.357" }
$gs = Get-LeviLog | Select-Object -Skip $mark | Select-String -Pattern "${client}tgmove: speed glide ([0-9.]+) blocks per tick" | Select-Object -Last 1
if (-not $gs -or $gs.Line -notmatch 'speed glide ([0-9.]+)') { Add-Failure 'no glide speed line' }
else {
    $results['glide forward with sneak'] = "$($Matches[1]) blocks/tick (upstream: air speed, about 0.2; the sneak slowdown would give 0.07)"
    if ([double]$Matches[1] -lt 0.12) { Add-Failure "gliding forward with SHIFT moves $($Matches[1]) blocks per tick: the sneak slowdown was not lifted" }
}
$si = Get-LeviLog | Select-Object -Skip $mark | Select-String -Pattern "${client}tgmove: gliding with sneak, input length .*" | Select-Object -Last 1
if ($si) { $results['sneak input'] = $si.Line -replace '.*tgmove: ', '' }
} catch { Add-Failure "glide: $_" }

# ---- first person: upstream draws no armour there, so no wings in view (looking down at the body) ----
Invoke-Teleport '/tp @s 300.5 -60 302.5 0 70'
Start-Sleep -Seconds 1
Save-Screenshot 'first-person' | Out-Null

# ---- water walk (first person: no wings in view) ----
try {
$mark = Get-LogMark
Invoke-Teleport '/tp @s 312.5 -59 300.5 0 40'
$ww = Wait-LogLine "${client}tgmove: walking on water at y (-?[0-9.]+)" 10 -Since $mark
if ($ww -notmatch 'at y (-?[0-9.]+)') { throw "no water walk line in '$ww'" }
$y = [double]$Matches[1]
$results['water walk'] = "stands at y $y (upstream: source y -61 + 0.5 = -60.5)"
if ([Math]::Abs($y - (-60.5)) -gt 0.01) { Add-Failure "water walk surface $y, expected -60.5" }
Focus-Game
Hold-Key $VK_W 1200
Start-Sleep -Milliseconds 300
Save-Screenshot 'water-walk' | Out-Null
} catch { Add-Failure "water walk: $_" }

# ---- step up (walk into a full block) ----
try {
Invoke-Teleport '/tp @s 305.5 -60 298.5 0 0'
Start-Sleep -Seconds 1
Focus-Game
$mark = Get-LogMark
Start-Sleep -Milliseconds 200
Hold-Key $VK_W 1200
$step = Wait-LogLine "${client}tgmove: stepped up ([0-9.]+) blocks" 10 -Since $mark
if ($step -notmatch 'stepped up ([0-9.]+)') { throw "no step up line in '$step'" }
$results['step up'] = "$($Matches[1]) blocks (upstream step height 0.6 + 0.5)"
Save-Screenshot 'step-up' | Out-Null
} catch { Add-Failure "step up: $_" }

# ---- the walks with the gear, then without (tgmove.e2e.plain) ----
$with = @{}; $without = @{}
try {
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 bow'
    Invoke-Chat '/give @s arrow 64'
    $with.forward  = Measure-Walk 'forward' '/tp @s 302.5 -60 296.5 0 0' 1500
    $with.grassSlide = Get-Slide 'minecraft:grass_block' $script:walkMark
    $with.fov      = Wait-LogLine "${client}tgmove: fov forward in ([0-9.]+) out ([0-9.]+)" 10 -Since $script:walkMark
    $with.ice      = Measure-Walk 'ice' '/tp @s 299.5 -60 308.5 270 0' 1000
    $with.iceSlide = Get-Slide 'minecraft:packed_ice' $script:walkMark
    $with.slime    = Measure-Walk 'slime' '/tp @s 299.5 -60 311.5 270 0' 1000
    $with.bow      = Measure-Walk 'bow' '/tp @s 304.5 -60 296.5 0 0' 1500 -Bow
    $with.soul     = Measure-Walk 'soul_sand' '/tp @s 299.5 -60 314.5 270 0' 1500
    $with.web      = Measure-Walk 'cobweb' '/tp @s 299.5 -60 318.5 270 0' 1500
    $with.swim     = Measure-Walk 'water' '/tp @s 317.5 -62.5 301.5 0 0' 1500
    # three honey jumps with unrestrained: each at upstream's 2.52 blocks (Jump Boost II, no honey factor)
    $honeyJumps = @()
    foreach ($n in 1..3) {
        $honeyJumps += Measure-HoneyJump
        if ($script:Mode -ne 'remote-vanilla') {
            $made = @(Get-LeviLog | Select-Object -Skip $script:honeyMark | Select-String -Pattern "${client}tgmove: honey jump made up by the server")
            if ($made.Count -ne 1) { Add-Failure "honey jump $n made up $($made.Count) times on the client (once expected)" }
        }
        Start-Sleep -Milliseconds 500
    }
    $with.honey = ($honeyJumps | Measure-Object -Average).Average
    $results['unrestrained honey jumps'] = ($honeyJumps -join ', ') + ' blocks (upstream 2.52)'
    foreach ($h in $honeyJumps) { if ([Math]::Abs($h - 2.52) -gt 0.15) { Add-Failure "unrestrained honey jump $h blocks, upstream's is 2.52" } }
    Invoke-E2eSelfTest 'tgmove.e2e.plain'
    $without.forward = Measure-Walk 'forward' '/tp @s 302.5 -60 296.5 0 0' 1500 -Strafe
    $without.fov     = Wait-LogLine "${client}tgmove: fov forward in ([0-9.]+) out ([0-9.]+)" 10 -Since $script:walkMark
    $without.ice     = Measure-Walk 'ice' '/tp @s 299.5 -60 308.5 270 0' 1000
    $without.iceSlide = Get-Slide 'minecraft:packed_ice' $script:walkMark
    $without.slime   = Measure-Walk 'slime' '/tp @s 299.5 -60 311.5 270 0' 1000
    $without.bow     = Measure-Walk 'bow' '/tp @s 304.5 -60 296.5 0 0' 1500 -Bow
    $without.soul    = Measure-Walk 'soul_sand' '/tp @s 299.5 -60 314.5 270 0' 1500
    $without.web     = Measure-Walk 'cobweb' '/tp @s 299.5 -60 318.5 270 0' 1500
    $without.swim    = Measure-Walk 'water' '/tp @s 317.5 -62.5 301.5 0 0' 1500
    $without.honey   = Measure-HoneyJump
    $results['agile ranger'] = "bow draw walk $($with.bow) with, $($without.bow) without (walk $($with.forward))"
    if ($with.bow -lt 3 * $without.bow -or $with.bow -lt 0.85 * $with.forward) { Add-Failure "agile ranger: walking while drawing a bow $($with.bow) with, $($without.bow) without, walking $($with.forward)" }
    $results['unrestrained'] = "soul sand $($with.soul) / $($without.soul), cobweb $($with.web) / $($without.web), honey jump $($with.honey) / $($without.honey)"
    # upstream: walking speed on soul sand (Bedrock's own slowdown there is milder than Java's 0.4)
    if ($with.soul -lt 0.9 * $with.forward -or $with.soul -lt 1.1 * $without.soul) { Add-Failure "unrestrained on soul sand: $($with.soul) with, $($without.soul) without, walk $($with.forward)" }
    if ($with.web -lt 3 * $without.web) { Add-Failure "unrestrained in cobweb: $($with.web) with, $($without.web) without" }
    if ($with.honey -lt $without.honey + 0.5) { Add-Failure "unrestrained honey jump: $($with.honey) with, $($without.honey) without" }
    $results['swift swim'] = "swim $($with.swim) with the vest, $($without.swim) without"
    if ($with.swim -lt 1.4 * $without.swim) { Add-Failure "swift swim: $($with.swim) with the vest, $($without.swim) without" }
    $ratio = $without.forward / $with.forward
    $results['straight ahead'] = "forward walk x$([Math]::Round($ratio, 3)) ($($without.forward) / $($with.forward))"
    if ($ratio -lt 1.25 -or $ratio -gt 1.55) { Add-Failure "straight ahead: forward x$ratio, upstream x1.4" }
    $sa = Get-LeviLog | Select-String -Pattern "${client}tgmove: straight ahead strafe ([-0-9.]+) -> ([-0-9.]+)" | Select-Object -Last 1
    if (-not $sa -or $sa.Line -notmatch 'strafe ([-0-9.]+) -> ([-0-9.]+)') { Add-Failure 'no straight ahead strafe line' }
    else {
        # upstream divides the raw strafe impulse by 1.4 and normalizes after: W + A (Bedrock 0.707, 0.707) -> 0.581
        $raw = [double]$Matches[1]; $got = [double]$Matches[2]
        $k = if ([Math]::Abs($raw) -gt 0.7 -and [Math]::Abs($raw) -lt 0.71) { 1.0 } else { $raw }
        $want = if ($k -eq 1.0) { [Math]::Sign($raw) * (1 / 1.4) / [Math]::Sqrt(1 / 1.96 + 1) } else { $raw / 1.4 }
        $results['straight ahead strafe'] = "$raw -> $got (upstream $([Math]::Round($want, 3)))"
        if ([Math]::Abs($got - $want) -gt 0.01) { Add-Failure "straight ahead strafe $raw -> $got, upstream $want" }
    }
    $fov = Get-LeviLog | Select-String -Pattern "${client}tgmove: straight ahead fov x([0-9.]+)" | Select-Object -Last 1
    if (-not $fov) { Add-Failure 'the FOV was not compensated for straight ahead' } else { $results['straight ahead fov'] = $fov.Line -replace '.*tgmove: ', '' }
    # the FOV walking forward with straight ahead (compensated) is the FOV walking without it
    if ($with.fov -match 'in ([0-9.]+) out ([0-9.]+)') { $plainFov = [double]$Matches[2] } else { $plainFov = 0 }
    if ($without.fov -match 'in ([0-9.]+) out ([0-9.]+)') { $saRaw = [double]$Matches[1]; $saFov = [double]$Matches[2] } else { $saRaw = 0; $saFov = 0 }
    $results['straight ahead fov factor'] = "walking $plainFov without, $saFov with straight ahead (engine $saRaw before the compensation)"
    if ([Math]::Abs($saFov - $plainFov) -gt 0.02) { Add-Failure "straight ahead FOV $saFov, walking without it $plainFov" }
    $results['unrestrained ice'] = "walk $($with.ice) / $($without.ice) (grass $($with.forward)), slide $($with.iceSlide) / $($without.iceSlide) (grass $($with.grassSlide))"
    if ([Math]::Abs($with.ice - $with.forward) -gt 0.15 * $with.forward) { Add-Failure "unrestrained on packed ice walks $($with.ice), on grass $($with.forward)" }
    if ([Math]::Abs($with.iceSlide - $with.grassSlide) -gt 0.4) { Add-Failure "unrestrained slide on packed ice $($with.iceSlide), on grass $($with.grassSlide)" }
    if ($without.iceSlide -lt $with.iceSlide + 0.5) { Add-Failure "without unrestrained the ice slide should be longer: $($without.iceSlide) / $($with.iceSlide)" }
    $results['unrestrained slime'] = "walk $($with.slime) with, $($without.slime) without (grass $($with.forward))"
    if ([Math]::Abs($with.slime - $with.forward) -gt 0.15 * $with.forward) { Add-Failure "unrestrained on slime walks $($with.slime), on grass $($with.forward)" }
} catch { Add-Failure "walks: $_" }

foreach ($k in $results.Keys) { Write-Step "${k}: $($results[$k])" }
if ($script:tgFailures.Count -gt 0) { throw ($script:tgFailures -join '; ') }
