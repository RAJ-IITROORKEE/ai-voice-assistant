[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repoRoot 'relay\src\LunaRelay\LunaRelay.csproj'
$settings = Join-Path $repoRoot 'relay\appsettings.Local.json'

if (-not (Test-Path -LiteralPath $settings)) {
    throw 'Missing local relay settings. Run: dotnet run --project relay/tools/LunaRelay.Setup -- .'
}

& dotnet run --project $project
exit $LASTEXITCODE
