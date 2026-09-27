param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationSetup',
    [string]$OutputDir = 'build/test-captures/animation-setup-split',
    [string]$ReviewDir = 'docs/test-captures/animation-setup-ak',
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Aiming.scene'))) {
    throw 'AnimationSetup AK_Aiming.scene is missing. Run Install-AkAnimationSetup.ps1 first.'
}
$sceneText = Get-Content -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Aiming.scene') -Raw
if (-not $sceneText.Contains('entity 1 4 1 "Main Camera"') -or
    -not $sceneText.Contains('socket 1 "head" 0 0 0 0 0 0 0') -or
    $sceneText.Contains('entity 11 4 1 "Head Camera Anchor"') -or
    $sceneText.Contains('script 1 "ThirdPersonCamera"')) {
    throw 'AK camera must be a Player child that copies head position without bone rotation.'
}
$splitSetup = $sceneText.Contains('script 1 "SplitFirstPersonController"')
$scriptName = if ($splitSetup) { 'SplitFirstPersonController.cs' } else { 'AKAimController.cs' }
$aimScript = Get-Content -LiteralPath (Join-Path $project "Assets/Scripts/$scriptName") -Raw
if (-not $aimScript.Contains('localRotation = Quaternion.Euler(pitch, 0f, 0f)') -or
    -not $aimScript.Contains('transform.Rotate(0f, -yawDelta, 0f)')) {
    throw 'AK camera rotation is not driven by the Player aim input.'
}
$inputPath = Join-Path $project 'Assets/Input/AK_Player.inputactions'
if (-not (Test-Path -LiteralPath $inputPath)) { throw 'AK input-action asset is missing.' }
$inputAsset = Get-Content -LiteralPath $inputPath -Raw | ConvertFrom-Json
$playerMap = @($inputAsset.maps | Where-Object { $_.name -eq 'Player' })
if ($playerMap.Count -ne 1 -or @($playerMap[0].actions | Where-Object { $_.name -eq 'Look' }).Count -ne 1 -or
    @($playerMap[0].bindings | Where-Object { $_.path -eq '<Mouse>/delta' -and $_.action -eq 'Look' }).Count -ne 1) {
    throw 'AK input-action asset has no Player/Look mouse-delta binding.'
}
if (-not $sceneText.Contains('"spine_01,spine_02,spine_03,spine_04,spine_05"')) {
    throw 'AK look rotation must use the AE Master five-bone spine chain.'
}
$controllerName = if ($splitSetup) { 'AK_View.controller' } else { 'AK_Aim.controller' }
$controllerText = Get-Content -LiteralPath (Join-Path $project "Assets/Animators/$controllerName") -Raw
$maskPath = Join-Path $project 'Assets/AK/Masks/UpperBody.mask'
$maskText = if (Test-Path -LiteralPath $maskPath) { Get-Content -LiteralPath $maskPath -Raw } else { '' }
$inlineSplit = $controllerText.Contains('layer "AK Upper" 1 override "spine_01" "ik_hand_gun" "ik_hand_l" "ik_hand_r" @meshspace')
$assetSplit = $controllerText.Contains('layer "AK Upper" 1 override @meshspace @mask "Assets/AK/Masks/UpperBody.mask"') -and
    $maskText.Contains('bone "spine_01"') -and $maskText.Contains('bone "ik_hand_gun"') -and
    $maskText.Contains('bone "ik_hand_l"') -and $maskText.Contains('bone "ik_hand_r"')
if ($splitSetup) {
    if (-not $sceneText.Contains('entity 11 1 1 "View Rig"') -or
        -not $sceneText.Contains('entity 6 11 1 "Arms"') -or
        -not $sceneText.Contains('entity 9 11 1 "AK Weapon"') -or
        -not $sceneText.Contains('animator 1 "Assets/Animators/AK_View.controller"') -or
        -not $controllerText.Contains('state "AK Idle" clip')) {
        throw 'The body and view arms are not separate Animator rigs.'
    }
}
elseif (-not $controllerText.Contains('layer "Base Layer" 1 override') -or -not ($inlineSplit -or $assetSplit)) {
    throw 'The locomotion base and spine_01 AK override mask are not configured.'
}
if (-not [System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path $repo $OutputDir }
if (-not [System.IO.Path]::IsPathRooted($ReviewDir)) { $ReviewDir = Join-Path $repo $ReviewDir }
New-Item -ItemType Directory -Force -Path $OutputDir,$ReviewDir | Out-Null

if (-not $SkipBuild) {
    & cmake --build (Join-Path $repo 'build') --config Debug
    if ($LASTEXITCODE -ne 0) { throw 'TheEngine Debug build failed.' }
}
$engine = Join-Path $repo 'build/Debug/TheEngine.exe'
Add-Type -AssemblyName System.Drawing
foreach ($case in @(
    @{ name = 'locomotion'; keys = 'W'; image = 'split-locomotion.png'; external = 'split-locomotion-external.png' },
    @{ name = 'aim-locomotion'; keys = 'W,I,J'; image = 'split-aim-locomotion.png'; external = 'split-aim-locomotion-external.png' },
    @{ name = 'mouse-look'; keys = 'W'; mouse = '60,-30'; image = 'split-mouse-look.png'; external = 'split-mouse-look-external.png' },
    @{ name = 'look-down'; keys = 'W,K'; seconds = 4; frame = '07'; image = 'split-look-down.png'; external = 'split-look-down-external.png' }
)) {
    $capture = Join-Path $OutputDir $case.name
    New-Item -ItemType Directory -Force -Path $capture | Out-Null
    $previous = $ErrorActionPreference
    $seconds = if ($case.ContainsKey('seconds')) { $case.seconds } else { 2 }
    try {
        $ErrorActionPreference = 'Continue'
        if ($case.ContainsKey('mouse')) {
            $log = & $engine --project $project --playtest $seconds --capture-dir $capture --hold $case.keys --mouse $case.mouse 2>&1
        }
        else {
            $log = & $engine --project $project --playtest $seconds --capture-dir $capture --hold $case.keys 2>&1
        }
        $exit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previous }
    $log | Out-File -LiteralPath (Join-Path $capture 'playtest.log') -Encoding utf8
    if ($exit -ne 0) { throw "$($case.name) playtest failed with exit $exit" }
    $lines = $log | ForEach-Object { $_.ToString() }
    $expectedBody = if ($splitSetup) { 'Player pos' } else { 'state Locomotion | AK Upper: AK Idle' }
    if (-not ($lines | Select-String -SimpleMatch $expectedBody) -or
        ($splitSetup -and (-not ($lines | Select-String -SimpleMatch 'state Locomotion') -or
                           -not ($lines | Select-String -SimpleMatch 'View Rig pos') -or
                           -not ($lines | Select-String -SimpleMatch 'state AK Idle')))) {
        throw "$($case.name): locomotion body and FP arms were not both active."
    }
    $positions = @($lines | Select-String -Pattern 'Player pos \([^)]* ([-0-9.]+)\)' |
        ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
    if ($positions.Count -eq 0 -or [Math]::Abs($positions[-1]) -lt 0.25) {
        throw "$($case.name): the locomotion base did not move the player forward."
    }
    $errors = @($lines | Select-String -Pattern 'hand error ([0-9.eE+-]+) m' -AllMatches |
        ForEach-Object { foreach ($match in $_.Matches) { [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) } })
    if ($errors.Count -eq 0 -or ($errors | Measure-Object -Maximum).Maximum -gt 0.005) {
        throw "$($case.name): a hand missed its AK grip by more than 5 mm."
    }
    if (-not ($lines | Select-String -SimpleMatch '[playtest] camera pos')) {
        throw "$($case.name): no first-person camera transform was reported."
    }
    if ($splitSetup) {
        $cameraDirection = @($lines | Select-String -Pattern 'camera pos .* forward \(([-0-9.]+) ([-0-9.]+) ([-0-9.]+)\)')[-1]
        $weaponDirection = @($lines | Select-String -Pattern 'weapon forward \(([-0-9.]+) ([-0-9.]+) ([-0-9.]+)\)')[-1]
        if (-not $cameraDirection -or -not $weaponDirection) { throw "$($case.name): camera or world-space weapon direction is missing." }
        $dot = 0.0
        for ($axis = 1; $axis -le 3; $axis++) {
            $a = [double]::Parse($cameraDirection.Matches[0].Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
            $b = [double]::Parse($weaponDirection.Matches[0].Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
            $dot += $a * $b
        }
        if ($dot -lt 0.95) { throw "$($case.name): weapon does not rotate with the camera (forward dot $dot)." }
    }
    if ($case.name -eq 'mouse-look') {
        $forwards = @($lines | Select-String -Pattern 'camera pos .* forward \(([-0-9.]+)' |
            ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        if ($forwards.Count -eq 0 -or $forwards[-1] -lt 0.2) {
            throw 'Mouse X did not turn the camera right without holding a button.'
        }
        $upwards = @($lines | Select-String -Pattern 'camera pos .* forward \([-0-9.]+ ([-0-9.]+)' |
            ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        if ($upwards.Count -eq 0 -or $upwards[-1] -lt 0.1) {
            throw 'Mouse Y did not pitch the camera up in the expected direction.'
        }
    }
    if ($case.name -eq 'look-down') {
        $down = @($lines | Select-String -Pattern 'camera pos .* forward \([-0-9.]+ ([-0-9.]+)' |
            ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        if ($down.Count -eq 0 -or $down[-1] -gt -0.45) { throw 'Look-down input did not pitch the camera toward the legs.' }
    }
    $frame = if ($case.ContainsKey('frame')) { $case.frame } else { '03' }
    $source = Join-Path $capture "play_$frame.bmp"
    if (-not (Test-Path -LiteralPath $source)) { throw "$($case.name): the visual capture is missing." }
    $bitmap = [System.Drawing.Image]::FromFile($source)
    try { $bitmap.Save((Join-Path $ReviewDir $case.image), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $externalSource = Join-Path $capture "scene_play_$frame.bmp"
    if (-not (Test-Path -LiteralPath $externalSource)) { throw "$($case.name): the external view capture is missing." }
    $external = [System.Drawing.Image]::FromFile($externalSource)
    try { $external.Save((Join-Path $ReviewDir $case.external), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $external.Dispose() }
    Write-Output "$($case.name): body and view rigs active; maximum hand error $(($errors | Measure-Object -Maximum).Maximum) m; screenshot $($case.image)"
}
