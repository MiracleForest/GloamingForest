# U8 (weapons): TF's weapons and throwables on a real client (post-merge acceptance; workers do not
# run it). The selftest weapons.client_showcase fills the hotbar and inventory with the 17 items
# (icons, the knightmetal shield's derived icon, rarity colours), throws a block and chain (the
# spiked ball; with the client mod its chain links to the player's eye) and a cube of annihilation
# (spinning, annihilate particles), and drops an ice bomb beside the player (packed ice, snow
# guardian zone). Every client draws the projectiles as RP entities; the chain links and the bow
# FOV zoom are the client mod's.
# Screenshots: out/weapons*/weapons-items-<mode>.png (hotbar), weapons-chain-<mode>.png,
# weapons-cube-<mode>.png, weapons-icebomb-<mode>.png. Local mode also runs the weapons.* selftests.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'weapons.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}weapons\.client_showcase: items ready" 90 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "weapons-items-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}weapons\.client_showcase: chain ready" 60 -Since $mark | Out-Null
Save-Screenshot "weapons-chain-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}weapons\.client_showcase: cube ready" 60 -Since $mark | Out-Null
Save-Screenshot "weapons-cube-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}weapons\.client_showcase: ice bomb ready" 60 -Since $mark | Out-Null
Save-Screenshot "weapons-icebomb-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 60 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'weapons.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'weapons.client_showcase failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'weapons.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) weapons selftest(s) failed" }
    if ($r.Pass -lt 13) { throw "expected at least 13 weapons selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
