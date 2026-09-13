param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$buildCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
)
$finder = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $finder) {
    $installation = & $finder -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if ($installation) { $buildCandidates += Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe' }
}
$builder = $buildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $builder) { throw 'Visual Studio C++ Build Tools not found.' }
Push-Location $PSScriptRoot
try {
    & $builder 'vrc-lyrics.sln' "/p:Configuration=$Configuration" '/p:Platform=x64' `
        '/p:TargetName=vrc-lyrics-3.4-qa' '/p:OutDir=out\qa-build-3.4\' `
        "/p:IntDir=out\qa-build-3.4\obj\" `
        "/p:ForceImportBeforeCppTargets=$PSScriptRoot\ui-test.props" '/m' '/nologo' '/v:minimal'
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Write-Output (Join-Path $PSScriptRoot 'out\qa-build-3.4\vrc-lyrics-3.4-qa.exe')
} finally { Pop-Location }
