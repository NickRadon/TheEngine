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
entity 1 11 1 "Main Camera"
  transform $($offset -join ' ') 0.000000 -0.216440 0.000000 0.976296 1 1 1 0 -25 0
  camera 1 70 0.03 1000 0 5
"@ + "`n"
$cameraMatch = [regex]::Match($scene, '(?ms)^entity 1 \d+ 1 "Main Camera"\r?\n.*?(?=^entity |\z)')
if (-not $cameraMatch.Success) { throw 'AK scene has no Main Camera entity 1.' }
$scene = $scene.Remove($cameraMatch.Index, $cameraMatch.Length).Insert($cameraMatch.Index, $camera)

# The socket follows the animated head; the camera's child transform is the optional view offset.
if ($scene -match '(?m)^entity 11 ') {
    if ($scene -notmatch '(?m)^entity 11 4 1 "Head Camera Anchor"') {
        throw 'Entity 11 is already used by another object.'
    }
    $anchor = [regex]::Match($scene, '(?ms)^entity 11 4 1 "Head Camera Anchor"\r?\n.*?(?=^entity |\z)')
    if ($anchor.Value -notmatch '(?m)^  socket 1 "head"') { throw 'Head Camera Anchor has no head socket.' }
    $aligned = [regex]::Replace($anchor.Value, '(?m)^  socket 1 "head"[^\r\n]*',
        '  socket 1 "head" 0 0 0 -90 0 -90 1')
    $scene = $scene.Remove($anchor.Index, $anchor.Length).Insert($anchor.Index, $aligned)
}
else {
    $scene = $scene.TrimEnd("`r", "`n") + "`n" + @'
entity 11 4 1 "Head Camera Anchor"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  socket 1 "head" 0 0 0 -90 0 -90 1
'@
}

# A first-person camera must not see the inside of the character's head.
$headMesh = '(?m)^(  mesh 1 "Assets/Character/Mesh/Player_Body_Full.fbx#0"[^\r\n]*?"Assets/Character/Mesh/Player_Body_Full_Materials/M_Quantum_Head.mat" 1)(?: 1)?$'
if ($scene -notmatch $headMesh) { throw 'AK scene head mesh was not found.' }
$scene = [regex]::Replace($scene, $headMesh, '$1 1')
Set-Content -LiteralPath $scenePath -Value $scene -NoNewline
Write-Output "First-person AK camera attached to head in $scenePath"
