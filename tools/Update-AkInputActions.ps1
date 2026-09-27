param(
    [string]$Project = 'C:\Users\nickr\Desktop\AnimationSetup'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$assetDirectory = Join-Path $Project 'Assets/Input'
$scriptDirectory = Join-Path $Project 'Assets/Scripts'

if (-not (Test-Path -LiteralPath (Join-Path $Project 'Assets') -PathType Container)) {
    throw "AnimationSetup Assets directory not found: $Project"
}
New-Item -ItemType Directory -Path $assetDirectory, $scriptDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'tests/assets/AK_Player.inputactions') -Destination (Join-Path $assetDirectory 'AK_Player.inputactions') -Force
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AKAimController.cs') -Destination (Join-Path $scriptDirectory 'AKAimController.cs') -Force
Write-Host "Updated AK input asset and controller in $Project"
