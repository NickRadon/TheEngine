param([string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$scenes = @(Join-Path $project 'Assets/Scenes/AK_Arms.scene') + @(Join-Path $project 'Assets/Scenes/AK_Grid_Test.scene')
$rootOverrides = '  prefab "Assets/AK/Prefabs/AK_Weapon.prefab" 1 "object:2" "transform:1" "transform:2" "transform:3" "transform:4" "transform:5" "transform:6" "transform:7" "transform:11" "transform:12" "transform:13"'
$aimTransform = '  transform 0 0.0572885 0.141343 0 0 0 1 1 1 1 0 0 0'
$aimEntity = 10

foreach ($scenePath in $scenes) {
    if (-not (Test-Path -LiteralPath $scenePath)) { continue }
    $scene = Get-Content -LiteralPath $scenePath -Raw
    if ($scene -notmatch 'entity 5 4 1 "AK Weapon"' -or $scene -notmatch 'entity 6 5 1 "AK Weapon Part"') {
        throw "Expected AK weapon hierarchy in $scenePath"
    }
    $existingAim = [regex]::Match($scene, '(?m)^entity (\d+) 5 1 "AimPoint"')
    $sceneAimEntity = if ($existingAim.Success) { [int]$existingAim.Groups[1].Value } else { $aimEntity }
    if ($scene -notmatch 'field "aimPointObject"') {
        $scene = $scene.Replace('    field "weaponObject" "GameObject" "entity:5"', "    field `"weaponObject`" `"GameObject`" `"entity:5`"`n    field `"aimPointObject`" `"GameObject`" `"entity:$sceneAimEntity`"")
    }
    $scene = [regex]::Replace($scene, '(?m)^    field "aimPointObject" "GameObject" "entity:\d+"', "    field `"aimPointObject`" `"GameObject`" `"entity:$sceneAimEntity`"")
    $scene = [regex]::Replace($scene, '(?m)^  prefab "Assets/AK/Prefabs/AK_Weapon.prefab"[^\r\n]*\r?\n', '')
    $scene = [regex]::Replace($scene, '(?m)^  prefab "" 2\r?\n', '')
    $scene = [regex]::Replace($scene, '(?m)^(entity 5 4 1 "AK Weapon"\r?\n(?:^(?!entity )[\s\S]*?))(?=^entity 6 5 1)', { param($m) $m.Groups[1].Value.TrimEnd("`r", "`n") + "`n$rootOverrides`n" })
    $scene = [regex]::Replace($scene, '(?m)^(entity 6 5 1 "AK Weapon Part"\r?\n(?:^(?!entity )[\s\S]*?))(?=^entity )', {
        param($m)
        $part = $m.Groups[1].Value.TrimEnd("`r", "`n") + "`n  prefab `"`" 2`n"
        if (-not $existingAim.Success) { $part += "entity $sceneAimEntity 5 1 `"AimPoint`"`n$aimTransform`n  prefab `"`" 3`n" }
        $part
    })
    $scene = [regex]::Replace($scene, '(?m)^    field "aimRigOffset[XYZ]"[^\r\n]*\r?\n', '')
    Set-Content -LiteralPath $scenePath -Value $scene
}

$prefabDir = Join-Path $project 'Assets/AK/Prefabs'
New-Item -ItemType Directory -Force -Path $prefabDir | Out-Null
$prefab = @'
TheEnginePrefab 1
entity 1 0 1 "AK Weapon"
  transform 0 0 0 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#0" 1 1 1 0 0.5 "Assets/AK/Weapon.mat" 1
  animator 1 "Assets/Animators/AK_Weapon.controller" 0 "" 0 "" 1
  socket 1 "vb_ak_weapon" 0 0 0 0 0 0 1
entity 2 1 1 "AK Weapon Part"
  transform 0 0 0 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#1" 1 1 1 0 0.5 "Assets/AK/Weapon.mat" 1
entity 3 1 1 "AimPoint"
  transform 0 0.0572885 0.141343 0 0 0 1 1 1 1 0 0 0
'@
$prefabPath = Join-Path $prefabDir 'AK_Weapon.prefab'
if (-not (Test-Path -LiteralPath $prefabPath)) { Set-Content -LiteralPath $prefabPath -Value $prefab }

$controllerPath = Join-Path $project 'Assets/Animators/AK_Arms.controller'
if (Test-Path -LiteralPath $controllerPath) {
    $controller = Get-Content -LiteralPath $controllerPath -Raw
    $aimLayer = @'
layer "Aim" 0 override
state "Aim" clip "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx" "" "" 0 1 320 100
default "Aim"
entry 40 100
any 40 300
'@
    if ($controller -match '(?m)^layer "Aim"') {
        $controller = [regex]::Replace($controller, '(?ms)^layer "Aim".*\z', $aimLayer + "`n")
    } else { $controller = $controller.TrimEnd() + "`n" + $aimLayer + "`n" }
    Set-Content -LiteralPath $controllerPath -Value $controller
}
$weaponControllerPath = Join-Path $project 'Assets/Animators/AK_Weapon.controller'
if (Test-Path -LiteralPath $weaponControllerPath) {
    $controller = Get-Content -LiteralPath $weaponControllerPath -Raw
    $aimLayer = @'
layer "Aim" 0 override
state "Aim" clip "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx" "" "" 0 1 320 100
default "Aim"
entry 40 100
any 40 300
'@
    if ($controller -match '(?m)^layer "Aim"') {
        $controller = [regex]::Replace($controller, '(?ms)^layer "Aim".*\z', $aimLayer + "`n")
    } else { $controller = $controller.TrimEnd() + "`n" + $aimLayer + "`n" }
    Set-Content -LiteralPath $weaponControllerPath -Value $controller
}
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AkArmsController.cs') -Destination (Join-Path $project 'Assets/Scripts/AkArmsController.cs') -Force
Write-Output "Installed AK weapon prefab and camera-aligned AimPoint in $project"
