param([string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$controllerPath = Join-Path $project 'Assets/Animators/AK_Arms.controller'
$actionsPath = Join-Path $project 'Assets/Input/AK_Controls.inputactions'
if (-not (Test-Path -LiteralPath $controllerPath) -or -not (Test-Path -LiteralPath $actionsPath)) {
    throw 'The AK controller and input-action asset must already exist.'
}

$controller = Get-Content -LiteralPath $controllerPath -Raw
$controller = [regex]::Replace($controller, '^TheEngineAnimator \d+', 'TheEngineAnimator 5')
if ($controller -notmatch '(?m)^layer "Aim"') {
    $controller += @'
layer "Aim" 0 override
state "Aim" clip "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx" "" "" 0 1 320 100
default "Aim"
entry 40 100
any 40 300
'@
}
Set-Content -LiteralPath $controllerPath -Value $controller

$actions = Get-Content -LiteralPath $actionsPath -Raw | ConvertFrom-Json
$player = @($actions.maps | Where-Object name -eq 'Player')[0]
if (-not @($player.actions | Where-Object name -eq 'Aim').Count) {
    $player.actions += [pscustomobject]@{ name='Aim'; type='Button'; expectedControlType='Button' }
    $player.bindings += [pscustomobject]@{ name=''; path='<Mouse>/rightButton'; processors=''; action='Aim'; isComposite=$false; isPartOfComposite=$false }
    $actions | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $actionsPath
}
& (Join-Path $repo 'tools/Install-AkWeaponPrefab.ps1') -ProjectRoot $project
Write-Output "Enabled camera-aligned AK AimPoint and right-mouse input in $project"
