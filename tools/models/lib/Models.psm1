# GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
# Shared code of the model converter (contract "geo", unit F2): compiles the C# converter
# (tools/models/lib/*.cs), runs it against the shared TF checkout and writes or checks
# assets/gen/geo/**. Part of GloamingForest, LGPL-3.0-or-later.
#
# Partitions. assets/gen/geo/<unit>/** belongs to <unit> (docs/spec/owners.tsv, rule R3): a W1 unit
# regenerates its own partition (convert.ps1 -Unit <unit>) and commits it in its pull request. F2
# owns the partitions no unit claims (models: the index and the shared render controllers;
# landmark). -Check compares F2's partitions and every unit partition that is committed.

$script:Root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$script:GenRoot = 'assets/gen/geo'

function Import-GfModelsAssembly {
    if ('Gf.Models.Generator' -as [type]) { return }
    $sources = @(Get-ChildItem (Join-Path $PSScriptRoot '*.cs') | Sort-Object Name | ForEach-Object FullName)
    Add-Type -Path $sources -ReferencedAssemblies 'System.Text.Json', 'System.Memory', 'System.Collections', 'System.Linq', 'System.Runtime', 'System.Console', 'System.Text.Encodings.Web' -WarningAction SilentlyContinue
}

# The TF source tree (src/main/java) of the shared checkout (rule R24).
function Get-GfTfJavaRoot {
    $tf = if ($env:GF_TF_HOME) { $env:GF_TF_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-tf' }
    $java = Join-Path $tf 'src/main/java'
    if (-not (Test-Path (Join-Path $java 'twilightforest/client/model/TFModelLayers.java'))) {
        & pwsh -NoProfile -File (Join-Path $script:Root 'tools/tf/fetch.ps1') -Quiet | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $java)) { throw "The Twilight Forest checkout is missing: run pwsh tools/tf/fetch.ps1" }
    }
    return $java
}

# The unit names of tools/spec/owners.rules.json by unit id, and the owner of each geo partition.
function Get-GfGeoPartitionOwners {
    $rules = Get-Content (Join-Path $script:Root 'tools/spec/owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable
    $names = @{}
    foreach ($k in $rules.units.Keys) { $names[$k] = $rules.units[$k].name }
    $owners = @{}
    foreach ($line in Get-Content (Join-Path $script:Root 'docs/spec/owners.tsv')) {
        $c = $line -split "`t"
        if ($c.Count -ge 3 -and $c[0] -eq 'path' -and $c[1] -match '^assets/gen/geo/([^/*{}]+)/\*\*$') { $owners[$Matches[1]] = $c[2] }
    }
    return @{ Names = $names; Partitions = $owners }
}

# Runs the generator. Returns @{ Files = [ordered] path -> text (every partition); Selected = paths
# a -Java / -Item filter matched (or $null); Log; Errors }.
function Invoke-GfModelsGenerator([string]$Java, [string]$Item) {
    Import-GfModelsAssembly
    $catalog = Get-Content (Join-Path $PSScriptRoot '../catalog.json') -Raw
    $javaRoot = Get-GfTfJavaRoot
    $full = [Gf.Models.Generator]::new($javaRoot, $catalog)
    $full.Run($null, $null, $null)
    $result = @{ Files = [ordered]@{}; Selected = $null; Log = @($full.Log); Errors = [System.Collections.Generic.List[string]]::new() }
    foreach ($e in $full.Errors) { $result.Errors.Add($e) }
    foreach ($k in $full.Files.Keys) { $result.Files[$k] = $full.Files[$k] }
    if ($Java -or $Item) {
        $part = [Gf.Models.Generator]::new($javaRoot, $catalog)
        $part.Run($(if ($Java) { $Java } else { $null }), $null, $(if ($Item) { $Item } else { $null }))
        $result.Log = @($part.Log)
        $result.Selected = @($part.Files.Keys)
    }
    foreach ($e in @(Test-GfModelsOwners $full)) { if ($e) { $result.Errors.Add($e) } }
    return $result
}

# Every id an output names must belong to the unit whose partition holds it (owners.tsv, rule R3).
function Test-GfModelsOwners($generator) {
    $po = Get-GfGeoPartitionOwners
    $owner = @{}
    foreach ($line in Get-Content (Join-Path $script:Root 'docs/spec/owners.tsv')) {
        $c = $line -split "`t"
        if ($c.Count -ge 3 -and $c[0] -in 'entity', 'block', 'item', 'block_item', 'spawn_egg') { $owner["$($c[0])|$($c[1])"] = $po.Names[$c[2]] }
    }
    $index = $generator.Files["$script:GenRoot/models/data/gf/models/index.json"] | ConvertFrom-Json
    $errors = [System.Collections.Generic.List[string]]::new()
    $check = {
        param($entry, $what)
        foreach ($kind in 'entity', 'block', 'item') {
            foreach ($id in @($entry.$kind)) {
                if (-not $id) { continue }
                $o = $owner["$kind|$id"]
                if (-not $o -and $kind -eq 'item') { $o = $owner["block_item|$id"] }
                if (-not $o -and $kind -eq 'item') { $o = $owner["block|$id"] }
                if (-not $o) { $errors.Add("$what names unknown $kind $id") }
                elseif ($o -ne $entry.unit) { $errors.Add("$what is in partition '$($entry.unit)' but $kind $id belongs to '$o'") }
            }
        }
    }
    foreach ($g in $index.geometries) { & $check $g "geometry $($g.identifier)" }
    foreach ($a in $index.attachables) { & $check ([pscustomobject]@{ item = @($a.item); unit = $a.unit }) "attachable $($a.item)" }
    return $errors
}

function Get-GfModelsExisting([string]$Base, [string[]]$Partitions) {
    $out = @()
    foreach ($p in $Partitions) {
        $dir = Join-Path $Base "$script:GenRoot/$p"
        if (-not (Test-Path $dir)) { continue }
        $out += Get-ChildItem $dir -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($Base, $_.FullName).Replace('\', '/') }
    }
    return $out
}

function Get-GfModelsPartition([string]$path) {
    if ($path -match "^$([regex]::Escape($script:GenRoot))/([^/]+)/") { return $Matches[1] }
    return $null
}

# Writes (or with -Check compares) the generated files of a scope:
#   default   the partitions F2 owns (+ with -Check every committed unit partition)
#   -Unit u   the partition of unit u
#   -All      every partition
# -Out writes below another root (the e2e harness materializes every partition into a deployed mod).
# Returns the number of problems.
function Write-GfModelsFiles($result, [string]$Unit, [switch]$All, [switch]$Check, [string]$Out) {
    $base = if ($Out) { $Out } else { $script:Root }
    $po = Get-GfGeoPartitionOwners
    $generatedParts = @($result.Files.Keys | ForEach-Object { Get-GfModelsPartition $_ } | Sort-Object -Unique)
    $ownedByF2 = { param($p) -not $po.Partitions.ContainsKey($p) -or $po.Partitions[$p] -eq 'F2' }
    if ($Unit -and $generatedParts -notcontains $Unit) {
        Write-Host "[geo] no partition '$Unit' (the converter writes: $($generatedParts -join ', '))" -ForegroundColor Red
        return @{ Problems = 1; Files = 0; Partitions = @($Unit) }
    }
    if ($All) { $parts = $generatedParts }
    elseif ($Unit) { $parts = @($Unit) }
    elseif ($null -ne $result.Selected) {
        # -Java / -Item: the partitions of what they matched
        $parts = @($result.Selected | ForEach-Object { Get-GfModelsPartition $_ } | Sort-Object -Unique)
    }
    else {
        $parts = @($generatedParts | Where-Object { & $ownedByF2 $_ })
        if ($Check) { $parts += @($generatedParts | Where-Object { -not (& $ownedByF2 $_) -and (Test-Path (Join-Path $base "$script:GenRoot/$_")) }) }
    }
    $parts = @($parts | Sort-Object -Unique)
    $selected = if ($null -ne $result.Selected) { @{} } else { $null }
    if ($selected) { foreach ($s in $result.Selected) { $selected[$s] = $true } }
    $problems = 0
    $written = 0
    $generated = @{}
    foreach ($path in $result.Files.Keys) {
        $part = Get-GfModelsPartition $path
        if ($parts -notcontains $part) { continue }
        if ($selected -and -not $selected.ContainsKey($path) -and $part -ne 'models') { continue }
        $generated[$path] = $true
        $abs = Join-Path $base $path
        $text = $result.Files[$path]
        if ($Check) {
            if (-not (Test-Path $abs)) { Write-Host "[geo] missing: $path" -ForegroundColor Red; $problems++; continue }
            $old = [IO.File]::ReadAllText($abs).Replace("`r`n", "`n")
            if ($old -ne $text) { Write-Host "[geo] differs: $path" -ForegroundColor Red; $problems++ }
        } else {
            $dir = Split-Path -Parent $abs
            if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
            $old = if (Test-Path $abs) { [IO.File]::ReadAllText($abs).Replace("`r`n", "`n") } else { $null }
            if ($old -ne $text) { [IO.File]::WriteAllText($abs, $text, [Text.UTF8Encoding]::new($false)) }
        }
        $written++
    }
    if (-not $selected) {
        foreach ($path in @(Get-GfModelsExisting $base $parts)) {
            if ($generated.ContainsKey($path)) { continue }
            if ($Check) { Write-Host "[geo] stale (not generated, never hand-edit assets/gen): $path" -ForegroundColor Red; $problems++ }
            else { Remove-Item (Join-Path $base $path) -Force; Write-Host "[geo] removed stale $path" }
        }
        if (-not $Check) {
            foreach ($p in $parts) {
                $dir = Join-Path $base "$script:GenRoot/$p"
                if (-not (Test-Path $dir)) { continue }
                Get-ChildItem $dir -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
                    Where-Object { -not (Get-ChildItem $_.FullName -Force) } | ForEach-Object { Remove-Item $_.FullName -Force }
            }
        }
    }
    return @{ Problems = $problems; Files = $written; Partitions = $parts }
}

Export-ModuleMember -Function Import-GfModelsAssembly, Get-GfTfJavaRoot, Invoke-GfModelsGenerator, Write-GfModelsFiles, Test-GfModelsOwners, Get-GfGeoPartitionOwners
