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
$controllerText = Get-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Aim.controller') -Raw
$maskText = Get-Content -LiteralPath (Join-Path $project 'Assets/AK/Masks/UpperBody.mask') -Raw
if (-not $controllerText.Contains('layer "Base Layer" 1 override') -or
    -not $controllerText.Contains('layer "AK Upper" 1 override @mask "Assets/AK/Masks/UpperBody.mask"') -or
    -not $maskText.Contains('bone "spine_01"')) {
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
    @{ name = 'locomotion'; keys = 'W'; image = 'split-locomotion.png' },
    @{ name = 'aim-locomotion'; keys = 'W,I,J'; image = 'split-aim-locomotion.png' }
)) {
    $capture = Join-Path $OutputDir $case.name
    New-Item -ItemType Directory -Force -Path $capture | Out-Null
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $log = & $engine --project $project --playtest 2 --capture-dir $capture --hold $case.keys 2>&1
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
    $source = Join-Path $capture 'play_03.bmp'
    if (-not (Test-Path -LiteralPath $source)) { throw "$($case.name): the visual capture is missing." }
    $bitmap = [System.Drawing.Image]::FromFile($source)
    try { $bitmap.Save((Join-Path $ReviewDir $case.image), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    Write-Output "$($case.name): both layers active; maximum hand error $(($errors | Measure-Object -Maximum).Maximum) m; screenshot $($case.image)"
}
