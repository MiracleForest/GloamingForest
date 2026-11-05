# U14 (progression): the client side of TF's advancements. Screenshots:
#   advancement-tree.png     the advancement screen after the demo grants (root, progress_naga,
#                            hedge, hill1, twilight_hunter, progress_lich, naga_armors): the client
#                            mod's native tree laid out by TreeNodePosition (TF's tab, mazestone
#                            background, gold earned frames, grey unearned ones two levels deep,
#                            the connecting lines; local, remote-mod), the form list on a vanilla
#                            client (remote-vanilla)
#   advancement-hover.png    (client mod) hill1 hovered: the dimmed tree, the gold title bar and
#                            the green description with its argument ("Defeat a Redcap ...")
#   advancement-drag.png     (client mod) the tree dragged with the left button: nodes cut at the
#                            tree's edge (frames and icons clipped)
#   advancement-toast.png    progress_labyrinth earned: Java's "Goal Reached!" toast with the
#                            labyrinth miniature (client mod), the game's text toast, title green
#                            (vanilla)
#   locked-biome-toast.png   TF's LockedBiomeToast after the 60-step countdown in the dark forest
#                            (client mod: lich tower miniature, yellow "Biome Locked!", white "Check
#                            your advancements"); on a vanilla client the server's toast after the
#                            same countdown in the swamp (text only)
# Compare them with TF's advancement screen (vanilla's window, TF's mazestone tab background) and
# LockedBiomeToast (the advancement toast frame, icon at 6,8, text at 25,7 and 25,18).
#
# The demo selftests act only on players tagged gf_e2e_progression (they save the player's
# progress first and progression.demo_reset restores it); the tag is removed at the end.
$ready = if ($script:Mode -eq 'local') { 'GloamingForest world ready' } else { '\[host\].*GloamingForest world ready' }
Wait-LogLine $ready 120 | Out-Null
Save-Screenshot 'hud' | Out-Null

$failures = [System.Collections.Generic.List[string]]::new()
function Invoke-Checked([string]$Test) {
    $r = Invoke-SelfTest $Test
    if ($r.Fail -gt 0) { $failures.Add("$Test failed") }
    return $r
}

foreach ($t in 'progression.advancements', 'progression.restrictions', 'progression.trigger_criteria', 'progression.toasts_and_screen', 'progression.descriptions',
    'progression.tree_layout', 'progression.icon_variants') { Invoke-Checked $t | Out-Null }

Invoke-Chat '/tag @s add gf_e2e_progression'
Start-Sleep -Seconds 1
try {
    # the tree (or the form)
    Invoke-Checked 'progression.demo_grant' | Out-Null
    Start-Sleep -Seconds 6 # the grant toasts slide away
    $mark = Get-LogMark
    Invoke-Checked 'progression.demo_screen' | Out-Null
    if (Test-ClientTrace) {
        try { Wait-LogLine 'client screen: progression.advancements \(\d+ nodes, \d+ earned\)' 20 -Since $mark | Out-Null }
        catch { $failures.Add('the advancement screen did not open on the client') }
    } else {
        Start-Sleep -Seconds 3
    }
    Start-Sleep -Seconds 2
    Save-Screenshot 'advancement-tree' | Out-Null

    if (Test-ClientTrace) {
        # hover a node whose description takes arguments (the "with" list of its component): the
        # first of these in view; its hover names the argument and shows no %s
        Update-LeviLog
        $cells = @(Get-LeviLog -Since $mark | Select-String -Pattern 'client screen: progression\.advancements cell (\S+) at ([\d.]+) ([\d.]+)')
        $target = $null
        foreach ($want in @(@('twilightforest:hill1', 'Redcap'), @('twilightforest:progress_merge', 'Hydra'), @('twilightforest:progress_lich', 'Lich'), @('twilightforest:naga_armors', 'Naga'))) {
            $hit = $cells | Where-Object { $_.Matches[0].Groups[1].Value -eq $want[0] } | Select-Object -Last 1
            if ($hit) { $target = @($want[0], $want[1], $hit); break }
        }
        if (-not $target) { $failures.Add('no node with a described argument is in view in the tree') }
        else {
            $hx = [double]::Parse($target[2].Matches[0].Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            $hy = [double]::Parse($target[2].Matches[0].Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
            $hmark = Get-LogMark
            Move-Mouse $hx $hy -Relative
            try {
                $line = Wait-LogLine "client screen: progression\.advancements hover $([regex]::Escape($target[0])) " 10 -Since $hmark
                $text = "$line"
                Write-Host "[progression] $text"
                if ($text -notmatch $target[1] -or $text -match '%s') { $failures.Add("$($target[0])'s hover description lacks its argument: $text") }
            } catch { $failures.Add("hovering $($target[0]) showed no hover") }
            Start-Sleep -Milliseconds 1200 # the fade reaches 0.3
            Save-Screenshot 'advancement-hover' | Out-Null
            # drag the tree with the left button: down on an empty spot of the tree, move, up
            $dmark = Get-LogMark
            Move-Mouse 0.5 0.62 -Relative
            Assert-GameForeground
            [BsE2E.Native]::MouseButton(0, $false)
            Start-Sleep -Milliseconds 150
            foreach ($i in 1..8) { Move-Mouse (0.5 + 0.012 * $i) (0.62 - 0.01 * $i) -Relative }
            Start-Sleep -Milliseconds 200
            Save-Screenshot 'advancement-drag' | Out-Null
            [BsE2E.Native]::MouseButton(0, $true)
            try { Wait-LogLine 'client screen: progression\.advancements dragged to \(-?\d+, -?\d+\)' 10 -Since $dmark | Out-Null }
            catch { $failures.Add('dragging did not pan the tree') }
        }
    }
    Close-Screen
    Start-Sleep -Seconds 1

    # the advancement toast
    $mark = Get-LogMark
    Invoke-Checked 'progression.demo_toast' | Out-Null
    if (Test-ClientTrace) {
        try { Wait-LogLine 'client toast: advancement twilightforest:progress_labyrinth \(goal\)' 20 -Since $mark | Out-Null }
        catch { $failures.Add('the client mod showed no advancement toast') }
    }
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'advancement-toast' | Out-Null
    Start-Sleep -Seconds 6

    # the locked biome toast (the client mod's countdown is driven by client selftests, which exist
    # only in the client build: local mode; over a remote server the host runs the server build)
    if ($script:Mode -eq 'remote-mod') {
        Write-Step 'progression: the locked biome toast of the client mod is covered in local mode'
    } elseif (Test-ClientTrace) {
        Invoke-Checked 'progression.client_toast' | Out-Null
        Start-Sleep -Seconds 6
        $mark = Get-LogMark
        Invoke-Checked 'progression.client_locked_toast' | Out-Null
        try { Wait-LogLine 'client toast: locked biome \(twilightforest:lich_tower_miniature_structure\)' 10 -Since $mark | Out-Null }
        catch { $failures.Add('the client mod showed no locked biome toast') }
        Save-Screenshot 'locked-biome-toast' | Out-Null
    } else {
        $mark = Get-LogMark
        Invoke-Checked 'progression.demo_vanilla_locked' | Out-Null
        try { Wait-LogLine '\[host\].*progression: locked biome toast for \S+ \(vanilla\)' 40 -Since $mark | Out-Null }
        catch { $failures.Add('the vanilla client got no locked biome toast') }
        Start-Sleep -Milliseconds 1500
        Save-Screenshot 'locked-biome-toast' | Out-Null
        Start-Sleep -Seconds 6
    }
} finally {
    Invoke-Checked 'progression.demo_reset' | Out-Null
    Invoke-Chat '/tag @s remove gf_e2e_progression'
}
if ($failures.Count -gt 0) { throw ($failures -join '; ') }
