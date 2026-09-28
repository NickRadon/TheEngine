using TheEngine;

// Pose modifier on the arms Animator. All firing motion uses the authored
// ik_hand_gun pivot before the rig copies weapon and grip targets.
public class AkGunPivot : MonoBehaviour
{
    float pitch;
    float kick;

    public void SetRecoil(float pitchDegrees, float backwardMeters)
    {
        pitch = pitchDegrees;
        kick = backwardMeters;
    }

    void OnAnimatorPose(AnimationStream stream)
    {
        if (pitch <= 0f && kick <= 0f) return;
        if (!stream.TryGetModel("ik_hand_gun", out BonePose gun)) return;

        // Read every target before writing: the hand targets may be children of the gun.
        bool hasLeft = stream.TryGetModel("ik_hand_l", out BonePose left);
        bool hasRight = stream.TryGetModel("ik_hand_r", out BonePose right);
        Quaternion turn = gun.rotation * Quaternion.Euler(pitch, 0f, 0f) * Quaternion.Inverse(gun.rotation);
        Vector3 shift = gun.rotation * new Vector3(0f, 0f, kick);
        Vector3 pivot = gun.position;
        gun.position += shift;
        gun.rotation = turn * gun.rotation;
        stream.SetModel("ik_hand_gun", gun);
        if (hasLeft)
        {
            left.position = pivot + turn * (left.position - pivot) + shift;
            left.rotation = turn * left.rotation;
            stream.SetModel("ik_hand_l", left);
        }
        if (hasRight)
        {
            right.position = pivot + turn * (right.position - pivot) + shift;
            right.rotation = turn * right.rotation;
            stream.SetModel("ik_hand_r", right);
        }
    }
}
