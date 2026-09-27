param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationSetup',
    [double]$CameraOffsetX = 0,
    [double]$CameraOffsetY = 0,
    [double]$CameraOffsetZ = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$settings = Join-Path $project 'ProjectSettings/ProjectSettings.txt'
$sourceScene = Join-Path $project 'Assets/Scenes/AnimationSetup.scene'
if (-not (Test-Path -LiteralPath $settings) -or -not (Test-Path -LiteralPath $sourceScene)) {
    throw 'AnimationSetup is missing its project settings or source scene.'
}
foreach ($relative in @(
    'Assets/Character/Mesh/Player_Body_Full.fbx',
    'Assets/AK/Animations/Character/A_FP_AK_Idle.fbx',
    'Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $project $relative))) { throw "Missing $relative" }
}

$rigDir = Join-Path $project 'Assets/AK/Rigs'
New-Item -ItemType Directory -Force -Path $rigDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'tests/assets/AE_AK.rig') -Destination (Join-Path $rigDir 'AE_AK.rig') -Force

$locomotionController = Get-Content -LiteralPath (Join-Path $project 'Assets/Animators/Player.controller') -Raw
if (-not $locomotionController.StartsWith('TheEngineAnimator 1')) {
    throw 'Expected the original AnimationSetup Player.controller (version 1) for the locomotion base layer.'
}
$controller = "TheEngineAnimator 4`nlayer ""Base Layer"" 1 override`n" +
    $locomotionController.Substring($locomotionController.IndexOf("`n") + 1).TrimEnd() + "`n" + @'
layer "AK Upper" 1 override @meshspace @mask "Assets/AK/Masks/UpperBody.mask"
state "AK Idle" clip "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx" "" "" 1 1 320 140
default "AK Idle"
entry 40 140
any 40 300
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Aim.controller') -Value $controller

$maskDir = Join-Path $project 'Assets/AK/Masks'
New-Item -ItemType Directory -Force -Path $maskDir | Out-Null
$mask = @'
TheEngineMask 1
bone "spine_01"
bone "ik_hand_gun"
bone "ik_hand_l"
bone "ik_hand_r"
'@
Set-Content -LiteralPath (Join-Path $maskDir 'UpperBody.mask') -Value $mask

$inputDir = Join-Path $project 'Assets/Input'
New-Item -ItemType Directory -Force -Path $inputDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'tests/assets/AK_Player.inputactions') `
    -Destination (Join-Path $inputDir 'AK_Player.inputactions') -Force

Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AKAimController.cs') -Destination (Join-Path $project 'Assets/Scripts/AKAimController.cs') -Force

$scene = Get-Content -LiteralPath $sourceScene -Raw
$scene = $scene.Replace('name "AnimationSetup"', 'name "AK Aiming"')
$scene = $scene.Replace('animator 1 "Assets/Animators/Player.controller" 1',
    'animator 1 "Assets/Animators/AK_Aim.controller" 1 "spine_01,spine_02,spine_03,spine_04,spine_05" 0 "Assets/AK/Rigs/AE_AK.rig" 1')
$scene = $scene.Replace('script 1 "PlayerController"', 'script 1 "AKAimController"')
$scene += @'
entity 9 4 1 "AK Weapon"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#0" 1 1 1 0 0.5 "" 1
  socket 1 "vb_ak_weapon" 0 0 0 0 0 0 1
entity 10 9 1 "AK Weapon Part 1"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#1" 1 1 1 0 0.5 "" 1
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Aiming.scene') -Value $scene -NoNewline
& (Join-Path $PSScriptRoot 'Set-AkFirstPersonCamera.ps1') -ProjectRoot $project `
    -CameraOffsetX $CameraOffsetX -CameraOffsetY $CameraOffsetY -CameraOffsetZ $CameraOffsetZ

$settingsText = Get-Content -LiteralPath $settings -Raw
$settingsText = $settingsText.Replace('lastScene "Assets/Scenes/AnimationSetup.scene"', 'lastScene "Assets/Scenes/AK_Aiming.scene"')
Set-Content -LiteralPath $settings -Value $settingsText -NoNewline
Write-Output "Installed AK aiming scene in $project"
