param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationSetup',
    [double]$CameraOffsetX = 0,
    [double]$CameraOffsetY = 0,
    [double]$CameraOffsetZ = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scenePath = Join-Path $ProjectRoot 'Assets/Scenes/AK_Aiming.scene'
if (-not (Test-Path -LiteralPath $scenePath -PathType Leaf)) { throw "Missing AK scene: $scenePath" }
$scene = Get-Content -LiteralPath $scenePath -Raw
if ($scene -notmatch '(?m)^entity 4 0 1 "Player"') { throw 'AK scene has no Player entity 4.' }
$offset = @($CameraOffsetX, $CameraOffsetY, $CameraOffsetZ) |
    ForEach-Object { $_.ToString('0.######', [Globalization.CultureInfo]::InvariantCulture) }
$camera = @"
entity 1 4 1 "Main Camera"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  camera 1 90 0.03 1000 0 5
  socket 1 "head" $($offset -join ' ') 0 0 0 0
"@ + "`n"
$cameraMatch = [regex]::Match($scene, '(?ms)^entity 1 \d+ 1 "Main Camera"\r?\n.*?(?=^entity |\z)')
if (-not $cameraMatch.Success) { throw 'AK scene has no Main Camera entity 1.' }
$scene = $scene.Remove($cameraMatch.Index, $cameraMatch.Length).Insert($cameraMatch.Index, $camera)

# Migrate the old head-parented empty. The camera now copies the head location directly,
# while its rotation remains controlled by the Player aim script.
if ($scene -match '(?m)^entity 11 4 1 "Head Camera Anchor"') {
    $anchor = [regex]::Match($scene, '(?ms)^entity 11 4 1 "Head Camera Anchor"\r?\n.*?(?=^entity |\z)')
    $scene = $scene.Remove($anchor.Index, $anchor.Length)
}

# A first-person camera must not see the inside of the character's head.
$headMesh = '(?m)^(  mesh 1 "Assets/Character/Mesh/Player_Body_Full.fbx#0"[^\r\n]*?"Assets/Character/Mesh/Player_Body_Full_Materials/M_Quantum_Head.mat" 1)(?: 1)?$'
if ($scene -notmatch $headMesh) { throw 'AK scene head mesh was not found.' }
$scene = [regex]::Replace($scene, $headMesh, '$1 1')
Set-Content -LiteralPath $scenePath -Value $scene -NoNewline
Write-Output "First-person AK camera parented to Player and copying head position in $scenePath"
