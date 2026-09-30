param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh',
    [string]$AeRoot = 'C:\Users\nickr\Documents\Unity Projects\AE Master\Assets\AE',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $project 'ProjectSettings/ProjectSettings.txt'))) { throw 'Project is missing.' }
if ((Test-Path -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Arms.scene')) -and -not $Force) {
    throw 'AK arms scene already exists. Pass -Force only when intentionally replacing the generated setup.'
}
$ak = Join-Path $AeRoot 'Weapons/AK/Animations'
$character = Join-Path $ak 'Character'
$weapon = Join-Path $ak 'Weapon'
$armsDest = Join-Path $project 'Assets/AK/Animations/Character'
$weaponDest = Join-Path $project 'Assets/AK/Animations/Weapon'
New-Item -ItemType Directory -Force -Path $armsDest,$weaponDest,(Join-Path $project 'Assets/AK/Rigs'),(Join-Path $project 'Assets/Animators'),(Join-Path $project 'Assets/Input'),(Join-Path $project 'Assets/Scripts') | Out-Null
$armClips = @('Draw','Empty_Reload','Holster','Idle_To_Sprint','Idle','Inspect','Mag_Check','Melee','Regrip','Sprint_To_Idle','Sprint','Tac_Reload','Walk')
$weaponClips = @('Empty_Reload','Fire','Idle','Inspect','Mag_Check','Tac_Reload')
foreach ($name in $armClips) {
    $file = "A_FP_AK_$name.fbx"; Copy-Item -LiteralPath (Join-Path $character $file) -Destination (Join-Path $armsDest $file) -Force
}
foreach ($name in $weaponClips) {
    $file = "A_W_AK_$name.fbx"; Copy-Item -LiteralPath (Join-Path $weapon $file) -Destination (Join-Path $weaponDest $file) -Force
}
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AkArmsController.cs') -Destination (Join-Path $project 'Assets/Scripts/AkArmsController.cs') -Force
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AkGunPivot.cs') -Destination (Join-Path $project 'Assets/Scripts/AkGunPivot.cs') -Force
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AE_AK_Arms.rig') -Destination (Join-Path $project 'Assets/AK/Rigs/AE_AK.rig') -Force
$textureDir=Join-Path $project 'Assets/AK/Textures'
New-Item -ItemType Directory -Force -Path $textureDir | Out-Null
Copy-Item -LiteralPath (Join-Path $AeRoot '../Body_Male/Arms/T_Quantum_Basemesh_Arms_BaseColor.1003.png') -Destination (Join-Path $textureDir 'Arms_BaseColor.png') -Force
Copy-Item -LiteralPath (Join-Path $AeRoot '../Body_Male/Arms/T_Quantum_Basemesh_Arms_Unity_Normal.1003.png') -Destination (Join-Path $textureDir 'Arms_Normal.png') -Force
foreach ($texture in @('aks74u_AlbedoTransparency.png','aks74u_Normal.png','aks74u_MetallicSmoothness.png')) {
    Copy-Item -LiteralPath (Join-Path $AeRoot "Weapons/AK/Materials/Textures/$texture") -Destination (Join-Path $textureDir $texture) -Force
}
@'
TheEngineMaterial 1
shader "Standard"
albedo 1 1 1
albedoMap "Assets/AK/Textures/Arms_BaseColor.png"
normalMap "Assets/AK/Textures/Arms_Normal.png"
maskMap ""
metallic 0
smoothness 0.45
normalStrength 1
tiling 1 1
emission 0 0 0
'@ | Set-Content -LiteralPath (Join-Path $project 'Assets/AK/Arms.mat')
@'
TheEngineMaterial 1
shader "Standard"
albedo 1 1 1
albedoMap "Assets/AK/Textures/aks74u_AlbedoTransparency.png"
normalMap "Assets/AK/Textures/aks74u_Normal.png"
maskMap "Assets/AK/Textures/aks74u_MetallicSmoothness.png"
metallic 0.65
smoothness 0.4
normalStrength 1
tiling 1 1
emission 0 0 0
'@ | Set-Content -LiteralPath (Join-Path $project 'Assets/AK/Weapon.mat')

function State([string]$label,[string]$file,[bool]$loop,[int]$row) {
    $l = if ($loop) { 1 } else { 0 }
    return "state `"$label`" clip `"$file`" `"`" `"`" 1 $l 320 $row"
}
function Transition([string]$from,[string]$to,[string]$param,[string]$mode,[bool]$exit=$false) {
    $e = if ($exit) { 1 } else { 0 }
    $at = if ($exit) { '0.95' } else { '0' }
    return "transition `"$from`" `"$to`" $e $at 0.12 interrupt none ordered 0`n  condition `"$param`" $mode 0"
}
function ReturnToIdle([string]$from) { return "transition `"$from`" `"Idle`" 1 0.95 0.12 interrupt none ordered 0" }
$a = [System.Collections.Generic.List[string]]::new()
$a.Add('TheEngineAnimator 5')
foreach ($param in @('Moving','Sprinting')) { $a.Add("param `"$param`" bool 0") }
foreach ($param in @('MagCheck','Inspect','Reload','Draw','Holster','Melee','Regrip','ReloadEmpty')) { $a.Add("param `"$param`" trigger 0") }
$a.Add('layer "Arms" 1 override')
$armStates = [ordered]@{ Idle='Idle'; Walk='Walk'; Sprint='Sprint'; Inspect='Inspect'; MagCheck='Mag_Check'; Reload='Tac_Reload'; ReloadEmpty='Empty_Reload'; Draw='Draw'; Holster='Holster'; Melee='Melee'; Regrip='Regrip'; IdleToSprint='Idle_To_Sprint'; SprintToIdle='Sprint_To_Idle' }
$row=100
foreach ($state in $armStates.GetEnumerator()) {
    $a.Add((State $state.Key "Assets/AK/Animations/Character/A_FP_AK_$($state.Value).fbx" ($state.Key -in @('Idle','Walk','Sprint')) $row)); $row += 90
}
$a.Add((Transition 'Idle' 'Walk' 'Moving' 'if'))
$a.Add((Transition 'Walk' 'Idle' 'Moving' 'ifnot'))
$a.Add((Transition 'Walk' 'IdleToSprint' 'Sprinting' 'if'))
$a.Add((ReturnToIdle 'IdleToSprint').Replace('"Idle"', '"Sprint"'))
$a.Add((Transition 'Sprint' 'SprintToIdle' 'Sprinting' 'ifnot'))
$a.Add((ReturnToIdle 'SprintToIdle'))
foreach ($action in @('MagCheck','Inspect','Reload','ReloadEmpty','Draw','Holster','Melee','Regrip')) {
    $a.Add((Transition 'Any State' $action $action 'if')); $a.Add((ReturnToIdle $action))
}
$a.Add('default "Idle"'); $a.Add('entry 40 100'); $a.Add('any 40 300')
$a.Add('layer "Aim" 0 override')
$a.Add('state "Aim" clip "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx" "" "" 0 1 320 100')
$a.Add('default "Aim"'); $a.Add('entry 40 100'); $a.Add('any 40 300')
Set-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Arms.controller') -Value $a

$w = [System.Collections.Generic.List[string]]::new()
$w.Add('TheEngineAnimator 5')
foreach ($param in @('Fire','MagCheck','Inspect','Reload','ReloadEmpty')) { $w.Add("param `"$param`" trigger 0") }
$w.Add('layer "Weapon" 1 override')
$weaponStates=[ordered]@{ Idle='Idle'; Fire='Fire'; MagCheck='Mag_Check'; Inspect='Inspect'; Reload='Tac_Reload'; ReloadEmpty='Empty_Reload' }
$row=100
foreach ($state in $weaponStates.GetEnumerator()) {
    $w.Add((State $state.Key "Assets/AK/Animations/Weapon/A_W_AK_$($state.Value).fbx" ($state.Key -eq 'Idle') $row)); $row+=100
}
foreach ($action in @('Fire','MagCheck','Inspect','Reload','ReloadEmpty')) {
    if ($action -eq 'Fire') {
        $w.Add('transition "Any State" "Fire" 0 0 0.03 interrupt none ordered 0')
        $w.Add('  condition "Fire" if 0')
        $w.Add('transition "Fire" "Fire" 0 0 0.02 interrupt none ordered 0')
        $w.Add('  condition "Fire" if 0')
        $w.Add('transition "Fire" "Idle" 1 0.95 0.04 interrupt none ordered 0')
    } else {
        $w.Add((Transition 'Any State' $action $action 'if')); $w.Add((ReturnToIdle $action))
    }
}
$w.Add('default "Idle"'); $w.Add('entry 40 100'); $w.Add('any 40 300')
$w.Add('layer "Aim" 0 override')
$w.Add('state "Aim" clip "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx" "" "" 0 1 320 100')
$w.Add('default "Aim"'); $w.Add('entry 40 100'); $w.Add('any 40 300')
Set-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Weapon.controller') -Value $w

$map=[ordered]@{name='Player';actions=@();bindings=@()}
function AddAction([string]$name,[string]$type,[string]$control) {
    $map.actions += [ordered]@{name=$name;type=$type;expectedControlType=$control}
}
function AddBinding([string]$action,[string]$path,[string]$part='',[bool]$composite=$false,[bool]$isPart=$false,[string]$processors='') {
    $map.bindings += [ordered]@{name=$part;path=$path;processors=$processors;action=$action;isComposite=$composite;isPartOfComposite=$isPart}
}
AddAction 'Move' 'Value' 'Vector2'; AddBinding 'Move' '2DVector(mode=1)' '' $true
foreach($item in @(@('up','w'),@('down','s'),@('left','a'),@('right','d'))) { AddBinding 'Move' "<Keyboard>/$($item[1])" $item[0] $false $true }
AddAction 'Look' 'Value' 'Vector2'; AddBinding 'Look' '<Mouse>/delta' '' $false $false 'scaleVector2(x=3,y=3)'
foreach($item in @(@('Sprint','<Keyboard>/leftShift'),@('Fire','<Mouse>/leftButton'),@('ToggleFireMode','<Keyboard>/x'),@('MagCheck','<Keyboard>/m'),@('Inspect','<Keyboard>/i'),@('Reload','<Keyboard>/r'))) {
    AddAction $item[0] 'Button' 'Button'; AddBinding $item[0] $item[1]
}
AddAction 'Aim' 'Button' 'Button'; AddBinding 'Aim' '<Mouse>/rightButton'
AddAction 'Jump' 'Button' 'Button'; AddBinding 'Jump' '<Keyboard>/space'
$asset=[ordered]@{version=1;name='AK Controls';maps=@($map);controlSchemes=@()}
$asset | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $project 'Assets/Input/AK_Controls.inputactions')

$scene=@'
TheEngineScene 2
name "AK Arms"
sky 1 0.04 5 1 0.5 0.5 0.5 0.369 0.349 0.341 1.3 1 0.45 0.85 1 0.6 1 0.19 0.3 0.47 80 1 1 2 1
entity 3 0 1 "Player"
  transform 0 0 0 0 0 0 1 1 1 1 0 0 0
  charactercontroller 1 1.8 0.3 0 0.9 0 50 0.3
  script 1 "AkArmsController"
    field "cameraObject" "GameObject" "entity:1"
    field "armsObject" "GameObject" "entity:4"
    field "weaponObject" "GameObject" "entity:5"
    field "aimPointObject" "GameObject" "entity:10"
entity 1 3 1 "Main Camera"
  transform 0 1.65 0 0 0 0 1 1 1 1 0 0 0
  camera 1 90 0.03 1000 0 5
entity 4 1 1 "AK Arms"
  transform -0.00316192 -1.57135 0.204972 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx#0" 1 1 1 0 0.5 "Assets/AK/Arms.mat" 1
  animator 1 "Assets/Animators/AK_Arms.controller" 0 "" 0 "Assets/AK/Rigs/AE_AK.rig" 1
  script 1 "AkGunPivot"
entity 5 4 1 "AK Weapon"
  transform 0 0 0 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#0" 1 1 1 0 0.5 "Assets/AK/Weapon.mat" 1
  animator 1 "Assets/Animators/AK_Weapon.controller" 0 "" 0 "" 1
  socket 1 "vb_ak_weapon" 0 0 0 0 0 0 1
  prefab "Assets/AK/Prefabs/AK_Weapon.prefab" 1 "object:2" "transform:1" "transform:2" "transform:3" "transform:4" "transform:5" "transform:6" "transform:7" "transform:11" "transform:12" "transform:13"
entity 6 5 1 "AK Weapon Part"
  transform 0 0 0 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#1" 1 1 1 0 0.5 "Assets/AK/Weapon.mat" 1
  prefab "" 2
entity 10 5 1 "AimPoint"
  transform 0 0.0572885 0.141343 0 0 0 1 1 1 1 0 0 0
  prefab "" 3
entity 2 0 1 "Directional Light"
  transform 0 3 0 -0.408218 -0.23457 -0.109382 0.875426 1 1 1 -50 -30 0
  light 1 1 0.957 0.839 1 1 1 0 10 30 21.8
entity 11 0 1 "Ground"
  transform 0 -0.5 0 0 0 0 1 40 1 40 0 0 0
  mesh 1 "Cube" 0.35 0.38 0.42 0 0.4 "" 1
  collider 1 0 0 0 0 1 1 1 0.5 2 0 0.5 0
entity 12 0 1 "Movement Test Wall"
  transform 2 1 0 0 0 0 1 1 2 2 0 0 0
  mesh 1 "Cube" 0.45 0.5 0.6 0 0.4 "" 1
  collider 1 0 0 0 0 1 1 1 0.5 2 0 0.5 0
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Arms.scene') -Value $scene
& (Join-Path $repo 'tools/Install-AkWeaponPrefab.ps1') -ProjectRoot $project
$settings=Join-Path $project 'ProjectSettings/ProjectSettings.txt'
$text=Get-Content -LiteralPath $settings -Raw
$text=[regex]::Replace($text,'(?m)^lastScene ".*"','lastScene "Assets/Scenes/AK_Arms.scene"')
Set-Content -LiteralPath $settings -Value $text -NoNewline
Write-Output "Configured AK arms, weapon, 19 FBX clips, input asset, controllers, and scene in $project"
