<#
.SYNOPSIS
  Contract "import" (CLI owned by F1; this entry point is an F0 placeholder that F1 replaces).

.DESCRIPTION
  Imports The Twilight Forest 4.8 from the shared checkout (tools/tf/fetch.ps1) into
  assets/gen/import/<unit>/** (partitioned by docs/spec/owners.tsv; deterministic):
  textures, models, blockstates, lang (29 Bedrock locales), tags, loot, recipes, advancements,
  worldgen, data maps, twilight/* registries, particles, sound-event skeleton, BP/RP skeletons for
  every block, item and entity, the creative catalog and the block families. Overrides:
  tools/import/overrides/<unit>.json (drop, rename, JSON-pointer patch). Never opens .ogg / .nbt.

.EXAMPLE
  pwsh tools/import/run.ps1 -Kinds textures,models,lang -Unit trees
  pwsh tools/import/run.ps1 -Check        # the committed tree is reproduced byte for byte
#>
param(
    [string[]]$Kinds = @('textures', 'models', 'blockstates', 'lang', 'tags', 'loot', 'recipes', 'advancements', 'worldgen', 'datamaps',
        'registries', 'particles', 'sounds', 'skeleton', 'catalog', 'families'),
    [string]$Unit,
    [switch]$Check
)
Write-Host '[import] not implemented: tools/import/run.ps1 is a placeholder of the "import" contract (owner F1).' -ForegroundColor Red
exit 1