<#
.SYNOPSIS
  U10 (provisions): composes GloamingForest's soundtrack, tools/sounds/music/<track>.json.

.DESCRIPTION
  The Gloaming Forest's nine tracks (the music discs and the Gloaming Forest music event) are All
  Rights Reserved and never opened (rule R10): only their names and jukebox lengths are upstream's
  (data/gloamingforest/jukebox_song: length_in_seconds, so jukeboxes and comparators keep upstream's
  timing). Everything else here is an original composition for F3's tracker
  (src/core/synth/Sequencer.h): each track has its own key, mode, tempo, two written themes (A and
  B, as scale degrees and durations), harmony for each section, a bass style, an accompaniment
  figure, an optional groove, and a form (intro, A, A with a counter-melody a third below, B, a
  bridge, the return of A transposed, outro) laid out to fill the track's length. The tracks win
  over F3's placeholders in tools/sounds/baseline/music; render them with
  pwsh tools/sounds/synth.ps1 -Unit provisions.

.EXAMPLE
  pwsh tools/sounds/music/compose.ps1          # writes the nine tracks
  pwsh tools/sounds/music/compose.ps1 -Check   # CI: the committed tracks are this script's output
#>
param([switch]$Check)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
. (Join-Path $root 'tools/sounds/lib/SynthJson.ps1')

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
function Degree([int]$rootMidi, $mode, [int]$deg) {
    $oct = [math]::Floor($deg / 7)
    $i   = $deg - 7 * $oct
    return $rootMidi + 12 * $oct + $mode[$i]
}

# Themes: "degree/sixteenths" tokens (degree 0 = the tonic of the lead's octave, 7 = an octave up,
# negative below; 'r' = a rest), 64 sixteenths = one 4-bar phrase. Progressions: one chord (scale
# degree of its root) per bar.
$tracks = [ordered]@{
    radiance = @{
        length = 123; bpm = 84; key = 62; mode = 'lydian'; seed = 101
        lead = 'bell'; counter = 'glock'; arp = 'harp'; pad = 'pad'; bassStyle = 'pulse'; arpStyle = 'updown'; groove = 'none'
        progA = 0, 1, 5, 4; progB = 3, 1, 0, 4; progC = 5, 1, 3, 4
        themeA = '4/6 5/2 4/4 2/4 1/8 0/4 1/4 2/6 4/2 7/8 6/4 5/8 4/2 r/2'
        themeB = '7/4 8/4 9/8 8/4 7/4 6/8 4/4 5/4 6/4 7/4 4/12 r/4'
    }
    steps = @{
        length = 195; bpm = 96; key = 57; mode = 'aeolian'; seed = 202
        lead = 'flute'; counter = 'pluck'; arp = 'pluck'; pad = 'pad'; bassStyle = 'walk'; arpStyle = 'broken'; groove = 'soft'
        progA = 0, 5, 3, 4; progB = 5, 6, 0, 4; progC = 3, 4, 5, 4
        themeA = '0/2 2/2 4/4 4/2 3/2 2/4 0/4 1/2 2/2 3/4 2/4 1/4 -1/4 0/8 4/4 3/4 2/4 r/4'
        themeB = '7/6 6/2 5/4 4/4 5/6 4/2 3/4 2/4 3/4 4/4 5/4 6/4 4/12 r/4'
    }
    superstitious = @{
        length = 192; bpm = 62; key = 62; mode = 'phrygian'; seed = 303
        lead = 'glock'; counter = 'bell'; arp = 'harp'; pad = 'choir'; bassStyle = 'root'; arpStyle = 'sparse'; groove = 'none'
        progA = 0, 1, 0, 6; progB = 3, 1, 4, 0; progC = 5, 6, 1, 0
        themeA = '4/8 5/4 4/4 1/12 0/4 2/8 1/4 0/4 -1/8 0/8'
        themeB = '7/8 8/8 7/4 5/4 4/8 3/4 1/4 2/4 1/4 0/12 r/4'
    }
    home = @{
        length = 215; bpm = 72; key = 65; mode = 'ionian'; seed = 404
        lead = 'flute'; counter = 'harp'; arp = 'harp'; pad = 'pad'; bassStyle = 'pulse'; arpStyle = 'up'; groove = 'none'
        progA = 0, 3, 5, 4; progB = 5, 3, 0, 4; progC = 1, 4, 0, 3
        themeA = '2/4 4/4 7/8 6/4 4/4 5/8 4/4 2/4 3/4 2/4 1/4 2/8 r/4'
        themeB = '4/6 5/2 6/4 7/4 9/8 7/8 6/4 5/4 4/4 2/4 4/12 r/4'
    }
    wayfarer = @{
        length = 173; bpm = 104; key = 64; mode = 'dorian'; seed = 505
        lead = 'flute'; counter = 'bell'; arp = 'pluck'; pad = 'pad'; bassStyle = 'ostinato'; arpStyle = 'broken'; groove = 'drive'
        progA = 0, 3, 6, 4; progB = 5, 6, 0, 0; progC = 3, 4, 3, 6
        themeA = '0/2 0/2 4/4 3/2 2/2 3/4 4/4 5/2 4/2 2/4 0/4 1/4 2/6 1/2 0/4 -2/4 0/8 r/4'
        themeB = '4/2 5/2 7/4 7/4 9/4 8/2 7/2 5/4 4/4 5/4 7/4 6/4 4/8 2/4 4/8 r/4'
    }
    findings = @{
        length = 196; bpm = 80; key = 60; mode = 'lydian'; seed = 606
        lead = 'bell'; counter = 'flute'; arp = 'glock'; pad = 'choir'; bassStyle = 'root'; arpStyle = 'updown'; groove = 'none'
        progA = 0, 1, 0, 4; progB = 5, 1, 2, 4; progC = 3, 5, 1, 4
        themeA = '7/4 6/2 4/2 3/8 4/4 6/4 7/4 9/8 7/4 4/4 6/8 4/4 2/4 4/2 r/2'
        themeB = '4/4 3/4 2/4 1/4 2/8 4/8 6/4 7/4 6/4 4/4 3/12 r/4'
    }
    maker = @{
        length = 207; bpm = 98; key = 55; mode = 'mixolydian'; seed = 707
        lead = 'flute'; counter = 'glock'; arp = 'pluck'; pad = 'pad'; bassStyle = 'ostinato'; arpStyle = 'broken'; groove = 'drive'
        progA = 0, 6, 3, 0; progB = 4, 3, 6, 4; progC = 3, 6, 0, 4
        themeA = '0/2 2/2 4/2 6/2 7/4 6/4 4/4 3/4 4/8 2/2 3/2 4/4 6/4 4/8 2/4 0/6 r/2'
        themeB = '7/4 6/4 7/2 9/2 7/4 6/8 4/4 3/4 2/4 4/4 6/4 4/4 3/4 0/8 r/4'
    }
    thread = @{
        length = 201; bpm = 66; key = 59; mode = 'aeolian'; seed = 808
        lead = 'glock'; counter = 'flute'; arp = 'harp'; pad = 'choir'; bassStyle = 'root'; arpStyle = 'up'; groove = 'none'
        progA = 0, 5, 2, 6; progB = 3, 4, 0, 4; progC = 5, 3, 6, 4
        themeA = '4/4 3/4 2/4 3/4 4/8 0/8 2/4 1/4 0/4 -1/4 0/12 r/4'
        themeB = '2/4 4/4 7/8 5/4 4/4 3/8 2/4 4/4 1/4 -1/4 0/12 r/4'
    }
    motion = @{
        length = 169; bpm = 92; key = 58; mode = 'ionian'; seed = 909
        lead = 'bell'; counter = 'flute'; arp = 'pluck'; pad = 'pad'; bassStyle = 'pulse'; arpStyle = 'broken'; groove = 'soft'
        progA = 0, 4, 5, 3; progB = 1, 4, 0, 5; progC = 3, 4, 1, 4
        themeA = '4/2 2/2 4/4 5/2 4/2 2/4 1/4 0/4 2/4 4/4 7/4 6/2 5/2 4/8 2/4 1/4 0/6 r/2'
        themeB = '7/2 6/2 5/4 4/4 2/4 3/2 4/2 5/4 4/4 2/4 1/4 2/8 4/4 5/4 4/4 r/8'
    }
}

$rows = 64

function Cells([int]$count) { return @(for ($i = 0; $i -lt $count; $i++) { '.' }) }

# a theme string -> 64 cells of (degree | $null rest | '.' hold); returns degrees per onset
function Expand-Theme([string]$theme) {
    $cells = Cells $rows
    $pos   = 0
    foreach ($tok in ($theme -split '\s+' | Where-Object { $_ })) {
        $deg, $len = $tok -split '/'
        $len = [int]$len
        if ($pos -ge $rows) { break }
        $cells[$pos] = if ($deg -eq 'r') { '-' } else { [int]$deg }
        $pos += $len
    }
    if ($pos -ne $rows) { throw "theme '$theme' spans $pos sixteenths, not $rows" }
    return $cells
}

function Chord($t, [int]$deg, [int]$base) {
    $mode = $modes[$t.mode]
    return @(0, 2, 4 | ForEach-Object { Degree $base $mode ($deg + $_) })
}

function Velocity([int]$row, [double]$scale) {
    $v = if ($row % 16 -eq 0) { 0.9 } elseif ($row % 8 -eq 0) { 0.8 } elseif ($row % 4 -eq 0) { 0.7 } else { 0.6 }
    return [math]::Round($v * $scale, 2)
}

function Melody($t, $cells, [int]$octave, [int]$shift, [double]$vel) {
    $mode = $modes[$t.mode]
    $out  = Cells $rows
    for ($i = 0; $i -lt $rows; $i++) {
        $c = $cells[$i]
        if ($c -is [int]) { $out[$i] = (NoteName (Degree ($t.key + 12 * $octave) $mode ($c + $shift))) + ':' + (Velocity $i $vel) }
        elseif ($c -eq '-') { $out[$i] = '-' }
    }
    return $out
}

function Bass($t, $prog, [string]$style) {
    $mode = $modes[$t.mode]
    $out  = Cells $rows
    $root = $t.key - 24
    for ($bar = 0; $bar -lt 4; $bar++) {
        $d = $prog[$bar]; $b = 16 * $bar
        switch ($style) {
            'root' { $out[$b] = (NoteName (Degree $root $mode $d)) + ':0.85' }
            'pulse' {
                $out[$b] = (NoteName (Degree $root $mode $d)) + ':0.9'
                $out[$b + 8] = (NoteName (Degree $root $mode ($d + 4))) + ':0.65'
                $out[$b + 14] = (NoteName (Degree $root $mode $d)) + ':0.5'
            }
            'walk' {
                $next = $prog[($bar + 1) % 4]
                $steps = @($d, ($d + 2), ($d + 4), ($next + 1))
                for ($k = 0; $k -lt 4; $k++) { $out[$b + 4 * $k] = (NoteName (Degree $root $mode $steps[$k])) + ':' + @(0.9, 0.6, 0.7, 0.6)[$k] }
            }
            'ostinato' {
                $fig = @($d, $d, ($d + 4), ($d + 7), $d, ($d + 4), ($d + 7), ($d + 4))
                for ($k = 0; $k -lt 8; $k++) { $out[$b + 2 * $k] = (NoteName (Degree $root $mode $fig[$k])) + ':' + @(0.9, 0.5, 0.6, 0.55, 0.75, 0.5, 0.6, 0.5)[$k] }
            }
        }
    }
    return $out
}

function Accompaniment($t, $prog, [string]$style, [double]$vel) {
    $out = Cells $rows
    for ($bar = 0; $bar -lt 4; $bar++) {
        $d     = $prog[$bar]; $b = 16 * $bar
        $tones = @(Chord $t $d ($t.key - 12)) + @(Chord $t ($d + 7) ($t.key - 12))
        switch ($style) {
            'up' { $order = 0, 1, 2, 3, 4, 5, 4, 2; $every = 2 }
            'updown' { $order = 0, 1, 2, 3, 2, 1, 0, 1; $every = 2 }
            'broken' { $order = 0, 2, 1, 2, 3, 2, 1, 2, 0, 2, 1, 2, 4, 2, 1, 2; $every = 1 }
            'sparse' { $order = 0, 2, 4, 3; $every = 4 }
        }
        for ($k = 0; $k -lt $order.Count; $k++) {
            $row = $b + $every * $k
            $out[$row] = (NoteName $tones[$order[$k]]) + ':' + (Velocity $row $vel)
        }
    }
    return $out
}

function Pad($t, $prog, [double]$vel) {
    $out = Cells $rows
    for ($bar = 0; $bar -lt 4; $bar++) {
        $out[16 * $bar] = ((Chord $t $prog[$bar] ($t.key - 12)) | ForEach-Object { NoteName $_ }) -join '+'
        $out[16 * $bar] += ":$vel"
    }
    return $out
}

function Groove([string]$style, [bool]$fill) {
    $kick = Cells $rows; $shaker = Cells $rows
    for ($bar = 0; $bar -lt 4; $bar++) {
        $b = 16 * $bar
        switch ($style) {
            'soft' {
                $kick[$b] = 'C2:0.85'; $kick[$b + 8] = 'C2:0.55'
                for ($k = 4; $k -lt 16; $k += 8) { $shaker[$b + $k] = 'C6:0.5' }
            }
            'drive' {
                $kick[$b] = 'C2:0.9'; $kick[$b + 6] = 'C2:0.5'; $kick[$b + 8] = 'C2:0.75'; $kick[$b + 4] = 'G2:0.55'; $kick[$b + 12] = 'G2:0.6'
                for ($k = 2; $k -lt 16; $k += 4) { $shaker[$b + $k] = 'C6:0.55' }
            }
        }
        if ($fill -and $bar -eq 3 -and $style -ne 'none') { $kick[$b + 14] = 'G2:0.5'; $kick[$b + 15] = 'G2:0.65' }
    }
    return @($kick, $shaker)
}

function Section($t, [string]$kind) {
    $themeA = Expand-Theme $t.themeA
    $themeB = Expand-Theme $t.themeB
    $ch = @()
    $add = { param($inst, $cells) $script:chList += , [ordered]@{ inst = $inst; notes = ($cells -join ' ') } }
    $script:chList = @()
    switch ($kind) {
        'intro' {
            & $add 'pad' (Pad $t $t.progA 0.55)
            & $add 'arp' (Accompaniment $t $t.progA $t.arpStyle 0.6)
        }
        'A' {
            & $add 'pad' (Pad $t $t.progA 0.6)
            & $add 'bass' (Bass $t $t.progA $t.bassStyle)
            & $add 'arp' (Accompaniment $t $t.progA $t.arpStyle 0.7)
            & $add 'lead' (Melody $t $themeA 0 0 1.0)
        }
        'A2' {
            & $add 'pad' (Pad $t $t.progA 0.6)
            & $add 'bass' (Bass $t $t.progA $t.bassStyle)
            & $add 'arp' (Accompaniment $t $t.progA $t.arpStyle 0.7)
            & $add 'lead' (Melody $t $themeA 0 0 1.0)
            & $add 'counter' (Melody $t $themeA 0 -2 0.6)
        }
        'B' {
            & $add 'pad' (Pad $t $t.progB 0.65)
            & $add 'bass' (Bass $t $t.progB $t.bassStyle)
            & $add 'arp' (Accompaniment $t $t.progB $t.arpStyle 0.65)
            & $add 'lead' (Melody $t $themeB 0 0 1.0)
        }
        'C' {
            # the bridge: the B theme an octave down on the counter instrument over the third harmony
            & $add 'pad' (Pad $t $t.progC 0.7)
            & $add 'bass' (Bass $t $t.progC 'root')
            & $add 'counter' (Melody $t $themeB -1 0 0.8)
            & $add 'arp' (Accompaniment $t $t.progC 'sparse' 0.55)
        }
        'outro' {
            & $add 'pad' (Pad $t $t.progA 0.5)
            & $add 'bass' (Bass $t $t.progA 'root')
            $tail = Expand-Theme $t.themeA
            for ($i = 32; $i -lt $rows; $i++) { $tail[$i] = '.' }
            $tail[32] = '-'
            & $add 'lead' (Melody $t $tail 0 0 0.7)
        }
    }
    if ($t.groove -ne 'none' -and $kind -in 'A', 'A2', 'B') {
        $g = Groove $t.groove ($kind -eq 'B')
        & $add 'drum' $g[0]
        & $add 'shaker' $g[1]
    }
    return [ordered]@{ rows = $rows; channels = $script:chList }
}

$outDir = $PSScriptRoot
$diffs  = 0
foreach ($name in $tracks.Keys) {
    $t = $tracks[$name]
    $patterns = [ordered]@{}
    foreach ($k in 'intro', 'A', 'A2', 'B', 'C', 'outro') { $patterns[$k] = Section $t $k }
    # the form, long enough for the track (the sequencer renders exactly `length` seconds, with the
    # fade-out at the end): intro A A2 B C A2(+key) B A2 ... outro
    $patternSeconds = 16 * 60.0 / $t.bpm
    $count = [math]::Ceiling($t.length / $patternSeconds)
    $body  = @('A', 'A2', 'B', 'C', [ordered]@{ p = 'A2'; t = 2 }, [ordered]@{ p = 'B'; t = 2 }, 'A', 'C', 'A2', 'B')
    $order = @('intro')
    $i = 0
    while ($order.Count -lt $count - 1) { $order += $body[$i % $body.Count]; $i++ }
    $order += 'outro'
    $instruments = [ordered]@{
        pad     = [ordered]@{ patch = "music/$($t.pad)"; gain = -10 }
        bass    = [ordered]@{ patch = 'music/bass'; gain = -8 }
        arp     = [ordered]@{ patch = "music/$($t.arp)"; gain = -11 }
        lead    = [ordered]@{ patch = "music/$($t.lead)"; gain = -5 }
        counter = [ordered]@{ patch = "music/$($t.counter)"; gain = -9 }
        drum    = [ordered]@{ patch = 'music/drum'; gain = -9 }
        shaker  = [ordered]@{ patch = 'music/shaker'; gain = -17 }
    }
    $track = [ordered]@{
        about       = "GloamingForest's '$name' ($($t.length) s, upstream's jukebox length), an original composition by tools/sounds/music/compose.ps1 (U10)."
        rate        = 22050
        quality     = 0.25
        seed        = $t.seed
        bpm         = $t.bpm
        rpb         = 4
        length      = $t.length
        instruments = $instruments
        patterns    = $patterns
        order       = $order
        fx          = @([ordered]@{ type = 'reverb'; room = 0.78; damp = 0.45; mix = 0.24 })
        normalize   = -2
        fadeIn      = 1.5
        fadeOut     = 7.0
    }
    $path = Join-Path $outDir "$name.json"
    $text = (Format-Json $track) + "`n"
    if ($Check) {
        $cur = if (Test-Path $path) { (Get-Content $path -Raw) -replace "`r`n", "`n" } else { '' }
        if ($cur -ne $text) { Write-Host "[compose] differs: tools/sounds/music/$name.json" -ForegroundColor Red; $diffs++ }
    } else {
        [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
    }
}
if ($Check) { if ($diffs) { exit 1 }; Write-Host '[compose] -Check OK' -ForegroundColor Green; exit 0 }
Write-Host "[compose] $($tracks.Count) tracks in tools/sounds/music" -ForegroundColor Green
