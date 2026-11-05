# Travellers' Gear movement (U12), client: a real player wearing Travellers' wings (double jump,
# side step, gradual glide, built-in high jump) and boots (water walk, built-in step up), equipped by
# the selftest tgmove.e2e.equip, uses them with real key presses and the client mod measures each
# move (one "tgmove: ..." line per move, src/client/tgmove/TgMoveClient.cpp), compared with TF:
#   double jump  SPACE, then SPACE in the air: the second jump rises 0.62 (Jump Boost II) ->
#                about 2.5 blocks above where it started (TF: jumpFromGround with the wings' boost)
#   sidestep     A, A within 4 ticks on the ground: a 1.6 push left, about 3.5 blocks (ground friction
#                0.546: 1.6 / 0.454), TF's "5 blocks" comment counts the walking too
#   glide        SHIFT held while falling: y velocity x 5/6 each tick -> about 0.36 blocks per tick
#   water walk   the player stands on a water source at its half-block stable shape (y + 0.5)
#   step up      walking into a full block steps up 1.0 (step height + 0.5)
# remote-mod runs the same against BDS (the companion packets go over the network, the host logs
# the double jump and the sidestep); remote-vanilla checks the fallback: SPACE in the air makes the
# server push the double jump (SetActorMotionPacket) and the client stays connected.
# Screenshots: out/tgmove*/{start,double-jump,sidestep,glide,water-walk,step-up}.png
$remote     = $script:Mode -ne 'local'
$hostPrefix = if ($remote) { '\[host\].*' } else { '' }
$client     = '^(?!\[host\]).*'

function Hold-Key([int]$vk, [int]$ms) {
    Assert-GameForeground
    [BsE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds $ms; [BsE2E.Native]::Key([uint16]$vk, $true)
}
$VK_SPACE = 0x20; $VK_SHIFT = 0x10; $VK_A = 0x41; $VK_W = 0x57

Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Save-Screenshot 'start' | Out-Null

# the test area: x 300..320, z 300..320 of the flat test world (grass at y -61)
Invoke-Chat '/gamemode survival'
Invoke-Chat '/effect @s resistance 600 4 true'
Invoke-Teleport '/tp @s 310.5 -60 310.5 0 0' # load the area's chunks before filling it
Start-Sleep -Seconds 3
Invoke-Chat '/fill 298 -60 296 322 -40 322 air'
Invoke-Chat '/fill 298 -61 296 322 -61 322 grass_block'
Invoke-Chat '/setblock 305 -60 300 stone'
Invoke-Chat '/fill 310 -61 300 314 -61 306 water'
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "tgmove.e2e.equip"'
$done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 60 -Since $mark
if ($done -notmatch 'pass=1 fail=0') { throw "tgmove.e2e.equip: $done" }
$results = [ordered]@{}
$script:tgFailures = @()
function Add-Failure([string]$m) { $script:tgFailures += $m; Write-Step "FAIL: $m" }

# ---- double jump ----
Invoke-Teleport '/tp @s 300.5 -60 302.5 0 0'
Start-Sleep -Seconds 2
Focus-Game
$mark = Get-LogMark
Hold-Key $VK_SPACE 80
Start-Sleep -Milliseconds 250
Hold-Key $VK_SPACE 80
Start-Sleep -Milliseconds 150
Save-Screenshot 'double-jump' | Out-Null
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
    Invoke-Chat '/twilightforest selftest "tgmove.e2e.lift"'
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
$results['double jump height'] = "$h blocks (TF about 2.5)"
if ($h -lt 2.0 -or $h -gt 3.0) { Add-Failure "double jump peak $h blocks, TF's is about 2.5 (0.62 with Jump Boost II)" }
if ($remote) { Wait-LogLine '\[host\].*tgmove: .* double jumped \(vy' 20 -Since $mark | Out-Null }

# ---- sidestep (A twice within 4 ticks) ----
try {
Invoke-Teleport '/tp @s 300.5 -60 304.5 0 0'
Start-Sleep -Seconds 3 # the 40 tick cooldown since the teleport is not relevant: last sidestep long ago
Focus-Game
$mark = Get-LogMark
Hold-Key $VK_A 70
Start-Sleep -Milliseconds 60
Hold-Key $VK_A 70
$side = Wait-LogLine "${client}tgmove: sidestep (left|right)" 10 -Since $mark
if ($side -notmatch 'sidestep left') { Add-Failure "A A should sidestep left: '$side'" }
$moved = Wait-LogLine "${client}tgmove: sidestep moved ([0-9.]+) blocks" 10 -Since $mark
if ($moved -notmatch 'moved ([0-9.]+) blocks') { throw "no sidestep distance in '$moved'" }
$d = [double]$Matches[1]
$results['sidestep distance'] = "$d blocks (TF 1.6 / 0.454 = 3.5)"
if ($d -lt 2.5 -or $d -gt 6.0) { Add-Failure "sidestep moved $d blocks, TF's push 1.6 gives about 3.5" }
Save-Screenshot 'sidestep' | Out-Null
if ($remote) { Wait-LogLine '\[host\].*tgmove: .* sidestepped left' 20 -Since $mark | Out-Null }
} catch { Add-Failure "sidestep: $_" }

# ---- gradual glide (SHIFT held while falling) ----
try {
# hold SHIFT on the ground, then the selftest tgmove.e2e.lift lifts the player 35 blocks (typing a
# command would release the key)
Invoke-Teleport '/tp @s 300.5 -60 310.5 0 0'
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "tgmove.e2e.lift"'
Focus-Game
[BsE2E.Native]::Key([uint16]$VK_SHIFT, $false)
try {
Wait-LogLine 'tgmove.e2e.lift: lifted' 20 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'glide' | Out-Null
$glide = Wait-LogLine "${client}tgmove: glide ended after (\d+) ticks, sink ([0-9.]+) blocks per tick" 20 -Since $mark
} finally { [BsE2E.Native]::Key([uint16]$VK_SHIFT, $true) }
if ($glide -notmatch 'after (\d+) ticks, sink ([0-9.]+)') { throw "no glide line in '$glide'" }
$ticks = [int]$Matches[1]; $sink = [double]$Matches[2]
$results['glide sink rate'] = "$sink blocks/tick over $ticks ticks (TF terminal 0.357)"
if ($ticks -lt 40) { Add-Failure "the glide lasted only $ticks ticks" }
if ($sink -lt 0.25 -or $sink -gt 0.40) { Add-Failure "glide sink $sink blocks per tick, TF's terminal is 0.357" }
} catch { Add-Failure "glide: $_" }

# ---- water walk ----
try {
$mark = Get-LogMark
Invoke-Teleport '/tp @s 312.5 -59 301.5 0 0'
$ww = Wait-LogLine "${client}tgmove: walking on water at y (-?[0-9.]+)" 10 -Since $mark
if ($ww -notmatch 'at y (-?[0-9.]+)') { throw "no water walk line in '$ww'" }
$y = [double]$Matches[1]
$results['water walk'] = "stands at y $y (TF: source y -61 + 0.5 = -60.5)"
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
$results['step up'] = "$($Matches[1]) blocks (TF step height 0.6 + 0.5)"
Save-Screenshot 'step-up' | Out-Null
} catch { Add-Failure "step up: $_" }

foreach ($k in $results.Keys) { Write-Step "${k}: $($results[$k])" }
if ($script:tgFailures.Count -gt 0) { throw ($script:tgFailures -join '; ') }
