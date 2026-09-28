using System;
using TheEngine;

// A camera-mounted arms rig. All bindings come from the input-action asset.
public class AkArmsController : MonoBehaviour
{
    public GameObject cameraObject;
    public GameObject armsObject;
    public GameObject weaponObject;
    public GameObject aimPointObject;
    public string inputActions = "Assets/Input/AK_Controls.inputactions";
    public float walkSpeed = 2.5f;
    public float sprintSpeed = 4.5f;
    public float mouseSensitivity = 1f;
    public bool invertHorizontal = false;
    public bool invertVertical = false;
    public float maxPitch = 89f;
    public float aimBlendSpeed = 10f;
    public bool lockCursorOnPlay = true;
    [Range(0f, 1f)] public float adsMovementScale = 1f;
    [Range(0f, 1f)] public float aimAnimationScale = 0.75f;
    public bool fullAuto = false;
    public float roundsPerMinute = 600f;
    public float recoilPitchPerShot = 1.2f;
    public float recoilKickPerShot = 0.012f;
    public float recoilRecoverySpeed = 14f;

    InputActionAsset actions;
    Animator arms;
    Animator weapon;
    AkGunPivot gunPivot;
    float pitch;
    float aimWeight;
    int aimLayer = -1;
    int weaponAimLayer = -1;
    Vector3 armsBasePosition;
    Quaternion armsBaseRotation;
    Vector3 adsOffsetInCameraSpace;
    bool hasAdsOffset;
    float nextShotTime;
    float recoilPitch;
    float recoilKick;
    int shotCount;

    void Start()
    {
        arms = armsObject?.GetComponent<Animator>();
        weapon = weaponObject?.GetComponent<Animator>();
        gunPivot = armsObject?.GetComponent<AkGunPivot>();
        if (armsObject != null)
        {
            armsBasePosition = armsObject.transform.localPosition;
            armsBaseRotation = armsObject.transform.localRotation;
        }
        if (aimPointObject == null) Debug.LogError("Assign the AK prefab's AimPoint to AkArmsController.");
        if (arms == null || weapon == null || cameraObject == null)
            Debug.LogError("Assign camera, arms, and weapon to AkArmsController.");
        try { actions = InputActionAsset.Load(inputActions); }
        catch (Exception e) { Debug.LogError("AK input actions: " + e.Message); }
        Cursor.lockState = lockCursorOnPlay ? CursorLockMode.Locked : CursorLockMode.None;
        Cursor.visible = !lockCursorOnPlay;
    }

    void Update()
    {
        if (actions == null) return;
        // Start each frame at the authored hip pose; LateUpdate aligns the animated sight.
        if (armsObject != null)
        {
            armsObject.transform.localPosition = armsBasePosition;
            armsObject.transform.localRotation = armsBaseRotation;
        }
        recoilPitch = Mathf.Max(0f, recoilPitch - recoilRecoverySpeed * Time.deltaTime);
        recoilKick = Mathf.Max(0f, recoilKick - recoilRecoverySpeed * 0.01f * Time.deltaTime);

        Vector2 move = actions.ReadVector2("Player/Move");
        float length = Mathf.Sqrt(move.x * move.x + move.y * move.y);
        if (length > 1f) { move.x /= length; move.y /= length; }
        bool moving = length > 0.01f;
        bool sprinting = moving && actions.IsPressed("Player/Sprint");
        float speed = sprinting ? sprintSpeed : walkSpeed;
        transform.Translate(move.x * speed * Time.deltaTime, 0f, -move.y * speed * Time.deltaTime);

        Vector2 look = actions.ReadVector2("Player/Look");
        float horizontal = look.x * mouseSensitivity * (invertHorizontal ? -1f : 1f);
        float vertical = look.y * mouseSensitivity * (invertVertical ? -1f : 1f);
        transform.Rotate(0f, -horizontal, 0f);
        pitch = Mathf.Clamp(pitch + vertical, -maxPitch, maxPitch);
        if (cameraObject != null) cameraObject.transform.localRotation = Quaternion.Euler(pitch, 0f, 0f);

        float targetAim = actions.IsPressed("Player/Aim") ? 1f : 0f;
        aimWeight += (targetAim - aimWeight) * Mathf.Clamp(Time.deltaTime * aimBlendSpeed, 0f, 1f);
        float idleOverlay = aimWeight * (1f - Mathf.Clamp01(aimAnimationScale));
        if (arms != null)
        {
            arms.SetBool("Moving", moving);
            arms.SetBool("Sprinting", sprinting);
            if (aimLayer < 0) aimLayer = arms.GetLayerIndex("Aim");
            if (aimLayer >= 0) arms.SetLayerWeight(aimLayer, idleOverlay);
        }
        if (weapon != null)
        {
            if (weaponAimLayer < 0) weaponAimLayer = weapon.GetLayerIndex("Aim");
            if (weaponAimLayer >= 0) weapon.SetLayerWeight(weaponAimLayer, idleOverlay);
        }
        if (actions.WasPressedThisFrame("Player/ToggleFireMode"))
        {
            fullAuto = !fullAuto;
            Debug.Log("[ak-fire] mode " + (fullAuto ? "full-auto" : "semi-auto"));
        }
        bool firePressed = actions.WasPressedThisFrame("Player/Fire");
        if ((firePressed || (fullAuto && actions.IsPressed("Player/Fire"))) &&
            Time.time >= nextShotTime && !ActionPlaying())
            FireOnce();
        if (actions.WasPressedThisFrame("Player/MagCheck")) TriggerBoth("MagCheck");
        if (actions.WasPressedThisFrame("Player/Inspect")) TriggerBoth("Inspect");
        if (actions.WasPressedThisFrame("Player/Reload")) TriggerBoth("Reload");
        gunPivot?.SetRecoil(recoilPitch, recoilKick);
    }

    void LateUpdate()
    {
        if (armsObject == null) return;
        // Calibrate from the authored idle sight. Keep this offset fixed while aiming so
        // weapon/reload animation does not make the whole rig orbit around the camera.
        if (aimPointObject != null && cameraObject != null)
        {
            if (!hasAdsOffset || (aimWeight < 0.01f && !ActionPlaying() && recoilPitch < 0.01f))
            {
                Vector3 delta = cameraObject.transform.position - aimPointObject.transform.position;
                adsOffsetInCameraSpace = Quaternion.Inverse(cameraObject.transform.rotation) * delta;
                hasAdsOffset = true;
            }
            armsObject.transform.position += cameraObject.transform.rotation * adsOffsetInCameraSpace *
                                             (aimWeight * Mathf.Clamp01(adsMovementScale));
        }
    }

    bool ActionPlaying()
    {
        if (weapon == null) return false;
        string state = weapon.currentStateName;
        return state == "Reload" || state == "ReloadEmpty" || state == "MagCheck" || state == "Inspect";
    }

    void FireOnce()
    {
        weapon?.SetTrigger("Fire");
        nextShotTime = Time.time + 60f / Mathf.Max(roundsPerMinute, 1f);
        recoilPitch = Mathf.Min(recoilPitch + Mathf.Max(recoilPitchPerShot, 0f), 8f);
        recoilKick = Mathf.Min(recoilKick + Mathf.Max(recoilKickPerShot, 0f), 0.06f);
        Debug.Log("[ak-fire] shot " + ++shotCount + " mode=" + (fullAuto ? "full-auto" : "semi-auto"));
    }

    void TriggerBoth(string name)
    {
        arms?.SetTrigger(name);
        weapon?.SetTrigger(name);
    }
}
