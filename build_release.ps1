param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference='Stop'
$candidates=@('C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe')
$finder='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if(Test-Path -LiteralPath $finder){
    $installation=& $finder -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if($installation){$candidates+=Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe'}
}
$builder=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if(-not $builder){throw 'Install Visual Studio C++ Build Tools first.'}
Push-Location $PSScriptRoot
try {
    & $builder 'vrc-lyrics.sln' "/p:Configuration=$Configuration" '/p:Platform=x64' `
        '/p:TargetName=vrc-lyrics-3.4' "/p:OutDir=out\$Configuration\" `
        "/p:IntDir=out\3.4-obj\$Configuration\" '/m' '/nologo' '/v:minimal'
    if($LASTEXITCODE -ne 0){throw "Build failed: $LASTEXITCODE"}
    Write-Output (Join-Path $PSScriptRoot "out\$Configuration\vrc-lyrics-3.4.exe")
}finally{Pop-Location}
