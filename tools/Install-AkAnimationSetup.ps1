param(
    [string]$ProjectRoot = 'C:\Users\nickr\Desktop\AnimationSetup'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$settings = Join-Path $project 'ProjectSettings/ProjectSettings.txt'
$sourceScene = Join-Path $project 'Assets/Scenes/AnimationSetup.scene'
if (-not (Test-Path -LiteralPath $settings) -or -not (Test-Path -LiteralPath $sourceScene)) {
    throw 'AnimationSetup is missing its project settings or source scene.'
}
foreach ($relative in @(
    'Assets/Character/Mesh/Player_Body_Full.fbx',
    'Assets/AK/Animations/Character/A_FP_AK_Idle.fbx',
    'Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $project $relative))) { throw "Missing $relative" }
}

$rigDir = Join-Path $project 'Assets/AK/Rigs'
New-Item -ItemType Directory -Force -Path $rigDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'tests/assets/AE_AK.rig') -Destination (Join-Path $rigDir 'AE_AK.rig') -Force

$controller = @'
TheEngineAnimator 1
state "AK Idle" clip "Assets/AK/Animations/Character/A_FP_AK_Idle.fbx" "" "" 1 1 320 140
default "AK Idle"
entry 40 140
any 40 300
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Animators/AK_Aim.controller') -Value $controller

$aimScript = @'
using TheEngine;

// AK pose preview: hold the right mouse button and move the mouse, or use I/J/K/L.
public class AKAimController : MonoBehaviour
{
    public float mouseSensitivity = 3f;
    public float keySpeed = 65f;
    public float maxPitch = 45f;
    public float maxYaw = 60f;

    Animator animator;
    float pitch;
    float yaw;

    void Start()
    {
        animator = GetComponent<Animator>();
        if (animator == null) Debug.LogError("AKAimController needs an Animator on the Player.");
    }

    void Update()
    {
        if (animator == null) return;
        if (Input.GetMouseButton(1))
        {
            pitch += Input.GetAxis("Mouse Y") * mouseSensitivity;
            yaw += Input.GetAxis("Mouse X") * mouseSensitivity;
        }
        if (Input.GetKey(KeyCode.I)) pitch += keySpeed * Time.deltaTime;
        if (Input.GetKey(KeyCode.K)) pitch -= keySpeed * Time.deltaTime;
        if (Input.GetKey(KeyCode.J)) yaw += keySpeed * Time.deltaTime;
        if (Input.GetKey(KeyCode.L)) yaw -= keySpeed * Time.deltaTime;
        pitch = Mathf.Clamp(pitch, -maxPitch, maxPitch);
        yaw = Mathf.Clamp(yaw, -maxYaw, maxYaw);
        animator.SetLookAngles(pitch, yaw);
    }
}
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Scripts/AKAimController.cs') -Value $aimScript

$scene = Get-Content -LiteralPath $sourceScene -Raw
$scene = $scene.Replace('name "AnimationSetup"', 'name "AK Aiming"')
$scene = $scene.Replace('animator 1 "Assets/Animators/Player.controller" 1',
    'animator 1 "Assets/Animators/AK_Aim.controller" 0 "spine_01,spine_02,spine_03,spine_04,spine_05,neck_01,head" 1 "Assets/AK/Rigs/AE_AK.rig" 1')
$scene = $scene.Replace('script 1 "PlayerController"', 'script 1 "AKAimController"')
$scene += @'
entity 9 4 1 "AK Weapon"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#0" 1 1 1 0 0.5 "" 1
  socket 1 "vb_ak_weapon" 0 0 0 0 0 0 1
entity 10 9 1 "AK Weapon Part 1"
  transform 0 0 0 0.000000 0.000000 0.000000 1.000000 1 1 1 0 0 0
  mesh 1 "Assets/AK/Animations/Weapon/A_W_AK_Idle.fbx#1" 1 1 1 0 0.5 "" 1
'@
Set-Content -LiteralPath (Join-Path $project 'Assets/Scenes/AK_Aiming.scene') -Value $scene -NoNewline

$settingsText = Get-Content -LiteralPath $settings -Raw
$settingsText = $settingsText.Replace('lastScene "Assets/Scenes/AnimationSetup.scene"', 'lastScene "Assets/Scenes/AK_Aiming.scene"')
Set-Content -LiteralPath $settings -Value $settingsText -NoNewline
Write-Output "Installed AK aiming scene in $project"
