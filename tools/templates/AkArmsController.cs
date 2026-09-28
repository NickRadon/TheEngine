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
    [Range(0f, 1f)] public float adsMovementScale = 1f;
    [Range(0f, 1f)] public float aimAnimationScale = 0.75f;

    InputActionAsset actions;
    Animator arms;
    Animator weapon;
    float pitch;
    float aimWeight;
    int aimLayer = -1;
    int weaponAimLayer = -1;
    Vector3 armsBasePosition;

    void Start()
    {
        arms = armsObject?.GetComponent<Animator>();
        weapon = weaponObject?.GetComponent<Animator>();
        if (armsObject != null) armsBasePosition = armsObject.transform.localPosition;
        if (aimPointObject == null) Debug.LogError("Assign the AK prefab's AimPoint to AkArmsController.");
        if (arms == null || weapon == null || cameraObject == null)
            Debug.LogError("Assign camera, arms, and weapon to AkArmsController.");
        try { actions = InputActionAsset.Load(inputActions); }
        catch (Exception e) { Debug.LogError("AK input actions: " + e.Message); }
        Cursor.lockState = CursorLockMode.Locked;
        Cursor.visible = false;
    }

    void Update()
    {
        if (actions == null) return;
        // Start each frame at the authored hip pose; LateUpdate aligns the animated sight.
        if (armsObject != null) armsObject.transform.localPosition = armsBasePosition;

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
        if (actions.WasPressedThisFrame("Player/Fire")) weapon?.SetTrigger("Fire");
        if (actions.WasPressedThisFrame("Player/MagCheck")) TriggerBoth("MagCheck");
        if (actions.WasPressedThisFrame("Player/Inspect")) TriggerBoth("Inspect");
        if (actions.WasPressedThisFrame("Player/Reload")) TriggerBoth("Reload");
    }

    void LateUpdate()
    {
        if (aimWeight <= 0f || armsObject == null || aimPointObject == null || cameraObject == null) return;
        // The AimPoint is part of the weapon prefab. After animation and socket placement,
        // translate the camera-mounted rig until that point reaches the camera.
        Vector3 delta = cameraObject.transform.position - aimPointObject.transform.position;
        armsObject.transform.position += delta * (aimWeight * Mathf.Clamp01(adsMovementScale));
    }

    void TriggerBoth(string name)
    {
        arms?.SetTrigger(name);
        weapon?.SetTrigger(name);
    }
}
