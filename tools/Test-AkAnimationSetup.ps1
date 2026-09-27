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
$aimScript = Get-Content -LiteralPath (Join-Path $project 'Assets/Scripts/AKAimController.cs') -Raw
if (-not $aimScript.Contains('cameraTransform.localRotation = Quaternion.Euler(pitch, yaw, 0f)')) {
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
$controllerText = Get-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Aim.controller') -Raw
$maskPath = Join-Path $project 'Assets/AK/Masks/UpperBody.mask'
$maskText = if (Test-Path -LiteralPath $maskPath) { Get-Content -LiteralPath $maskPath -Raw } else { '' }
$inlineSplit = $controllerText.Contains('layer "AK Upper" 1 override "spine_01" "ik_hand_gun" "ik_hand_l" "ik_hand_r" @meshspace')
$assetSplit = $controllerText.Contains('layer "AK Upper" 1 override @meshspace @mask "Assets/AK/Masks/UpperBody.mask"') -and
    $maskText.Contains('bone "spine_01"') -and $maskText.Contains('bone "ik_hand_gun"') -and
    $maskText.Contains('bone "ik_hand_l"') -and $maskText.Contains('bone "ik_hand_r"')
if (-not $controllerText.Contains('layer "Base Layer" 1 override') -or
    -not ($inlineSplit -or $assetSplit)) {
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
    @{ name = 'mouse-look'; keys = 'W,Mouse1'; mouse = '60,-30'; image = 'split-mouse-look.png'; external = 'split-mouse-look-external.png' }
)) {
    $capture = Join-Path $OutputDir $case.name
    New-Item -ItemType Directory -Force -Path $capture | Out-Null
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($case.ContainsKey('mouse')) {
            $log = & $engine --project $project --playtest 2 --capture-dir $capture --hold $case.keys --mouse $case.mouse 2>&1
        }
        else {
            $log = & $engine --project $project --playtest 2 --capture-dir $capture --hold $case.keys 2>&1
        }
        $exit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previous }
    $log | Out-File -LiteralPath (Join-Path $capture 'playtest.log') -Encoding utf8
    if ($exit -ne 0) { throw "$($case.name) playtest failed with exit $exit" }
    $lines = $log | ForEach-Object { $_.ToString() }
    if (-not ($lines | Select-String -SimpleMatch 'state Locomotion | AK Upper: AK Idle')) {
        throw "$($case.name): the locomotion and AK upper layers were not both active."
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
    if ($case.name -eq 'mouse-look') {
        $forwards = @($lines | Select-String -Pattern 'camera pos .* forward \(([-0-9.]+)' |
            ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) })
        if ($forwards.Count -eq 0 -or [Math]::Abs($forwards[-1]) -lt 0.2) {
            throw 'Mouse look binding did not turn the camera.'
        }
    }
    $source = Join-Path $capture 'play_03.bmp'
    if (-not (Test-Path -LiteralPath $source)) { throw "$($case.name): the visual capture is missing." }
    $bitmap = [System.Drawing.Image]::FromFile($source)
    try { $bitmap.Save((Join-Path $ReviewDir $case.image), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $externalSource = Join-Path $capture 'scene_play_03.bmp'
    if (-not (Test-Path -LiteralPath $externalSource)) { throw "$($case.name): the external view capture is missing." }
    $external = [System.Drawing.Image]::FromFile($externalSource)
    try { $external.Save((Join-Path $ReviewDir $case.external), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $external.Dispose() }
    Write-Output "$($case.name): both layers active; maximum hand error $(($errors | Measure-Object -Maximum).Maximum) m; screenshot $($case.image)"
}
