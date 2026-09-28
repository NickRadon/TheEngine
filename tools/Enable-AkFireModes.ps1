param([string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$actionsPath = Join-Path $project 'Assets/Input/AK_Controls.inputactions'
$weaponControllerPath = Join-Path $project 'Assets/Animators/AK_Weapon.controller'
if (-not (Test-Path -LiteralPath $actionsPath) -or -not (Test-Path -LiteralPath $weaponControllerPath)) {
    throw 'The AK input asset and weapon controller must already exist in AnimationFresh.'
}

$asset = Get-Content -LiteralPath $actionsPath -Raw | ConvertFrom-Json
$player = @($asset.maps | Where-Object name -eq 'Player')[0]
if (-not @($player.actions | Where-Object name -eq 'ToggleFireMode').Count) {
    $player.actions += [pscustomobject]@{ name='ToggleFireMode'; type='Button'; expectedControlType='Button' }
}
if (-not @($player.bindings | Where-Object { $_.action -eq 'ToggleFireMode' -and $_.path -eq '<Keyboard>/x' }).Count) {
    $player.bindings += [pscustomobject]@{ name=''; path='<Keyboard>/x'; processors=''; action='ToggleFireMode'; isComposite=$false; isPartOfComposite=$false }
}
$asset | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $actionsPath

$controller = Get-Content -LiteralPath $weaponControllerPath -Raw
if ($controller -notmatch '(?m)^state "Fire" ' -or $controller -notmatch '(?m)^param "Fire" trigger') {
    throw 'The weapon controller is missing its authored Fire state or Fire trigger.'
}
if ($controller -notmatch '(?m)^transition "Fire" "Fire"') {
    $self = "transition `"Fire`" `"Fire`" 0 0 0.02 interrupt none ordered 0`n  condition `"Fire`" if 0`n"
    $controller = [regex]::Replace($controller, '(?m)^(transition "Fire" "Idle")', { param($m) $self + $m.Groups[1].Value })
}
$controller = [regex]::Replace($controller, '(?m)^(transition "Any State" "Fire" 0 0 )\S+', '${1}0.03')
$controller = [regex]::Replace($controller, '(?m)^(transition "Fire" "Idle" 1 0.95 )\S+', '${1}0.04')
Set-Content -LiteralPath $weaponControllerPath -Value $controller

Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AkArmsController.cs') -Destination (Join-Path $project 'Assets/Scripts/AkArmsController.cs') -Force
Copy-Item -LiteralPath (Join-Path $repo 'tools/templates/AkGunPivot.cs') -Destination (Join-Path $project 'Assets/Scripts/AkGunPivot.cs') -Force
foreach ($name in @('AK_Arms.scene', 'AK_Grid_Test.scene')) {
    $scenePath = Join-Path $project "Assets/Scenes/$name"
    if (-not (Test-Path -LiteralPath $scenePath)) { continue }
    $scene = Get-Content -LiteralPath $scenePath -Raw
    $pattern = '(?s)(entity 4 1 1 "AK Arms"\r?\n.*?)(?=entity \d+ \d+ \d+ ")'
    $scene = [regex]::Replace($scene, $pattern, {
        param($match)
        $block = $match.Value
        if ($block -notmatch 'script 1 "AkGunPivot"') {
            $block += "  script 1 `"AkGunPivot`"`n"
        }
        return $block
    })
    Set-Content -LiteralPath $scenePath -Value $scene
}
Write-Output "Installed X fire-mode toggle and ik_hand_gun-pivot recoil in $project"
