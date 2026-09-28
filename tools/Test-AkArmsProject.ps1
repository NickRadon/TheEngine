param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh',
    [string]$ReviewDir = 'docs/test-captures/animation-fresh-ak',
    [string[]]$OnlyCases = @(),
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Arms.scene'))) { throw 'AK arms scene is missing.' }
if (-not [System.IO.Path]::IsPathRooted($ReviewDir)) { $ReviewDir = Join-Path $repo $ReviewDir }
if (-not $SkipBuild) {
    & cmake --build (Join-Path $repo 'build') --config Debug --target TheEngine
    if ($LASTEXITCODE -ne 0) { throw 'TheEngine build failed.' }
}
$engine = Join-Path $repo 'build/Debug/TheEngine.exe'
$output = Join-Path $repo 'build/test-captures/animation-fresh-ak'
New-Item -ItemType Directory -Force -Path $output,$ReviewDir | Out-Null
Add-Type -AssemblyName System.Drawing
$cases = @(
    @{ name='idle'; args=@(); arms='Idle'; weapon='Idle' },
    @{ name='walk'; args=@('--hold','W'); arms='Walk'; weapon='Idle' },
    @{ name='sprint'; args=@('--hold','W,LeftShift'); arms='Sprint'; weapon='Idle'; seconds='2.2'; frame='03' },
    @{ name='fire'; args=@('--press','Mouse0@0.25'); arms='Idle'; weapon='Fire' },
    @{ name='mag-check'; args=@('--press','M@0.25'); arms='MagCheck'; weapon='MagCheck'; seconds='2.5'; frame='04' },
    @{ name='inspect'; args=@('--press','I@0.25'); arms='Inspect'; weapon='Inspect'; seconds='2.5'; frame='04' },
    @{ name='reload'; args=@('--press','R@0.25'); arms='Reload'; weapon='Reload'; seconds='2.5'; frame='04' }
)
foreach ($case in $cases) {
    if ($OnlyCases.Count -gt 0 -and $case.name -notin $OnlyCases) { continue }
    $capture = Join-Path $output $case.name
    New-Item -ItemType Directory -Force -Path $capture | Out-Null
    $seconds = if ($case.ContainsKey('seconds')) { $case.seconds } else { '1.2' }
    $arguments = @('--project', $project, '--playtest', $seconds, '--capture-dir', $capture) + @($case.args)
    $lines = @(& $engine @arguments 2>&1)
    if ($LASTEXITCODE -ne 0 -or @($lines | Select-String -Pattern '^\[error\]').Count -gt 0) {
        throw "$($case.name): engine reported an error. $($lines -join [Environment]::NewLine)"
    }
    $arms = @($lines | Select-String -Pattern "\[playtest\].*AK Arms.*state $($case.arms)(?: |$)")
    $weapon = @($lines | Select-String -Pattern "\[playtest\].*AK Weapon.*state $($case.weapon)(?: |$)")
    if ($arms.Count -eq 0 -or $weapon.Count -eq 0) {
        throw "$($case.name): expected arms=$($case.arms), weapon=$($case.weapon) not observed. $($lines -join [Environment]::NewLine)"
    }
    $frame = if ($case.ContainsKey('frame')) { $case.frame } else { '01' }
    $source = Join-Path $capture "play_$frame.bmp"
    if (-not (Test-Path -LiteralPath $source)) { throw "$($case.name): screenshot is missing." }
    $image = [System.Drawing.Image]::FromFile($source)
    try { $image.Save((Join-Path $ReviewDir "$($case.name).png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
    Write-Output "$($case.name): arms=$($case.arms), weapon=$($case.weapon), capture=$($case.name).png"
}
