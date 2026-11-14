# U15 (atmosphere): the server side of the Gloaming Forest's atmosphere. Every atmosphere.* selftest
# runs (dx 1500..1599): the config section, the fog stacks of the vanilla fallback and the 24 shipped
# resource pack fog definitions, PlayerFogPackets to a player in the Gloaming Forest (sent once per
# change), the acid rain damage type and sound; the show_* previews have no companion here and only
# report. The client scenario (tools/e2e/client/scenarios/atmosphere.ps1) covers the rendering.
$r = Invoke-SelfTest 'atmosphere.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) atmosphere selftest(s) failed" }
foreach ($name in 'atmosphere.config', 'atmosphere.fog_stack', 'atmosphere.fog_packet', 'atmosphere.acid_rain') {
    if ($r.Passed -notcontains $name) { throw "$name did not pass" }
}
Wait-LogLine 'atmosphere: fog gloamingforest:spooky_forest sent to' 10 | Out-Null
Write-Host 'atmosphere: the vanilla fog fallback sent its packets'
