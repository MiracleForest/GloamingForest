<#
.SYNOPSIS
  Contract "synth" (F3): composes the baseline music tracks tools/sounds/baseline/music/<track>.json.

.DESCRIPTION
  TF's nine tracks (music discs and the Twilight Forest music event) are All Rights Reserved and
  never opened (rule R10). This script writes original placeholder compositions for the tracker
  (src/core/synth/Sequencer.h) with the TF track lengths (data/twilightforest/jukebox_song,
  length_in_seconds) so jukeboxes and comparators keep TF's timing. Each track gets its own key,
  mode, tempo, progression and instruments; melodies come from a seeded walk over chord tones.
  U10 (provisions) composes the real soundtrack in tools/sounds/music/<track>.json, which takes
  precedence over these.

.EXAMPLE
  pwsh tools/sounds/baseline/compose.ps1
#>
param([switch]$Check)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
. (Join-Path $root 'tools/sounds/lib/SynthJson.ps1')

# a tiny LCG so the compositions never depend on .NET's Random implementation
$script:state = [uint64]1
function Seed([long]$s) { $script:state = [uint64]($s % 2147483647) + 1 }
function Rnd([int]$n) {
    $script:state = ($script:state * 48271) % 2147483647
    return [int]($script:state % [uint64]$n)
}
function Pick($list) { return $list[(Rnd $list.Count)] }

$modes = @{
    ionian     = 0, 2, 4, 5, 7, 9, 11
    dorian     = 0, 2, 3, 5, 7, 9, 10
    phrygian   = 0, 1, 3, 5, 7, 8, 10
    lydian     = 0, 2, 4, 6, 7, 9, 11
    mixolydian = 0, 2, 4, 5, 7, 9, 10
    aeolian    = 0, 2, 3, 5, 7, 8, 10
}
$names = 'C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'
function NoteName([int]$midi) { return $names[$midi % 12] + ([math]::Floor($midi / 12) - 1) }

# scale degree (0-based, may exceed 6) -> MIDI
function Degree([int]$rootMidi, $mode, [int]$deg) {
    $oct = [math]::Floor($deg / 7)
    $i   = $deg - 7 * $oct
    return $rootMidi + 12 * $oct + $mode[$i]
}

# track designs: TF names and lengths, original musical choices
$tracks = [ordered]@{
    radiance      = @{ length = 123; bpm = 76; key = 62; mode = 'ionian'; prog = 0, 5, 3, 4; progB = 3, 4, 2, 5; lead = 'bell'; arp = 'harp'; pad = 'pad'; drums = $false; seed = 11 }
    steps         = @{ length = 195; bpm = 92; key = 57; mode = 'aeolian'; prog = 0, 6, 5, 4; progB = 3, 0, 4, 4; lead = 'flute'; arp = 'pluck'; pad = 'pad'; drums = $true; seed = 23 }
    superstitious = @{ length = 192; bpm = 60; key = 62; mode = 'phrygian'; prog = 0, 1, 0, 6; progB = 3, 1, 4, 0; lead = 'glock'; arp = 'harp'; pad = 'choir'; drums = $false; seed = 37 }
    home          = @{ length = 215; bpm = 70; key = 65; mode = 'ionian'; prog = 0, 3, 5, 4; progB = 5, 3, 0, 4; lead = 'flute'; arp = 'pluck'; pad = 'pad'; drums = $false; seed = 41 }
    wayfarer      = @{ length = 173; bpm = 100; key = 64; mode = 'dorian'; prog = 0, 3, 6, 4; progB = 5, 6, 0, 0; lead = 'flute'; arp = 'harp'; pad = 'pad'; drums = $true; seed = 53 }
    findings      = @{ length = 196; bpm = 80; key = 60; mode = 'lydian'; prog = 0, 1, 0, 4; progB = 5, 1, 2, 4; lead = 'bell'; arp = 'glock'; pad = 'choir'; drums = $false; seed = 67 }
    maker         = @{ length = 207; bpm = 96; key = 55; mode = 'mixolydian'; prog = 0, 6, 3, 0; progB = 4, 3, 6, 4; lead = 'flute'; arp = 'pluck'; pad = 'pad'; drums = $true; seed = 71 }
    thread        = @{ length = 201; bpm = 66; key = 59; mode = 'aeolian'; prog = 0, 5, 2, 6; progB = 3, 4, 0, 4; lead = 'glock'; arp = 'harp'; pad = 'choir'; drums = $false; seed = 83 }
    motion        = @{ length = 169; bpm = 88; key = 58; mode = 'ionian'; prog = 0, 4, 5, 3; progB = 1, 4, 0, 5; lead = 'bell'; arp = 'pluck'; pad = 'pad'; drums = $true; seed = 97 }
}

$rows = 64 # 4 bars of 16 sixteenths

function Chord($t, [int]$deg, [int]$octave) {
    $mode = $modes[$t.mode]
    return @(0, 2, 4 | ForEach-Object { Degree ($t.key + 12 * $octave) $mode ($deg + $_) })
}

function Cells([int]$count) { return @(for ($i = 0; $i -lt $count; $i++) { '.' }) }

function Pattern($t, $prog, [string]$kind) {
    $mode  = $modes[$t.mode]
    $pad   = Cells $rows
    $arp   = Cells $rows
    $bass  = Cells $rows
    $lead  = Cells $rows
    $perc  = Cells $rows
    $prev  = 7 + (Rnd 3)
    for ($bar = 0; $bar -lt 4; $bar++) {
        $deg   = $prog[$bar]
        $chord = Chord $t $deg -1
        $base  = 16 * $bar
        $pad[$base] = ($chord | ForEach-Object { NoteName $_ }) -join '+'
        if ($pad[$base]) { $pad[$base] += ':0.7' }
        $bass[$base] = (NoteName (Degree ($t.key - 24) $mode $deg)) + ':0.9'
        if ($kind -ne 'intro') { $bass[$base + 8] = (NoteName (Degree ($t.key - 24) $mode ($deg + 4))) + ':0.6' }
        if ($kind -ne 'outro') {
            # arpeggio: chord tones up and down over the bar
            $tones = @(Chord $t $deg 0) + @(Chord $t ($deg + 7) 0)[0]
            $order = 0, 1, 2, 3, 2, 1, 0, 1
            for ($k = 0; $k -lt 8; $k++) {
                $vel = if ($k % 4 -eq 0) { 0.8 } else { 0.55 }
                $arp[$base + 2 * $k] = (NoteName $tones[$order[$k]]) + ":$vel"
            }
        }
        if ($kind -eq 'A' -or $kind -eq 'B') {
            # melody: a walk over scale degrees leaning to chord tones, motif rhythm per bar
            $rhythm = Pick @(@(0, 4, 8, 12), @(0, 6, 8, 14), @(0, 3, 8, 10, 12), @(0, 8), @(0, 4, 6, 8, 12))
            foreach ($r in $rhythm) {
                $step = Pick @(-2, -1, -1, 0, 1, 1, 2, 3, -3)
                $cand = $prev + $step
                if ((Rnd 3) -eq 0) { $cand = $deg + 7 + (Pick @(0, 2, 4)) } # land on a chord tone
                $cand = [math]::Max(4, [math]::Min(13, $cand))
                $prev = $cand
                $lead[$base + $r] = (NoteName (Degree $t.key $mode $cand)) + ':' + (Pick @('0.7', '0.8', '0.9'))
            }
            $lead[$base + 15] = '-'
        }
        if ($t.drums -and $kind -ne 'intro' -and $kind -ne 'outro') {
            $perc[$base] = 'C2:0.9'
            $perc[$base + 8] = 'C2:0.6'
            if ($kind -eq 'B') { $perc[$base + 12] = 'G2:0.5' }
        }
    }
    $channels = @(
        [ordered]@{ inst = 'pad'; notes = ($pad -join ' ') }
        [ordered]@{ inst = 'bass'; notes = ($bass -join ' ') }
    )
    if ($kind -ne 'outro') { $channels += [ordered]@{ inst = 'arp'; notes = ($arp -join ' ') } }
    if ($kind -eq 'A' -or $kind -eq 'B') { $channels += [ordered]@{ inst = 'lead'; notes = ($lead -join ' ') } }
    if ($t.drums -and $kind -ne 'intro' -and $kind -ne 'outro') {
        $channels += [ordered]@{ inst = 'drum'; notes = ($perc -join ' ') }
        $sh = Cells $rows
        for ($i = 2; $i -lt $rows; $i += 4) { $sh[$i] = 'C6:0.5' }
        $channels += [ordered]@{ inst = 'shaker'; notes = ($sh -join ' ') }
    }
    return [ordered]@{ rows = $rows; channels = $channels }
}

$outDir = Join-Path $PSScriptRoot 'music'
$diffs  = 0
foreach ($name in $tracks.Keys) {
    $t = $tracks[$name]
    Seed $t.seed
    $patterns = [ordered]@{
        intro = Pattern $t $t.prog 'intro'
        A     = Pattern $t $t.prog 'A'
        A2    = Pattern $t $t.prog 'A'
        B     = Pattern $t $t.progB 'B'
        outro = Pattern $t $t.prog 'outro'
    }
    $instruments = [ordered]@{
        pad    = [ordered]@{ patch = "music/$($t.pad)"; gain = -9 }
        bass   = [ordered]@{ patch = 'music/bass'; gain = -8 }
        arp    = [ordered]@{ patch = "music/$($t.arp)"; gain = -10 }
        lead   = [ordered]@{ patch = "music/$($t.lead)"; gain = -6 }
        drum   = [ordered]@{ patch = 'music/drum'; gain = -9 }
        shaker = [ordered]@{ patch = 'music/shaker'; gain = -16 }
    }
    $track = [ordered]@{
        about       = "Baseline placeholder for TF's '$name' ($($t.length) s), composed by tools/sounds/baseline/compose.ps1; U10 replaces it with tools/sounds/music/$name.json."
        rate        = 22050
        quality     = 0.25
        seed        = $t.seed
        bpm         = $t.bpm
        rpb         = 4
        length      = $t.length
        instruments = $instruments
        patterns    = $patterns
        order       = @('intro', 'A', 'A2', 'B', 'A', [ordered]@{ p = 'B'; t = 2 }, 'A2', 'B', 'outro')
        fx          = @([ordered]@{ type = 'reverb'; room = 0.75; damp = 0.4; mix = 0.22 })
        normalize   = -2
        fadeIn      = 1.0
        fadeOut     = 6.0
    }
    $path = Join-Path $outDir "$name.json"
    $text = (Format-Json $track) + "`n"
    if ($Check) {
        $cur = if (Test-Path $path) { (Get-Content $path -Raw) -replace "`r`n", "`n" } else { '' }
        if ($cur -ne $text) { Write-Host "[compose] differs: tools/sounds/baseline/music/$name.json" -ForegroundColor Red; $diffs++ }
    } else {
        New-Item -ItemType Directory -Force $outDir | Out-Null
        [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
    }
}
if ($Check) { if ($diffs) { exit 1 }; Write-Host '[compose] -Check OK' -ForegroundColor Green; exit 0 }
Write-Host "[compose] $($tracks.Count) baseline tracks in tools/sounds/baseline/music" -ForegroundColor Green
