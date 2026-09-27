using System;
using TheEngine;

// Attach to a player root with a body Animator. Assign an independent view-rig Animator
// and camera in the Inspector. Parent the view rig to the camera with an asset-specific
// offset so the arms and weapon rotate around the camera. The asset owns bindings;
// no character, weapon or key names are baked into the engine.
public class SplitFirstPersonController : MonoBehaviour
{
    public GameObject viewRig;
    public GameObject cameraObject;
    public string inputActions = "Assets/Input/Player.inputactions";
    public string actionMap = "Player";
    public string moveAction = "Move";
    public string walkAction = "Walk";
    public string sprintAction = "Sprint";
    public string turnAction = "Turn";
    public string lookAction = "Look";
    public string lookKeysAction = "LookKeys";
    public string moveXParameter = "MoveX";
    public string moveYParameter = "MoveY";
    public string speedParameter = "Speed";
    public float walkSpeed = 1f;
    public float jogSpeed = 2f;
    public float sprintSpeed = 3f;
    public float turnSpeed = 120f;
    public float lookKeySpeed = 65f;
    public float movementDamping = 0.12f;
    public float maxPitch = 89f;
    public float viewRigLowerOnLookDown = 0.35f;

    Animator bodyAnimator;
    Animator viewAnimator;
    InputActionAsset actions;
    Vector3 viewRigBasePosition;
    float pitch;

    string Action(string name) => actionMap + "/" + name;

    void Start()
    {
        bodyAnimator = GetComponent<Animator>();
        viewAnimator = viewRig?.GetComponent<Animator>();
        if (viewRig != null) viewRigBasePosition = viewRig.transform.localPosition;
        if (bodyAnimator == null) Debug.LogError("SplitFirstPersonController needs a body Animator.");
        if (viewAnimator == null) Debug.LogError("SplitFirstPersonController needs an assigned view-rig Animator.");
        try { actions = InputActionAsset.Load(inputActions); }
        catch (Exception e) { Debug.LogError("Could not load input actions: " + e.Message); }
        Cursor.lockState = CursorLockMode.Locked;
        Cursor.visible = false;
    }

    void Update()
    {
        if (actions == null) return;
        Vector2 move = actions.ReadVector2(Action(moveAction));
        float length = Mathf.Sqrt(move.x * move.x + move.y * move.y);
        float x = move.x, y = move.y;
        if (length > 1f) { x /= length; y /= length; length = 1f; }
        float gait = actions.IsPressed(Action(walkAction)) ? walkSpeed : jogSpeed;
        if (actions.IsPressed(Action(sprintAction)) && y > 0.5f && Mathf.Abs(x) < 0.5f) gait = sprintSpeed;
        if (bodyAnimator != null)
        {
            bodyAnimator.SetFloat(moveXParameter, x * gait, movementDamping, Time.deltaTime);
            bodyAnimator.SetFloat(moveYParameter, y * gait, movementDamping, Time.deltaTime);
            bodyAnimator.SetFloat(speedParameter, length * gait);
        }
        Vector2 look = actions.ReadVector2(Action(lookAction));
        Vector2 lookKeys = actions.ReadVector2(Action(lookKeysAction));
        pitch = Mathf.Clamp(pitch + look.y + lookKeys.y * lookKeySpeed * Time.deltaTime, -maxPitch, maxPitch);
        float turn = actions.ReadFloat(Action(turnAction)) * turnSpeed * Time.deltaTime;
        float yawDelta = look.x + lookKeys.x * lookKeySpeed * Time.deltaTime + turn;
        transform.Rotate(0f, -yawDelta, 0f);
        if (cameraObject != null) cameraObject.transform.localRotation = Quaternion.Euler(pitch, 0f, 0f);
        if (viewRig != null)
        {
            float down = Mathf.Clamp(-pitch / (maxPitch > 1f ? maxPitch : 1f), 0f, 1f);
            viewRig.transform.localPosition = new Vector3(viewRigBasePosition.x,
                viewRigBasePosition.y - viewRigLowerOnLookDown * down, viewRigBasePosition.z);
        }
    }
}
