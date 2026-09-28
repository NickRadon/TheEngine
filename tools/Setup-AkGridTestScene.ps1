param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationFresh'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$source = Join-Path $project 'Assets/Scenes/AK_Arms.scene'
if (-not (Test-Path -LiteralPath $source)) { throw "AK arms scene is missing: $source" }

$materialDir = Join-Path $project 'Assets/Materials'
$textureDir = Join-Path $project 'Assets/Textures'
New-Item -ItemType Directory -Force -Path $materialDir,$textureDir | Out-Null

# A neutral UV grid with stronger lines every quarter tile.
Add-Type -AssemblyName System.Drawing
$texture = Join-Path $textureDir 'Grid.png'
$bitmap = [System.Drawing.Bitmap]::new(256,256)
$background = [System.Drawing.Color]::FromArgb(224,229,232)
$minor = [System.Drawing.Color]::FromArgb(123,135,143)
$major = [System.Drawing.Color]::FromArgb(51,69,80)
try {
    for ($y = 0; $y -lt 256; $y++) {
        for ($x = 0; $x -lt 256; $x++) {
            $minorLine = ($x % 32 -lt 2) -or ($y % 32 -lt 2)
            $majorLine = ($x % 128 -lt 4) -or ($y % 128 -lt 4)
            $bitmap.SetPixel($x,$y, $(if ($majorLine) { $major } elseif ($minorLine) { $minor } else { $background }))
        }
    }
    $bitmap.Save($texture, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally { $bitmap.Dispose() }

function Write-GridMaterial([string]$path, [int]$tiles) {
    @"
TheEngineMaterial 1
shader "Standard"
albedo 1 1 1
albedoMap "Assets/Textures/Grid.png"
normalMap ""
maskMap ""
metallic 0
smoothness 0.25
normalStrength 1
tiling $tiles $tiles
emission 0 0 0
"@ | Set-Content -LiteralPath $path
}
Write-GridMaterial (Join-Path $materialDir 'Grid.mat') 1
Write-GridMaterial (Join-Path $materialDir 'GridFloor.mat') 12

$scene = Get-Content -LiteralPath $source -Raw
$scene = $scene.Replace('name "AK_Arms"', 'name "AK_Grid_Test"')
$scene += @'
entity 7 0 1 "Grid Floor"
  transform 0 -0.05 0 0 0 0 1 20 1 20 0 0 0
  mesh 1 "Plane" 1 1 1 0 0.5 "Assets/Materials/GridFloor.mat" 1
entity 8 0 1 "Grid Cube Left"
  transform -2 0.5 -8 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Cube" 1 1 1 0 0.5 "Assets/Materials/Grid.mat" 1
entity 9 0 1 "Grid Cube Right"
  transform 2 0.5 -8 0 0 0 1 1 1 1 0 0 0
  mesh 1 "Cube" 1 1 1 0 0.5 "Assets/Materials/Grid.mat" 1
'@
$destination = Join-Path $project 'Assets/Scenes/AK_Grid_Test.scene'
Set-Content -LiteralPath $destination -Value $scene

$settings = Join-Path $project 'ProjectSettings/ProjectSettings.txt'
$text = Get-Content -LiteralPath $settings -Raw
$text = [regex]::Replace($text, '(?m)^lastScene ".*"', 'lastScene "Assets/Scenes/AK_Grid_Test.scene"')
Set-Content -LiteralPath $settings -Value $text -NoNewline
Write-Output "Created $destination with a grid environment and the original arms and weapon materials."
