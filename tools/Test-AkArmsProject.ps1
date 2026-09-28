param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh',
    [string]$ReviewDir = 'docs/test-captures/animation-fresh-ak',
    [string]$EnginePath = 'build/Debug/TheEngine.exe',
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
$engine = if ([System.IO.Path]::IsPathRooted($EnginePath)) { $EnginePath } else { Join-Path $repo $EnginePath }
$output = Join-Path $repo 'build/test-captures/animation-fresh-ak'
New-Item -ItemType Directory -Force -Path $output,$ReviewDir | Out-Null
Add-Type -AssemblyName System.Drawing
$cases = @(
    @{ name='idle'; args=@(); arms='Idle'; weapon='Idle' },
    @{ name='walk'; args=@('--hold','W'); arms='Walk'; weapon='Idle' },
    @{ name='sprint'; args=@('--hold','W,LeftShift'); arms='Sprint'; weapon='Idle'; seconds='2.2'; frame='03' },
    @{ name='fire'; args=@('--press','Mouse0@0.25'); arms='Idle'; weapon='Fire' },
    @{ name='aim'; args=@('--hold','Mouse1'); arms='Idle'; weapon='Idle' },
    @{ name='aim-walk'; args=@('--hold','Mouse1,W'); arms='Walk'; weapon='Idle' },
    @{ name='aim-sprint'; args=@('--hold','Mouse1,W,LeftShift'); arms='Sprint'; weapon='Idle'; seconds='2.2'; frame='03' },
    @{ name='aim-fire'; args=@('--hold','Mouse1','--press','Mouse0@0.25'); arms='Idle'; weapon='Fire' },
    @{ name='aim-reload'; args=@('--hold','Mouse1','--press','R@0.25'); arms='Reload'; weapon='Reload'; seconds='2.5'; frame='04' },
    @{ name='aim-mag-check'; args=@('--hold','Mouse1','--press','M@0.25'); arms='MagCheck'; weapon='MagCheck'; seconds='2.5'; frame='04' },
    @{ name='aim-inspect'; args=@('--hold','Mouse1','--press','I@0.25'); arms='Inspect'; weapon='Inspect'; seconds='2.5'; frame='04' },
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
    if ($case.name -like 'aim*') {
        $weights = @($lines | ForEach-Object { if ($_ -match 'aim layer weight ([-\d.]+)') { [double]$Matches[1] } })
        if ($weights.Count -eq 0 -or ($weights | Measure-Object -Maximum).Maximum -lt 0.2) {
            throw "$($case.name): aimed animation damping layer did not reach weight 0.2."
        }
        Write-Output "$($case.name): aim layer max weight $([Math]::Round(($weights | Measure-Object -Maximum).Maximum, 3))"
    }
    if ($case.name -in @('reload','aim-reload')) {
        $magPositions = @($lines | ForEach-Object {
            if ($_ -match 'weapon mag2 local \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)') {
                [double[]]@([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
            }
        })
        $magMotion = 0.0
        for ($i = 0; $i + 5 -lt $magPositions.Count; $i += 3) {
            $distance = [Math]::Sqrt([Math]::Pow($magPositions[$i] - $magPositions[$i + 3], 2) +
                [Math]::Pow($magPositions[$i + 1] - $magPositions[$i + 4], 2) +
                [Math]::Pow($magPositions[$i + 2] - $magPositions[$i + 5], 2))
            $magMotion = [Math]::Max($magMotion, $distance)
        }
        if ($magMotion -lt 0.1) { throw "$($case.name): weapon magazine pose did not move (maximum sampled delta $magMotion m)." }
        Write-Output "$($case.name): weapon magazine pose delta $([Math]::Round($magMotion, 3)) m"
    }
    $frame = if ($case.ContainsKey('frame')) { $case.frame } else { '01' }
    $source = Join-Path $capture "play_$frame.bmp"
    if (-not (Test-Path -LiteralPath $source)) { throw "$($case.name): screenshot is missing." }
    $image = [System.Drawing.Image]::FromFile($source)
    try { $image.Save((Join-Path $ReviewDir "$($case.name).png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
    $externalSource = Join-Path $capture "scene_play_$frame.bmp"
    if (-not (Test-Path -LiteralPath $externalSource)) { throw "$($case.name): external screenshot is missing." }
    $external = [System.Drawing.Image]::FromFile($externalSource)
    try { $external.Save((Join-Path $ReviewDir "$($case.name)-external.png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $external.Dispose() }
    Write-Output "$($case.name): arms=$($case.arms), weapon=$($case.weapon), capture=$($case.name).png"
}
