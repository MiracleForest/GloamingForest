# Uncrafting table and maps (U13), client side. What it shows (compare each with TF):
#   out/uncraftmaps/uncraftmaps-1-table.png        the uncrafting table block (TF's uncrafting_top /
#                                                 uncrafting_side textures), unpowered and, on an
#                                                 amethyst block with a redstone block, powered (the
#                                                 glowing sides)
#   out/uncraftmaps/uncraftmaps-2-screen.png       the table right-clicked: local / remote-mod the
#                                                 client mod's screen on TF's guigoblintinkering.png
#                                                 (the frame is compared pixel by pixel with the
#                                                 texture), remote-vanilla the form
#   out/uncraftmaps/uncraftmaps-3-ghost.png        an iron pickaxe shift-clicked into the input: the 3
#                                                 iron and 2 sticks as ghosts under the grey overlay
#                                                 in the assembly grid, the cost 3 (green: 30 levels)
#   out/uncraftmaps/uncraftmaps-4-taken.png        a ghost iron clicked: on the cursor, the rest in the
#                                                 assembly grid, the pickaxe used up
#   out/uncraftmaps/uncraftmaps-5-magic-map.png    a magic map made in the Twilight Forest and held:
#                                                 the biome colours and the landmark icons (TF's icons
#                                                 with the client mod, vanilla icons otherwise)
#   out/uncraftmaps/uncraftmaps-6-items.png        the 8 map items in the hotbar (TF's item textures)
# Local mode also runs the uncraftmaps selftests on the integrated server (biomepng's local-world
# path included).

function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}
$who = if ($script:Mode -eq 'local') { '@s' } else { '@a' }

if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'uncraftmaps.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) uncraftmaps selftest(s) failed" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Invoke-Build 'gamerule sendcommandfeedback false'

$y = -60 # the flat world's first air layer
$tx = 40; $tz = 70
# stand there first: setblock needs the chunks loaded
Invoke-Teleport "/tp $who $($tx + 2) $($y + 1) $($tz + 4) 180 25"
Start-Sleep -Seconds 3
Invoke-Build "setblock $tx $y $tz twilightforest:uncrafting_table"
Invoke-Build "setblock $($tx + 3) $($y - 1) $tz amethyst_block"
Invoke-Build "setblock $($tx + 3) $y $tz twilightforest:uncrafting_table"
Invoke-Build "setblock $($tx + 4) $y $tz redstone_block"
Invoke-Teleport "/tp $who $($tx + 2) $($y + 1) $($tz + 4) 180 25"
Start-Sleep -Seconds 2
Save-Screenshot 'uncraftmaps-1-table' | Out-Null

# the screen: stand south of the table and right-click it
Invoke-Build "clear $who"
Invoke-Build "give $who iron_pickaxe"
Invoke-Build "xp 30L $who"
Invoke-Teleport "/tp $who $($tx + 0.5) $y $($tz + 2.5) 180 30"
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
if ($script:Mode -eq 'remote-vanilla') {
    Wait-LogLine 'uncraftmaps: .* opened the uncrafting table' 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'uncraftmaps-2-screen' | Out-Null
    Close-Screen
} else {
    Wait-LogLine 'client screen: uncraftmaps.uncrafting opened' 20 -Since $mark | Out-Null
    $open = Wait-LogLine 'screen opened: .* panel=' 20 -Since $mark
    Start-Sleep -Seconds 1
    $png = Save-Screenshot 'uncraftmaps-2-screen'
    if ("$open" -notmatch 'panel=([-\d.]+),([-\d.]+) inv=[-\d.,]+ gui=([\d.]+)x([\d.]+)') { throw "no layout in '$open'" }
    $px = [double]$Matches[1]; $py = [double]$Matches[2]; $gw = [double]$Matches[3]; $gh = [double]$Matches[4]
    # the frame against TF's texture: points of the background away from slots, text and items
    $tex = Join-Path $script:RepoRoot 'assets/gen/import/import/rp/textures/twilightforest/gui/guigoblintinkering.png'
    Add-Type -AssemblyName System.Drawing
    $shot = [System.Drawing.Bitmap]::FromFile($png); $t = [System.Drawing.Bitmap]::FromFile($tex)
    try {
        $scale = $shot.Width / $gw
        $worst = 0; $n = 0
        foreach ($p in @(@(3, 3), @(170, 3), @(3, 160), @(170, 160), @(100, 70), @(30, 50), @(90, 12), @(140, 60), @(4, 80), @(172, 80), @(60, 76), @(120, 4))) {
            $sx = [int](($px + $p[0] + 0.5) * $scale); $sy = [int](($py + $p[1] + 0.5) * $scale)
            $a = $shot.GetPixel($sx, $sy); $b = $t.GetPixel($p[0], $p[1])
            $d = [Math]::Max([Math]::Abs($a.R - $b.R), [Math]::Max([Math]::Abs($a.G - $b.G), [Math]::Abs($a.B - $b.B)))
            $worst = [Math]::Max($worst, $d); $n++
        }
        Write-Step "uncrafting frame vs guigoblintinkering.png: $n points, worst channel difference $worst"
        if ($worst -gt 24) { throw "the uncrafting screen's frame differs from TF's texture (worst channel difference $worst)" }
    } finally { $shot.Dispose(); $t.Dispose() }
    $at = { param($gx, $gy) @((($px + $gx) / $gw), (($py + $gy) / $gh)) }
    # shift-click the hotbar's first slot (menu slot 47 at 8, 142) into the input
    $p = & $at 16 150
    Assert-GameForeground; [BsE2E.Native]::Key(0x10, $false); Start-Sleep -Milliseconds 100
    Click $p[0] $p[1] -Relative
    [BsE2E.Native]::Key(0x10, $true); Start-Sleep -Seconds 1
    Move-Mouse 0.02 0.02 -Relative
    Save-Screenshot 'uncraftmaps-3-ghost' | Out-Null
    # take the ghost iron at the assembly grid's first slot (62, 17)
    $p = & $at 70 25
    Click $p[0] $p[1] -Relative
    Start-Sleep -Seconds 1
    Save-Screenshot 'uncraftmaps-4-taken' | Out-Null
    Close-Screen
}

# the map items and a magic map in the Twilight Forest
Invoke-Build "clear $who"
foreach ($i in 'magic_map', 'maze_map', 'ore_map', 'magic_map_focus', 'maze_map_focus') { Invoke-Build "give $who twilightforest:$i" }
Start-Sleep -Seconds 1
Save-Screenshot 'uncraftmaps-6-items' | Out-Null
$mark = Get-LogMark
if ($script:Mode -eq 'local') { Invoke-Chat '/twilightforest tp' } else { Send-HostConsole "execute as @a at @s run twilightforest tp" }
Start-Sleep -Seconds 2
# the test world's Twilight Forest has no ground here: a platform high up, before the fall reaches the void
Invoke-Teleport "/tp $who 40.5 100 72.5"
if ($script:Mode -eq 'local') { Invoke-Chat '/fill 38 98 70 43 98 75 stone' } else { Send-HostConsole 'execute as @a at @s run fill 38 98 70 43 98 75 stone' }
Invoke-Teleport "/tp $who 40.5 99 72.5 0 30"
Start-Sleep -Seconds 4
Invoke-Build "replaceitem entity $who slot.hotbar 0 twilightforest:magic_map"
Press-Key '1'
# look up into the air and use the empty map (EmptyMagicMapItem#use)
Invoke-Teleport "/tp $who ~ ~ ~ 0 -45"
Start-Sleep -Seconds 1
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'uncraftmaps: magic map -?\d+ for' 30 -Since $mark | Out-Null
Start-Sleep -Seconds 6
Invoke-Teleport "/tp $who ~ ~ ~ 0 60"
Start-Sleep -Seconds 2
Save-Screenshot 'uncraftmaps-5-magic-map' | Out-Null
