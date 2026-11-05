# U14 (progression): the client side of TF's advancements. Screenshots:
#   advancement-tree.png     the advancement screen after the demo grants (root, progress_naga,
#                            hedge, hill1, twilight_hunter, progress_lich, naga_armors): the client
#                            mod's native tree (TF's tab, mazestone background, gold earned frames,
#                            grey unearned ones two levels deep, the connecting lines; local,
#                            remote-mod), the form list on a vanilla client (remote-vanilla)
#   advancement-toast.png    progress_labyrinth earned: Java's "Goal Reached!" toast with the
#                            labyrinth miniature (client mod), the game's text toast (vanilla)
#   locked-biome-toast.png   TF's LockedBiomeToast after the 60-step countdown in the dark forest
#                            (client mod: lich tower miniature, yellow "Biome Locked!", white "Check
#                            your advancements"); on a vanilla client the server's toast after the
#                            same countdown in the swamp (text only)
# Compare them with TF's advancement screen (vanilla's window, TF's mazestone tab background) and
# LockedBiomeToast (the advancement toast frame, icon at 6,8, text at 25,7 and 25,18).
$ready = if ($script:Mode -eq 'local') { 'GloamingForest world ready' } else { '\[host\].*GloamingForest world ready' }
Wait-LogLine $ready 120 | Out-Null
Save-Screenshot 'hud' | Out-Null

$failures = [System.Collections.Generic.List[string]]::new()
function Invoke-Checked([string]$Test) {
    $r = Invoke-SelfTest $Test
    if ($r.Fail -gt 0) { $failures.Add("$Test failed") }
    return $r
}

foreach ($t in 'progression.advancements', 'progression.restrictions', 'progression.trigger_criteria', 'progression.toasts_and_screen') { Invoke-Checked $t | Out-Null }

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

# the locked biome toast
if (Test-ClientTrace) {
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

Invoke-Checked 'progression.demo_reset' | Out-Null
if ($failures.Count -gt 0) { throw ($failures -join '; ') }
