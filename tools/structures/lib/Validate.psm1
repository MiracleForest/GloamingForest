# Validation of GloamingForest templates (.gft.json) for tools/structures/build.ps1 -Check.
# The structural rules match structure::templates::validate in src/feature/structure/Templates.cpp.

Set-StrictMode -Version Latest

$script:Directions = @('north', 'south', 'east', 'west', 'up', 'down')

function Get-Pos3($p) {
    if ($null -eq $p -or $p -isnot [System.Collections.IList] -or $p.Count -ne 3) { return $null }
    foreach ($v in $p) { if ($v -isnot [int] -and $v -isnot [long] -and $v -isnot [double]) { return $null } }
    return , @([int]$p[0], [int]$p[1], [int]$p[2])
}

function Test-GftDocument {
    param([hashtable]$Doc)
    $errors = [System.Collections.Generic.List[string]]::new()
    if ($Doc['format'] -ne 'gloamingforest:template') { $errors.Add('format must be "gloamingforest:template"') }
    if ($Doc['format_version'] -ne 1) { $errors.Add('format_version must be 1') }
    if (-not ($Doc['id'] -is [string] -and $Doc['id'] -match '^[a-z0-9_.-]+:[a-z0-9_./-]+$')) { $errors.Add('id must be a namespaced id') }
    $size = Get-Pos3 $Doc['size']
    if (-not $size -or $size[0] -le 0 -or $size[1] -le 0 -or $size[2] -le 0) {
        $errors.Add('size must be three positive integers')
        return , $errors.ToArray()
    }
    $palette = $Doc['palette']
    if ($palette -isnot [System.Collections.IList] -or $palette.Count -eq 0) { $errors.Add('palette must be a non-empty array'); $palette = @() }
    foreach ($p in $palette) {
        if ($p -isnot [hashtable] -or $p['Name'] -isnot [string]) { $errors.Add("palette entry without a Name: $($p | ConvertTo-Json -Compress)") }
    }
    $volume = [long]$size[0] * $size[1] * $size[2]
    $total  = [long]0
    $blocks = $Doc['blocks']
    if ($blocks -isnot [System.Collections.IList]) { $errors.Add('blocks must be an array of [palette index, count] runs') }
    else {
        foreach ($run in $blocks) {
            if ($run -isnot [System.Collections.IList] -or $run.Count -ne 2 -or $run[1] -le 0) { $errors.Add("bad block run $($run | ConvertTo-Json -Compress)"); continue }
            if ($run[0] -lt 0 -or $run[0] -ge $palette.Count) { $errors.Add("palette index out of range in [$($run -join ', ')]") }
            $total += [long]$run[1]
        }
    }
    if ($total -ne $volume) { $errors.Add("blocks cover $total cells, size needs $volume") }
    $inside = { param($q) $q -and $q[0] -ge 0 -and $q[1] -ge 0 -and $q[2] -ge 0 -and $q[0] -lt $size[0] -and $q[1] -lt $size[1] -and $q[2] -lt $size[2] }
    foreach ($m in @($Doc['markers'] | Where-Object { $_ })) {
        $q = Get-Pos3 $m['pos']
        if (-not (& $inside $q)) { $errors.Add("marker outside the template: $($m | ConvertTo-Json -Compress -Depth 5)") }
        if ($m['marker'] -isnot [string]) { $errors.Add("marker without a name: $($m | ConvertTo-Json -Compress -Depth 5)") }
        if ($m['marker'] -eq 'data' -and -not ($m['data'] -is [hashtable] -and $m['data'].ContainsKey('metadata'))) { $errors.Add('data marker without data.metadata') }
    }
    foreach ($j in @($Doc['jigsaw'] | Where-Object { $_ })) {
        $q = Get-Pos3 $j['pos']
        if (-not (& $inside $q)) { $errors.Add("jigsaw outside the template: $($j | ConvertTo-Json -Compress)") }
        $front = if ($j.ContainsKey('front')) { $j['front'] } else { $j['facing'] }
        if ($front -notin $script:Directions) { $errors.Add("jigsaw without a front direction: $($j | ConvertTo-Json -Compress)") }
        if ($j.ContainsKey('top') -and $j['top'] -notin $script:Directions) { $errors.Add("jigsaw top must be a direction: $($j | ConvertTo-Json -Compress)") }
    }
    foreach ($e in @($Doc['entities'] | Where-Object { $_ })) {
        $p = $e['pos']
        if ($p -isnot [System.Collections.IList] -or $p.Count -ne 3) { $errors.Add("entity without pos: $($e | ConvertTo-Json -Compress -Depth 5)") }
        elseif ($p[0] -lt 0 -or $p[1] -lt 0 -or $p[2] -lt 0 -or $p[0] -gt $size[0] -or $p[1] -gt $size[1] -or $p[2] -gt $size[2]) {
            $errors.Add("entity outside the template: $($e | ConvertTo-Json -Compress -Depth 5)")
        }
        if ($e['id'] -isnot [string]) { $errors.Add("entity without id: $($e | ConvertTo-Json -Compress -Depth 5)") }
    }
    return , $errors.ToArray()
}

# owners.tsv: kind -> set of ids
function Read-GftOwners([string]$Path) {
    $kinds = @{}
    foreach ($line in Get-Content $Path) {
        if ($line.StartsWith('#') -or $line.StartsWith("kind`t")) { continue }
        $c = $line -split "`t"
        if ($c.Count -lt 3) { continue }
        if (-not $kinds.ContainsKey($c[0])) { $kinds[$c[0]] = [System.Collections.Generic.HashSet[string]]::new() }
        [void]$kinds[$c[0]].Add($c[1])
    }
    return $kinds
}

function Test-Known($owners, [string]$kind, [string]$id) {
    if (-not $id.StartsWith('twilightforest:')) { return $true }
    return $owners.ContainsKey($kind) -and $owners[$kind].Contains($id)
}

function Test-GftReferences {
    param([hashtable]$Doc, $Owners)
    $errors = [System.Collections.Generic.List[string]]::new()
    foreach ($p in @($Doc['palette'] | Where-Object { $_ -is [hashtable] })) {
        if (-not (Test-Known $Owners 'block' $p['Name'])) { $errors.Add("unknown block $($p['Name'])") }
    }
    foreach ($m in @($Doc['markers'] | Where-Object { $_ })) {
        $d = $m['data']
        if ($d -isnot [hashtable]) { continue }
        if ($m['marker'] -eq 'loot' -and $d['table'] -and -not (Test-Known $Owners 'loot_table' $d['table'])) { $errors.Add("unknown loot table $($d['table'])") }
        if ($m['marker'] -eq 'spawner' -and $d['entity'] -and -not (Test-Known $Owners 'entity' $d['entity'])) { $errors.Add("unknown entity $($d['entity'])") }
    }
    foreach ($e in @($Doc['entities'] | Where-Object { $_ })) {
        if ($e['id'] -and -not (Test-Known $Owners 'entity' $e['id'])) { $errors.Add("unknown entity $($e['id'])") }
    }
    return , $errors.ToArray()
}

function Get-GftDataRoots([string]$Root, [string]$Registry) {
    # imported data first (assets/gen/import/<unit>/data/<ns>/twilight/<registry>), else the TF checkout
    $roots = @(Get-ChildItem (Join-Path $Root 'assets/gen/import') -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            Get-ChildItem (Join-Path $_.FullName 'data') -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                $d = Join-Path $_.FullName "twilight/$Registry"
                if (Test-Path $d) { [pscustomobject]@{ Ns = $_.Name; Dir = $d; Source = 'imported' } }
            }
        })
    if ($roots.Count) { return $roots }
    $tf = Join-Path $env:LOCALAPPDATA 'gf-tf/src/generated/resources/data'
    if (Test-Path $tf) {
        return @(Get-ChildItem $tf -Directory | ForEach-Object {
                $d = Join-Path $_.FullName "twilight/$Registry"
                if (Test-Path $d) { [pscustomobject]@{ Ns = $_.Name; Dir = $d; Source = 'TF checkout' } }
            })
    }
    return @()
}

function Read-GftRegistry([string]$Root, [string]$Registry) {
    $out = @{}
    $source = 'none'
    foreach ($r in (Get-GftDataRoots $Root $Registry)) {
        $source = $r.Source
        foreach ($f in Get-ChildItem $r.Dir -Recurse -File -Filter '*.json') {
            $rel = [IO.Path]::GetRelativePath($r.Dir, $f.FullName) -replace '\\', '/' -replace '\.json$', ''
            $out["$($r.Ns):$rel"] = Get-Content $f.FullName -Raw | ConvertFrom-Json -AsHashtable
        }
    }
    return [pscustomobject]@{ Map = $out; Source = $source }
}

function Read-GftTemplateDefinitions([string]$Root) { return Read-GftRegistry $Root 'template_definition' }
function Read-GftMarkerHandlerLists([string]$Root) { return Read-GftRegistry $Root 'template_marker_handler_list' }

# The labels a TF marker handler (rotation / handler_switch / leaf handlers) accepts; $null: any.
function Get-HandlerLabels($handler) {
    switch ($handler['type']) {
        'twilightforest:rotation' { return Get-HandlerLabels $handler['rotated'] }
        'twilightforest:handler_switch' { return @($handler['element_processor'].Keys) }
        default { return $null }
    }
}

function Test-GftAgainstDefinitions {
    param([hashtable]$Templates, [hashtable]$Definitions, [hashtable]$Handlers)
    $errors = [System.Collections.Generic.List[string]]::new()
    # pools filled by the definitions, and the connector names each pool offers
    $pools = @{}
    foreach ($tid in $Definitions.Keys) {
        foreach ($pool in $Definitions[$tid].Keys) {
            if (-not $pools.ContainsKey($pool)) { $pools[$pool] = [System.Collections.Generic.List[string]]::new() }
            $pools[$pool].Add($tid)
        }
    }
    foreach ($id in $Templates.Keys) {
        if (-not $Definitions.ContainsKey($id)) { continue }
        $t   = $Templates[$id]
        $doc = $t.doc
        foreach ($j in @($doc['jigsaw'] | Where-Object { $_ })) {
            $pool = $j['pool']
            if ($pool -and $pool -ne 'minecraft:empty' -and -not $pools.ContainsKey($pool)) { $errors.Add("$($t.rel): jigsaw pool $pool is filled by no template definition") }
            if ($pool -and $pools.ContainsKey($pool) -and $j['target']) {
                # a target must be the name of a connector of a designed template of the pool
                $designed = @($pools[$pool] | Where-Object { $Templates.ContainsKey($_) })
                if ($designed.Count) {
                    $names = @($designed | ForEach-Object { $Templates[$_].doc['jigsaw'] } | Where-Object { $_ } | ForEach-Object { $_['name'] })
                    if ($j['target'] -notin $names) { $errors.Add("$($t.rel): jigsaw target $($j['target']) matches no connector name of pool $pool") }
                }
            }
        }
        # data markers against the definition's marker handler lists
        foreach ($instance in $Definitions[$id].Values) {
            if ($instance -isnot [hashtable] -or -not $instance['marker_handlers']) { continue }
            $listId = $instance['marker_handlers']
            if ($listId -isnot [string]) { continue }
            if (-not $Handlers.ContainsKey($listId)) { $errors.Add("$($t.rel): marker handler list $listId is not defined"); continue }
            $labels = [System.Collections.Generic.HashSet[string]]::new()
            $any    = $false
            foreach ($h in $Handlers[$listId]) {
                $l = Get-HandlerLabels $h
                if ($null -eq $l) { $any = $true } else { foreach ($x in $l) { [void]$labels.Add($x) } }
            }
            if ($any) { continue }
            foreach ($m in @($doc['markers'] | Where-Object { $_ -and $_['marker'] -eq 'data' })) {
                $label = [string]$m['data']['metadata']
                $keys  = @(($label -split '@', 2)[0] -split '\|' | ForEach-Object { $_ })
                if (-not ($keys | Where-Object { $labels.Contains($_) })) { $errors.Add("$($t.rel): data marker '$label' is not handled by $listId") }
            }
        }
    }
    return , $errors.ToArray()
}

Export-ModuleMember -Function Test-GftDocument, Test-GftReferences, Read-GftOwners, Read-GftTemplateDefinitions, Read-GftMarkerHandlerLists, Test-GftAgainstDefinitions
