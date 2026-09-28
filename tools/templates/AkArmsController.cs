using System;
using TheEngine;

// A camera-mounted arms rig. All bindings come from the input-action asset.
public class AkArmsController : MonoBehaviour
{
    public GameObject cameraObject;
    public GameObject armsObject;
    public GameObject weaponObject;
    public string inputActions = "Assets/Input/AK_Controls.inputactions";
    public float walkSpeed = 2.5f;
    public float sprintSpeed = 4.5f;
    public float mouseSensitivity = 1f;
    public bool invertHorizontal = false;
    public bool invertVertical = false;
    public float maxPitch = 89f;

    InputActionAsset actions;
    Animator arms;
    Animator weapon;
    float pitch;

    void Start()
    {
        arms = armsObject?.GetComponent<Animator>();
        weapon = weaponObject?.GetComponent<Animator>();
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

        if (arms != null)
        {
            arms.SetBool("Moving", moving);
            arms.SetBool("Sprinting", sprinting);
        }
        if (actions.WasPressedThisFrame("Player/Fire")) weapon?.SetTrigger("Fire");
        if (actions.WasPressedThisFrame("Player/MagCheck")) TriggerBoth("MagCheck");
        if (actions.WasPressedThisFrame("Player/Inspect")) TriggerBoth("Inspect");
        if (actions.WasPressedThisFrame("Player/Reload")) TriggerBoth("Reload");
    }

    void TriggerBoth(string name)
    {
        arms?.SetTrigger(name);
        weapon?.SetTrigger(name);
    }
}
