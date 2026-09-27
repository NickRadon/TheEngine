using System;
using TheEngine;

// Lower body uses locomotion; the AK first-person clip owns spine_01 and up.
// All keys and mouse bindings live in Assets/Input/AK_Player.inputactions.
public class AKAimController : MonoBehaviour
{
    public string inputActions = "Assets/Input/AK_Player.inputactions";
    public float turnSpeed = 120f;
    public float damping = 0.12f;
    public float keySpeed = 65f;
    public float maxPitch = 45f;
    public float maxYaw = 60f;

    Animator animator;
    InputActionAsset actions;
    Transform cameraTransform;
    float pitch;
    float yaw;

    void Start()
    {
        animator = GetComponent<Animator>();
        if (animator == null) Debug.LogError("AKAimController needs an Animator on the Player.");
        try { actions = InputActionAsset.Load(inputActions); }
        catch (Exception e) { Debug.LogError("Could not load AK input actions: " + e.Message); }
        GameObject cameraObject = GameObject.Find("Main Camera");
        if (cameraObject != null) cameraTransform = cameraObject.transform;
    }

    void Update()
    {
        if (animator == null || actions == null) return;
        Vector2 move = actions.ReadVector2("Player/Move");
        float x = move.x;
        float y = move.y;
        float length = Mathf.Sqrt(x * x + y * y);
        if (length > 1f) { x /= length; y /= length; length = 1f; }
        float gait = actions.IsPressed("Player/Walk") ? 1f : 2f;
        if (actions.IsPressed("Player/Sprint") && y > 0.5f && Mathf.Abs(x) < 0.5f) gait = 3f;
        animator.SetFloat("MoveX", x * gait, damping, Time.deltaTime);
        animator.SetFloat("MoveY", y * gait, damping, Time.deltaTime);
        animator.SetFloat("Speed", length * gait);
        float turn = actions.ReadFloat("Player/Turn");
        transform.Rotate(0f, -turn * turnSpeed * Time.deltaTime, 0f);

        Vector2 look = actions.ReadVector2("Player/Look");
        pitch += look.y;
        yaw += look.x;
        Vector2 lookKeys = actions.ReadVector2("Player/LookKeys");
        pitch += lookKeys.y * keySpeed * Time.deltaTime;
        yaw += lookKeys.x * keySpeed * Time.deltaTime;
        pitch = Mathf.Clamp(pitch, -maxPitch, maxPitch);
        yaw = Mathf.Clamp(yaw, -maxYaw, maxYaw);
        animator.SetLookAngles(pitch, yaw);
        if (cameraTransform != null) cameraTransform.localRotation = Quaternion.Euler(pitch, yaw, 0f);
    }
}
