# Contract "synth" (F3): JSON helpers shared by tools/sounds/*.ps1 (dot-sourced).
#
# Format-Json writes deterministic JSON (2-space indent, keys in insertion order, invariant
# numbers) so that generated files are byte-identical on every machine and PowerShell version.

$script:SynthInvariant = [Globalization.CultureInfo]::InvariantCulture

function Format-Json($v, [int]$indent = 0) {
    $pad  = '  ' * $indent
    $pad1 = '  ' * ($indent + 1)
    if ($null -eq $v) { return 'null' }
    if ($v -is [bool]) { return $(if ($v) { 'true' } else { 'false' }) }
    if ($v -is [string]) {
        $s = $v.Replace('\', '\\').Replace('"', '\"').Replace("`n", '\n').Replace("`r", '\r').Replace("`t", '\t')
        return '"' + $s + '"'
    }
    if ($v -is [int] -or $v -is [long] -or $v -is [int16] -or $v -is [byte]) { return ([long]$v).ToString($script:SynthInvariant) }
    if ($v -is [double] -or $v -is [single] -or $v -is [decimal] -or $v -is [bigint]) {
        $d = [double]$v
        if ($d -eq [math]::Floor($d) -and [math]::Abs($d) -lt 1e15) { return ([long]$d).ToString($script:SynthInvariant) }
        return $d.ToString('R', $script:SynthInvariant)
    }
    if ($v -is [Collections.IDictionary]) {
        if ($v.Count -eq 0) { return '{}' }
        $parts = foreach ($k in $v.Keys) { $pad1 + (Format-Json ([string]$k)) + ': ' + (Format-Json $v[$k] ($indent + 1)) }
        return "{`n" + ($parts -join ",`n") + "`n$pad}"
    }
    if ($v -is [pscustomobject]) {
        $props = @($v.PSObject.Properties)
        if ($props.Count -eq 0) { return '{}' }
        $parts = foreach ($p in $props) { $pad1 + (Format-Json $p.Name) + ': ' + (Format-Json $p.Value ($indent + 1)) }
        return "{`n" + ($parts -join ",`n") + "`n$pad}"
    }
    if ($v -is [Collections.IEnumerable]) {
        $items = @($v)
        if ($items.Count -eq 0) { return '[]' }
        $parts = foreach ($x in $items) { $pad1 + (Format-Json $x ($indent + 1)) }
        return "[`n" + ($parts -join ",`n") + "`n$pad]"
    }
    return (Format-Json ([string]$v))
}

function Write-JsonFile([string]$path, $value) {
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    [IO.File]::WriteAllText($path, (Format-Json $value) + "`n", [Text.UTF8Encoding]::new($false))
}

# FNV-1a 32-bit of a string: stable default seeds from names
function Get-NameSeed([string]$s) {
    [uint64]$h = 2166136261
    foreach ($b in [Text.Encoding]::UTF8.GetBytes($s)) { $h = (($h -bxor $b) * 16777619) % 4294967296 }
    return [long]$h
}
