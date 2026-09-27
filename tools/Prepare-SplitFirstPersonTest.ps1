param(
    [string]$SourceProject = 'C:\Users\nickr\Desktop\AnimationSetup',
    [string]$TestProject = 'build/split-first-person-test'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$source = (Resolve-Path -LiteralPath $SourceProject).Path
if (-not [System.IO.Path]::IsPathRooted($TestProject)) { $TestProject = Join-Path $repo $TestProject }
$test = [System.IO.Path]::GetFullPath($TestProject)

function Copy-Asset([string]$relative) {
    $from = Join-Path $source $relative
    if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { throw "Missing source asset: $relative" }
    $to = Join-Path $test $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $to -Parent) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to -Force
}

Copy-Asset 'ProjectSettings/ProjectSettings.txt'
Copy-Asset 'Assets/Scenes/AnimationSetup.scene'
Copy-Asset 'Assets/Animators/Player.controller'
Copy-Asset 'Assets/Character/Mesh/Player_Body_Full.fbx'
Copy-Asset 'Assets/AK/Animations/Character/A_FP_AK_Idle.fbx'
Copy-Asset 'Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx'
$controller = Get-Content -LiteralPath (Join-Path $source 'Assets/Animators/Player.controller') -Raw
foreach ($match in [regex]::Matches($controller, 'Assets/[^"\r\n]+\.[Ff][Bb][Xx]')) {
    Copy-Asset $match.Value
}
foreach ($directory in @('Assets/Materials', 'Assets/Character/Mesh/Player_Body_Full_Materials',
                         'Assets/AK/Animations/Weapon/A_W_AK_Idle_Materials')) {
    $from = Join-Path $source $directory
    if (Test-Path -LiteralPath $from) {
        $to = Join-Path $test $directory
        New-Item -ItemType Directory -Force -Path $to | Out-Null
        Copy-Item -LiteralPath (Join-Path $from '*') -Destination $to -Recurse -Force
    }
}
& (Join-Path $PSScriptRoot 'Install-AkAnimationSetup.ps1') -ProjectRoot $test
Write-Output "Prepared isolated split-rig test project at $test"
