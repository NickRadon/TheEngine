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
    @{ name='semi-held'; args=@('--hold','Mouse0'); arms='Idle'; weapon='Any'; frame='00' },
    @{ name='auto-fire'; args=@('--hold','Mouse0,X'); arms='Idle'; weapon='Fire' },
    @{ name='aim-auto-fire'; args=@('--hold','Mouse0,Mouse1,X'); arms='Idle'; weapon='Fire' },
    @{ name='toggle-fire-mode'; args=@('--hold','Mouse0','--press','X@0.1,X@0.8'); arms='Idle'; weapon='Any'; seconds='2.2' },
    @{ name='aim'; args=@('--hold','Mouse1'); arms='Idle'; weapon='Idle' },
    @{ name='aim-walk'; args=@('--hold','Mouse1,W'); arms='Walk'; weapon='Idle' },
    @{ name='aim-sprint'; args=@('--hold','Mouse1,W,LeftShift'); arms='Walk'; weapon='Idle'; seconds='2.2'; frame='03' },
    @{ name='backward-sprint'; args=@('--hold','S,LeftShift'); arms='Walk'; weapon='Idle' },
    @{ name='jump'; args=@('--press','Space@0.25'); arms='Idle'; weapon='Idle'; seconds='2.5'; frame='03' },
    @{ name='wall-stop'; args=@('--hold','D'); arms='Idle'; weapon='Idle'; seconds='2.5'; frame='04' },
    @{ name='reload-priority'; args=@('--press','R@0.25,I@0.25,M@0.25,Mouse0@0.25'); arms='Reload'; weapon='Reload'; seconds='2.5'; frame='04' },
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
    $lines | Set-Content -LiteralPath (Join-Path $capture 'playtest.log')
    if ($LASTEXITCODE -ne 0 -or @($lines | Select-String -Pattern '^\[error\]').Count -gt 0) {
        throw "$($case.name): engine reported an error. $($lines -join [Environment]::NewLine)"
    }
    $arms = @($lines | Select-String -Pattern "\[playtest\].*AK Arms.*state $($case.arms)(?: |$)")
    $weaponFound = $case.weapon -eq 'Any' -or
        @($lines | Select-String -Pattern "\[playtest\].*AK Weapon.*state $($case.weapon)(?: |$)").Count -gt 0
    if ($arms.Count -eq 0 -or -not $weaponFound) {
        throw "$($case.name): expected arms=$($case.arms), weapon=$($case.weapon) not observed. $($lines -join [Environment]::NewLine)"
    }
    if ($case.name -in @('fire','semi-held','auto-fire','aim-auto-fire','toggle-fire-mode')) {
        $shots = @($lines | Select-String -Pattern '\[ak-fire\] shot ')
        $minimum = if ($case.name -eq 'toggle-fire-mode') { 2 } elseif ($case.name -like '*auto-fire') { 5 } else { 1 }
        $maximum = if ($case.name -eq 'toggle-fire-mode') { 10 } elseif ($case.name -like '*auto-fire') { 20 } else { 1 }
        if ($shots.Count -lt $minimum -or $shots.Count -gt $maximum) {
            throw "$($case.name): expected $minimum-$maximum shots, observed $($shots.Count)."
        }
        if ($case.name -like '*auto-fire' -and -not @($lines | Select-String -Pattern '\[ak-fire\] mode full-auto').Count) {
            throw "$($case.name): X did not enable full-auto."
        }
        if ($case.name -eq 'toggle-fire-mode' -and
            (-not @($lines | Select-String -Pattern '\[ak-fire\] mode full-auto').Count -or
             -not @($lines | Select-String -Pattern '\[ak-fire\] mode semi-auto').Count)) {
            throw 'X did not toggle full-auto back to semi-auto.'
        }
        Write-Output "$($case.name): $($shots.Count) firing-animation triggers"
    }
    if ($case.name -eq 'reload-priority' -and @($lines | Select-String -Pattern '\[ak-fire\] shot ').Count) {
        throw 'Reload must win over simultaneous fire, inspect and magazine-check input.'
    }
    if ($case.name -in @('jump', 'wall-stop')) {
        $positions = @($lines | ForEach-Object {
            if ($_ -match '\[playtest\] camera pos \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)') {
                [pscustomobject]@{ x=[double]$Matches[1]; y=[double]$Matches[2]; z=[double]$Matches[3] }
            }
        })
        if ($positions.Count -eq 0) { throw "$($case.name): camera positions missing." }
        if ($case.name -eq 'jump') {
            $peak = ($positions.y | Measure-Object -Maximum).Maximum
            if ($peak -lt 2.1 -or [Math]::Abs($positions[-1].y - 1.65) -gt 0.15) {
                throw "Jump did not rise and land: peak=$peak, final=$($positions[-1].y)."
            }
            Write-Output "jump: camera peak $peak m, landed at $($positions[-1].y) m"
        } elseif ($positions[-1].x -lt 0.8 -or $positions[-1].x -gt 1.3) {
            throw "Wall collision failed: final x=$($positions[-1].x)."
        } else { Write-Output "wall-stop: capsule stopped at x=$($positions[-1].x) m" }
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
