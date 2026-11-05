# Spike S7 follow-up (F1): how a block of each format_version declares block tags in BDS 1.26.51. The probe blocks
# come from import.s7.prelaunch.ps1 (listed in the probe pack's probes.json); import.s7_probe reports what the game
# registered (block type and permutation tags, block actor type), and the content log says which forms each format
# rejects. Evidence for docs/spikes/S7.md.
#
# The probe pack loads the rejected forms on purpose, so this run's content log has errors by design. The scenario
# derives the exact expected error set from probes.json and the measured rules below (one multiset of messages per
# probe file), requires every expected line, reports them as evidence, and then hides exactly those lines from
# run.ps1's final error check (it redefines Get-ErrorLines, dot-sourced in run.ps1's scope, on top of the harness's
# own filter). Every other ERR / FTL line still fails the run: an unexpected probe message, an error of a probe
# that should load, an error outside the probe pack, or one logged after this check (shutdown). A missing
# expected line fails the run too. So the scenario exits 0 when the probes behave as measured and nothing else fails.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'import.s7_probe'
# every TF block registered with its frozen states, and its tool / tier tags as the game holds them (the family
# blocks from the import's own skeleton at 1.26.50 in this run, see the prelaunch)
$r = Invoke-SelfTest 'import.blocks'
$r = Invoke-SelfTest 'import.harvest_tags'
$r = Invoke-SelfTest 'import.rotations'
$r = Invoke-SelfTest 'import.light'
$r = Invoke-SelfTest 'import.behaviour'

# ---- the expected content errors of the probe pack ---------------------------------------------------------------
# What BDS 1.26.51 logs for each probe form (docs/spikes/S7.md, W0c follow-up). The 1.26.20 block schema (formats
# 1.26.20 and later) logs one line per refused document: the item form of minecraft:tags, or the first "tag:<id>"
# component; the legacy parser (up to 1.26.10) logs one line per refused component and "Block definition parsing
# failed". The array form from 1.26.20 and the "tag:" form up to 1.26.10 load without any line.
$s7Modern = @{
    legacy        = @('-> components -> tag:minecraft:is_pickaxe_item_destructible: this component was found in the input, but is not present in the Schema')
    object        = @('-> components -> minecraft:tags: expected an array')
    array         = @()
    array_entity  = @()
    legacy_entity = @('-> components -> tag:minecraft:is_pickaxe_item_destructible: this component was found in the input, but is not present in the Schema')
}
$s7TagsChild = "components | minecraft:tags | child 'minecraft:tags' not valid here."
$s7EntityChild = "components | minecraft:block_entity | child 'minecraft:block_entity' not valid here."
$s7Failed = 'Block definition parsing failed'
$s7Legacy = @{
    legacy        = @()
    object        = @($s7TagsChild, $s7Failed)
    array         = @($s7TagsChild, $s7Failed)
    array_entity  = @($s7EntityChild, $s7TagsChild, $s7Failed)
    legacy_entity = @($s7EntityChild, $s7Failed)
}
function Test-S7ModernFormat([string]$Format) {
    $v = [version]$Format
    return $v -ge [version]'1.26.20'
}

$s7Pack = Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName\behavior_packs\GloamingForest_S7_BP"
$s7Probes = @((Get-Content (Join-Path $s7Pack 'probes.json') -Raw | ConvertFrom-Json).probes)
if ($s7Probes.Count -eq 0) { throw 'S7: the probe pack lists no probe (probes.json)' }
# probe file name (blocks/<name>.json) -> the expected messages, as a list (a multiset: the order is not checked)
$s7Expected = @{}
foreach ($p in $s7Probes) {
    $name = $p.id.Substring($p.id.IndexOf(':') + 1)
    $table = if (Test-S7ModernFormat $p.format) { $s7Modern } else { $s7Legacy }
    if (-not $table.ContainsKey($p.form)) { throw "S7: probe $($p.id) has form '$($p.form)', which this scenario has no expectation for" }
    $s7Expected[$name] = [System.Collections.Generic.List[string]]::new([string[]]@($table[$p.form]))
}
$s7ExpectedCount = ($s7Expected.Values | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum

# The probe pack's lines: "ERR [Server] [Blocks] block_definitions | <pack> | blocks/<name>.json | <message>", where
# the legacy parser repeats "<pack> | blocks/<name>.json | gf_s7:<name> | " before the component path.
$s7LineRegex = '\bERR \[Server\] \[Blocks\] block_definitions \| (?<pack>[^|]*?[\\/]GloamingForest_S7_BP) \| blocks/(?<name>[A-Za-z0-9_]+)\.json \| (?<msg>.*)$'
$s7Matched = [System.Collections.Generic.List[string]]::new()
$s7Unexpected = [System.Collections.Generic.List[string]]::new()
$s7Errors = @(Get-ErrorLines)
foreach ($l in $s7Errors) {
    $m = [regex]::Match($l, $s7LineRegex)
    if (-not $m.Success) {
        if ($l -match 'GloamingForest_S7_BP') { $s7Unexpected.Add($l) }   # a probe pack line of another shape
        continue                                                         # outside the probe pack: run.ps1 reports it
    }
    $name = $m.Groups['name'].Value
    $msg = $m.Groups['msg'].Value.Trim()
    $prefix = "$($m.Groups['pack'].Value) | blocks/$name.json | gf_s7:$name | "
    if ($msg.StartsWith($prefix, [StringComparison]::Ordinal)) { $msg = $msg.Substring($prefix.Length) }
    if ($s7Expected.ContainsKey($name) -and $s7Expected[$name].Remove($msg)) { $s7Matched.Add($l) }
    else { $s7Unexpected.Add($l) }
}
$s7Missing = @(foreach ($k in ($s7Expected.Keys | Sort-Object)) { foreach ($msg in $s7Expected[$k]) { "blocks/$k.json | $msg" } })
$s7Refused = @($s7Probes | Where-Object {
        $t = if (Test-S7ModernFormat $_.format) { $s7Modern } else { $s7Legacy }
        @($t[$_.form]).Count -gt 0
    })

Write-Step "S7 evidence: $($s7Matched.Count) of $s7ExpectedCount expected content errors from the probe pack ($($s7Refused.Count) of $($s7Probes.Count) probes refused: $(@($s7Refused | ForEach-Object { $_.id.Substring(6) }) -join ', '))"
$s7Matched | ForEach-Object { Write-Step "  expected: $_" }
$others = @($s7Errors | Where-Object { $_ -notmatch 'GloamingForest_S7_BP' })
Write-Step "S7: $($s7Unexpected.Count) unexpected probe pack error(s), $($s7Missing.Count) expected error(s) missing, $($others.Count) error(s) outside the probe pack"
$s7Unexpected | ForEach-Object { Write-Step "  unexpected: $_" }
$s7Missing | ForEach-Object { Write-Step "  missing: $_" }

# run.ps1's final check: every error line but the expected probe lines matched above (each once). The harness has no
# expected-errors hook yet, so this relies on run.ps1 dot-sourcing the scenario at script level and calling
# Get-ErrorLines in its final check; if either changes, the override stops applying and the run fails again (safe).
$script:S7ExpectedLines = [System.Collections.Generic.List[string]]::new([string[]]$s7Matched)
$script:S7BaseErrorLines = ${function:Get-ErrorLines}
function script:Get-ErrorLines {
    $left = [System.Collections.Generic.List[string]]::new([string[]]$script:S7ExpectedLines)
    @(& $script:S7BaseErrorLines | Where-Object { -not $left.Remove($_) })
}

if ($s7Missing.Count -gt 0) { throw "S7: $($s7Missing.Count) expected probe content error(s) missing (first: $($s7Missing[0]))" }
if ($s7Unexpected.Count -gt 0) { throw "S7: $($s7Unexpected.Count) unexpected probe pack content error(s) (first: $($s7Unexpected[0]))" }
