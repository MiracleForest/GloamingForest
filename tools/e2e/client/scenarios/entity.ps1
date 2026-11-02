# F8 (entity): the boss bar on a real client (spike S4). The selftest entity.client_fixture shows
# the fixture boss gf_test:boss next to the real player: an orange bar (0xFF8000), a colour none of
# Bedrock's 8 BossBarColor values has, with the Naga's NOTCHED_10 overlay, then a second fixture
# boss with 3 linked parts, then a boss that dies with a 100-tick death animation:
#   local / remote-mod   the client mod paints the bar in the exact RGB (TFBossBarPacket through
#                        ClientInstanceScreenModel::getBossBarColor) and gives the HUD bar's images
#                        the notched textures; the client logs "client boss bar <id> coloured
#                        #FF8000" and "... notched 10", the synced string list ("client entity data
#                        ... gf_test:heads") and the death time ("client boss <id> dying"): required;
#                        the screenshot must show the 9 dividers of NOTCHED_10 on the orange bar
#                        (counted in its pixels: darker columns along the bar's middle row).
#   remote-vanilla       the vanilla client shows the nearest vanilla colour (red) and never gets a
#                        companion packet.
# Screenshots: out/entity/boss-bar-<mode>.png, boss-parts-<mode>.png (the parts render on every
# client as ordinary actors) and boss-dying-<mode>.png (the dead body 2 s after the death, where
# Bedrock alone removes it after 1 s). Local mode also runs every entity selftest on the
# integrated server (the client build's hooks).
# The dividers of a notched bar in a screenshot: the orange bar's brightest row in the top of the
# image, then the runs of pixels clearly darker than the row's median inside the bar.
function Measure-BarDividers([string]$png) {
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($png)
    try {
        $top = [int]($bmp.Height * 0.12)
        $best = $null
        for ($y = 0; $y -lt $top; $y++) {
            $n = 0; $x0 = -1; $x1 = -1; $rs = [System.Collections.Generic.List[int]]::new()
            for ($x = [int]($bmp.Width * 0.2); $x -lt [int]($bmp.Width * 0.8); $x++) {
                $c = $bmp.GetPixel($x, $y)
                if ($c.R -gt 60 -and $c.R -gt $c.G * 1.5 -and $c.G -gt $c.B * 2 -and $c.B -lt 40) { $n++; $rs.Add($c.R); if ($x0 -lt 0) { $x0 = $x }; $x1 = $x }
            }
            if ($n -lt 100) { continue }
            # the bar's brightest row by its median (its middle rows carry the dividers; the top and
            # bottom rows do not)
            $rs.Sort(); $mid = $rs[[int]($rs.Count / 2)]
            if (-not $best -or $mid -gt $best.Mid) { $best = [pscustomobject]@{ N = $n; Y = $y; X0 = $x0; X1 = $x1; Mid = $mid } }
        }
        if (-not $best -or $best.N -lt 100) { return [pscustomobject]@{ Found = $false; Dividers = 0 } }
        $reds = @(for ($x = $best.X0 + 4; $x -le $best.X1 - 4; $x++) { [int]$bmp.GetPixel($x, $best.Y).R })
        $median = ($reds | Sort-Object)[[int]($reds.Count / 2)]
        $runs = 0; $in = $false
        foreach ($r in $reds) { $dark = $r -lt $median * 0.75; if ($dark -and -not $in) { $runs++ }; $in = $dark }
        [pscustomobject]@{ Found = $true; Row = $best.Y; From = $best.X0; To = $best.X1; Median = $median; Dividers = $runs }
    } finally { $bmp.Dispose() }
}

Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }
$clientLine = if ($remote) { '^(?!\[host\]).*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

# The fixture runs on the server (the integrated one in local mode, the host otherwise) and holds
# each boss for 20 seconds; the screenshots are taken meanwhile.
$mark = Get-LogMark
$command = Format-SelfTestCommand 'entity.client_fixture'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}entity\.client_fixture: bar boss ready" 90 -Since $mark | Out-Null
if ($script:Mode -eq 'remote-vanilla') {
    Start-Sleep -Seconds 4
    if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
    if (Get-LeviLog -Since $mark | Select-String -Pattern '^(?!\[host\]).*client boss bar' -Quiet) { throw 'the vanilla client applied a companion colour' }
} else {
    Wait-LogLine "${clientLine}client boss bar -?\d+ received \(#FF8000\)" 30 -Since $mark | Out-Null
    $hit = Wait-LogLine "${clientLine}client boss bar -?\d+ coloured #FF8000" 30 -Since $mark
    Write-Step "exact RGB applied: $hit"
    # the bar's overlay (NOTCHED_10, the Naga's): the HUD bar's images got the notched textures
    $notch = Wait-LogLine "${clientLine}client boss bar -?\d+ notched 10 \(HUD bar" 30 -Since $mark
    Write-Step "notched textures applied: $notch"
    # a synced value (TFDataSerializers.STRING_LIST) reached the client mod's readers
    Wait-LogLine "${clientLine}client entity data -?\d+ gf_test:heads = \[`"left`",`"center`",`"right`"\]" 30 -Since $mark | Out-Null
}
Start-Sleep -Seconds 2
$barShot = Save-Screenshot "boss-bar-$($script:Mode)"
$dividers = Measure-BarDividers $barShot
Write-Step "bar in boss-bar-$($script:Mode).png: found $($dividers.Found), row $($dividers.Row), x $($dividers.From)..$($dividers.To), $($dividers.Dividers) dividers"
if ($script:Mode -eq 'remote-vanilla') {
    if ($dividers.Dividers -ge 9) { throw 'a vanilla client shows notches: the client mod must not be loaded' }
} elseif (-not $dividers.Found -or $dividers.Dividers -lt 9) {
    throw "the NOTCHED_10 bar shows $($dividers.Dividers) dividers in the screenshot, expected 9"
}
Wait-LogLine "${hostLine}entity\.client_fixture: parts boss ready" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot "boss-parts-$($script:Mode)" | Out-Null
# a boss with a 100-tick death animation: its body is still there 2 seconds after its death
Wait-LogLine "${hostLine}entity\.client_fixture: dying boss ready" 60 -Since $mark | Out-Null
Save-Screenshot "boss-dying-$($script:Mode)" | Out-Null
if ($script:Mode -ne 'remote-vanilla') { Wait-LogLine "${clientLine}client boss -?\d+ dying \(death time" 10 -Since $mark | Out-Null }
Wait-LogLine "${hostLine}boss: gf_test:boss left \(peaceful\)" 60 -Since $mark | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 60 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'entity.client_fixture'
if (-not (Register-SelfTestResult $r)) { throw 'entity.client_fixture failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'entity.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) entity selftest(s) failed" }
    if ($r.Pass -lt 30) { throw "expected at least 30 entity selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
