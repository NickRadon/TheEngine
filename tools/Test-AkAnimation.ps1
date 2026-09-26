param(
    [string]$AeAssets,
    [string]$OutputDir = 'build/test-captures/ak',
    [string]$ReviewDir = 'docs/test-captures/ak',
    [switch]$SkipBuild,
    [switch]$ProcessOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $AeAssets) { $AeAssets = $env:THEENGINE_AE_ASSETS }
if (-not $AeAssets) {
    $AeAssets = Join-Path $env:USERPROFILE 'Documents/Unity Projects/AE Master/Assets/AE'
}
$AeAssets = (Resolve-Path -LiteralPath $AeAssets).Path

$required = @(
    'Meshes/Character/Quantum_Body_Full.fbx',
    'Weapons/AK/Animations/Character/A_FP_AK_Idle.fbx',
    'Weapons/AK/Animations/Weapon/A_W_AK_Idle.fbx'
)
foreach ($relative in $required) {
    $asset = Join-Path $AeAssets $relative
    if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) { throw "Missing AE test asset: $asset" }
}

if (-not [System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path $repo $OutputDir }
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

if (-not $ProcessOnly) {
    foreach ($name in @('ae_ak_neutral.bmp', 'ae_ak_neutral.png', 'ae_ak_aim.bmp', 'ae_ak_aim.png',
                       'ae_ak_comparison.png', 'ae_ak_closeup.png', 'ae_ak_metrics.csv', 'selftest.log', 'build.log', 'summary.json')) {
        $path = Join-Path $OutputDir $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}

if (-not $ProcessOnly) {
    Push-Location $repo
    try {
    if (-not $SkipBuild) {
        $buildOutput = & cmake --build build --config Debug 2>&1
        $buildExit = $LASTEXITCODE
        $buildOutput | Out-File -LiteralPath (Join-Path $OutputDir 'build.log') -Encoding utf8
        if ($buildExit -ne 0) { throw "Build failed ($buildExit). See $(Join-Path $OutputDir 'build.log')" }
    }

    $engine = Join-Path $repo 'build/Debug/TheEngine.exe'
    if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) { throw "Engine executable missing: $engine" }
    $previousAeAssets = $env:THEENGINE_AE_ASSETS
    $previousErrorAction = $ErrorActionPreference
    try {
        $env:THEENGINE_AE_ASSETS = $AeAssets
        # In Windows PowerShell 5, native stderr becomes an ErrorRecord; collect it without
        # aborting before we can read the engine's final test result.
        $ErrorActionPreference = 'Continue'
        $testOutput = & $engine --selftest-animation --capture-dir $OutputDir 2>&1
        $testExit = $LASTEXITCODE
        $testOutput | Out-File -LiteralPath (Join-Path $OutputDir 'selftest.log') -Encoding utf8
    }
    finally {
        $env:THEENGINE_AE_ASSETS = $previousAeAssets
        $ErrorActionPreference = $previousErrorAction
    }
    }
    finally { Pop-Location }
}

Add-Type -AssemblyName System.Drawing
foreach ($bmp in Get-ChildItem -LiteralPath $OutputDir -Filter '*.bmp' -File) {
    $png = [System.IO.Path]::ChangeExtension($bmp.FullName, '.png')
    $image = [System.Drawing.Image]::FromFile($bmp.FullName)
    try { $image.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
}

$neutral = Join-Path $OutputDir 'ae_ak_neutral.png'
$aim = Join-Path $OutputDir 'ae_ak_aim.png'
$metricsPath = Join-Path $OutputDir 'ae_ak_metrics.csv'
$missing = @(@($neutral, $aim, $metricsPath) | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count -gt 0) { throw "AK captures are missing: $($missing -join ', '). See selftest.log" }

$left = [System.Drawing.Image]::FromFile($neutral)
$right = [System.Drawing.Image]::FromFile($aim)
try {
    $height = [Math]::Max($left.Height, $right.Height)
    $sheet = [System.Drawing.Bitmap]::new($left.Width + $right.Width, $height + 44)
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $font = [System.Drawing.Font]::new('Segoe UI', 14)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(32, 34, 40))
        $graphics.DrawString('Neutral (0 pitch, 0 yaw)', $font, [System.Drawing.Brushes]::White, 12, 9)
        $graphics.DrawString('Aim (20 pitch, 15 yaw)', $font, [System.Drawing.Brushes]::White, $left.Width + 12, 9)
        $graphics.DrawImage($left, 0, 44)
        $graphics.DrawImage($right, $left.Width, 44)
        $sheet.Save((Join-Path $OutputDir 'ae_ak_comparison.png'), [System.Drawing.Imaging.ImageFormat]::Png)

        # The fixed test camera puts the actor near the middle of each frame. Save a tighter
        # view as well, so hands, weapon parts and their alignment can be inspected remotely.
        $cropWidth = [int]($left.Width * 0.42)
        $cropHeight = [int]($height * 0.75)
        $cropTop = [int]($height * 0.04)
        $closeup = [System.Drawing.Bitmap]::new($cropWidth * 2, $cropHeight + 44)
        $closeupGraphics = [System.Drawing.Graphics]::FromImage($closeup)
        try {
            $closeupGraphics.Clear([System.Drawing.Color]::FromArgb(32, 34, 40))
            $closeupGraphics.DrawString('Neutral (0 pitch, 0 yaw)', $font, [System.Drawing.Brushes]::White, 12, 9)
            $closeupGraphics.DrawString('Aim (20 pitch, 15 yaw)', $font, [System.Drawing.Brushes]::White, $cropWidth + 12, 9)
            $leftSource = [System.Drawing.Rectangle]::new([int](($left.Width - $cropWidth) / 2), $cropTop, $cropWidth, $cropHeight)
            $rightSource = [System.Drawing.Rectangle]::new([int](($right.Width - $cropWidth) / 2), $cropTop, $cropWidth, $cropHeight)
            $closeupGraphics.DrawImage($left, [System.Drawing.Rectangle]::new(0, 44, $cropWidth, $cropHeight),
                                       $leftSource, [System.Drawing.GraphicsUnit]::Pixel)
            $closeupGraphics.DrawImage($right, [System.Drawing.Rectangle]::new($cropWidth, 44, $cropWidth, $cropHeight),
                                       $rightSource, [System.Drawing.GraphicsUnit]::Pixel)
            $closeup.Save((Join-Path $OutputDir 'ae_ak_closeup.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally { $closeupGraphics.Dispose(); $closeup.Dispose() }
    }
    finally { $font.Dispose(); $graphics.Dispose(); $sheet.Dispose() }
}
finally { $left.Dispose(); $right.Dispose() }

$log = Get-Content -LiteralPath (Join-Path $OutputDir 'selftest.log')
$last = $log | Where-Object { $_ -match '\[selftest\] done: (\d+) passed, (\d+) failed' } | Select-Object -Last 1
if ($last -match '\[selftest\] done: (\d+) passed, (\d+) failed') {
    $passed = [int]$Matches[1]
    $failed = [int]$Matches[2]
}
else { throw 'Self-test summary missing from selftest.log' }
if ($ProcessOnly) { $testExit = if ($failed -eq 0) { 0 } else { 1 } }
$commit = (& git -C $repo rev-parse HEAD).Trim()
$metrics = @(Import-Csv -LiteralPath $metricsPath)
$summary = [ordered]@{
    commit = $commit
    runAt = (Get-Date).ToString('o')
    aeAssets = $AeAssets
    passed = $passed
    failed = $failed
    exitCode = $testExit
    metrics = $metrics
    comparison = (Join-Path $OutputDir 'ae_ak_comparison.png')
    closeup = (Join-Path $OutputDir 'ae_ak_closeup.png')
    log = (Join-Path $OutputDir 'selftest.log')
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDir 'summary.json') -Encoding utf8

if (-not [System.IO.Path]::IsPathRooted($ReviewDir)) { $ReviewDir = Join-Path $repo $ReviewDir }
$ReviewDir = [System.IO.Path]::GetFullPath($ReviewDir)
New-Item -ItemType Directory -Force -Path $ReviewDir | Out-Null
Copy-Item -LiteralPath (Join-Path $OutputDir 'ae_ak_comparison.png') -Destination (Join-Path $ReviewDir 'latest-comparison.png') -Force
Copy-Item -LiteralPath (Join-Path $OutputDir 'ae_ak_closeup.png') -Destination (Join-Path $ReviewDir 'latest-closeup.png') -Force

Write-Host "Animation test: $passed passed, $failed failed (exit $testExit)"
Write-Host "AK comparison: $(Join-Path $OutputDir 'ae_ak_comparison.png')"
Write-Host "AK close-up: $(Join-Path $OutputDir 'ae_ak_closeup.png')"
Write-Host "Grip metrics: $metricsPath"
Write-Host "Full log: $(Join-Path $OutputDir 'selftest.log')"
Write-Host "Remote-review images to commit: $ReviewDir"
if ($testExit -ne 0 -or $failed -ne 0) { exit 1 }
