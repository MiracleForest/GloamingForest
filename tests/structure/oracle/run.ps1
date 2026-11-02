# Runs the structure kit's Java oracles on the F13 oracle install (%LOCALAPPDATA%\gf-oracle: its
# JDK 21 and the Minecraft 1.21.1 server jar with the libraries NeoForge installed beside it):
#   (default)      ProcessorOracle (the hand model) and TemplateOracle (the same fixtures through the
#                  server jar's StructureTemplate) and checks that they print the same lines;
#   -Lists         regenerates src/feature/structure/JavaListsGolden.inc (StructureTemplate#addToLists
#                  of every 1.21.1 block state) for the selftest structure.java_lists;
#   -Randomizable  lists the 1.21.1 blocks whose block entity is a RandomizableContainer.
# Plain vanilla classes on a class path (no NeoForge, no TF): Bootstrap.bootStrap() registers 1.21.1's
# blocks.
param([switch]$Lists, [switch]$Randomizable)
$ErrorActionPreference = 'Stop'
$root   = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$oracle = Join-Path $env:LOCALAPPDATA 'gf-oracle'
$server = Join-Path $oracle 'server'
$jdk    = Get-ChildItem (Join-Path $oracle 'jdk') -Directory | Sort-Object Name | Select-Object -Last 1
if (-not $jdk -or -not (Test-Path (Join-Path $server 'libraries'))) { throw "no oracle install at $oracle (run the F13 oracle install first)" }
$java  = Join-Path $jdk.FullName 'bin\java.exe'
$javac = Join-Path $jdk.FullName 'bin\javac.exe'

# the vanilla server jar (Mojang names) and its data, then the libraries of NeoForge's legacy class
# path without the loader's own (modlauncher, mixin, access transformers, ...)
$mc = Get-ChildItem (Join-Path $server 'libraries\net\minecraft\server') -Recurse -File |
    Where-Object { $_.Name -match '^server-1\.21\.1-\d{8}\.\d{6}-(srg|extra)\.jar$' } | Sort-Object Name -Descending
if (@($mc).Count -ne 2) { throw 'the oracle install has no 1.21.1 srg / extra server jars' }
$args_ = Get-ChildItem (Join-Path $server 'libraries\net\neoforged\neoforge') -Recurse -File -Filter win_args.txt | Select-Object -First 1
$legacy = ((Get-Content $args_.FullName -Raw) -split '-DlegacyClassPath=')[1] -split '\s' | Select-Object -First 1
$libs = $legacy.Split(';') | Where-Object { $_ -and $_ -notmatch 'neoforged|cpw/mods|minecraftforge|sponge-mixin|nashorn|antlr|accesstransformers|earlydisplay' } |
    ForEach-Object { Join-Path $server $_ }
$cp = (@($mc.FullName) + $libs) -join ';'

$out = Join-Path ([System.IO.Path]::GetTempPath()) 'gf-structure-oracle'
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
& $javac -proc:none -cp $cp -d $out (Join-Path $PSScriptRoot 'ProcessorOracle.java') (Join-Path $PSScriptRoot 'TemplateOracle.java')
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }

# Bootstrap sends System.out through the game's logger: keep what it printed
function Invoke-Oracle([string[]]$OracleArgs) {
    Push-Location $out
    try { $raw = & $java -cp "$out;$cp" TemplateOracle @OracleArgs 2>&1 } finally { Pop-Location }
    $raw | ForEach-Object { if ("$_" -match '\[STDOUT\]: (.*)$') { $Matches[1] } }
}

if ($Lists) {
    $lines = Invoke-Oracle @('lists')
    $file  = Join-Path $root 'src\feature\structure\JavaListsGolden.inc'
    [System.IO.File]::WriteAllText($file, (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "wrote $file ($(@($lines).Count) lines)"
    return
}
if ($Randomizable) {
    Invoke-Oracle @('randomizable')
    return
}
$model = & $java -cp $out ProcessorOracle
$game  = Invoke-Oracle @()
$model
if (Compare-Object @($model) @($game)) {
    Write-Host 'TemplateOracle (the game) differs from ProcessorOracle (the model):'
    $game
    exit 1
}
Write-Host 'TemplateOracle (StructureTemplate of the 1.21.1 server jar) prints the same lines'
