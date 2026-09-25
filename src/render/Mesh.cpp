#include "render/Mesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

const char* PrimitiveName(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::Cube: return "Cube";
    case PrimitiveType::Sphere: return "Sphere";
    case PrimitiveType::Capsule: return "Capsule";
    case PrimitiveType::Cylinder: return "Cylinder";
    case PrimitiveType::Plane: return "Plane";
    case PrimitiveType::Quad: return "Quad";
    default: return "None";
    }
}

bool PrimitiveFromName(const std::string& name, PrimitiveType& out)
{
    for (int i = 1; i < static_cast<int>(PrimitiveType::Count); ++i)
        if (name == PrimitiveName(static_cast<PrimitiveType>(i)))
        {
            out = static_cast<PrimitiveType>(i);
            return true;
        }
    return false;
}

namespace
{
    void AddQuad(MeshData& m, glm::vec3 center, glm::vec3 right, glm::vec3 up, glm::vec3 normal)
    {
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({ center - right - up, normal, { 0, 1 } });
        m.vertices.push_back({ center + right - up, normal, { 1, 1 } });
        m.vertices.push_back({ center + right + up, normal, { 1, 0 } });
        m.vertices.push_back({ center - right + up, normal, { 0, 0 } });
        m.indices.insert(m.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }

    MeshData Cube()
    {
        MeshData m;
        const float h = 0.5f;
        AddQuad(m, { 0, 0, h }, { h, 0, 0 }, { 0, h, 0 }, { 0, 0, 1 });
        AddQuad(m, { 0, 0, -h }, { -h, 0, 0 }, { 0, h, 0 }, { 0, 0, -1 });
        AddQuad(m, { h, 0, 0 }, { 0, 0, -h }, { 0, h, 0 }, { 1, 0, 0 });
        AddQuad(m, { -h, 0, 0 }, { 0, 0, h }, { 0, h, 0 }, { -1, 0, 0 });
        AddQuad(m, { 0, h, 0 }, { h, 0, 0 }, { 0, 0, -h }, { 0, 1, 0 });
        AddQuad(m, { 0, -h, 0 }, { h, 0, 0 }, { 0, 0, h }, { 0, -1, 0 });
        return m;
    }

    // UV sphere with optional cylindrical middle section (capsule when cylinderHeight > 0).
    MeshData Capsule(float radius, float cylinderHeight, int segments, int rings)
    {
        MeshData m;
        const float halfCyl = cylinderHeight * 0.5f;
        const int ringsTotal = rings + (cylinderHeight > 0.0f ? 1 : 0);
        for (int r = 0; r <= ringsTotal; ++r)
        {
            int rr = r;
            float offset = halfCyl;
            if (cylinderHeight > 0.0f && r > rings / 2) { rr = r - 1; offset = -halfCyl; }
            float phi = glm::pi<float>() * static_cast<float>(rr) / static_cast<float>(rings);
            float y = std::cos(phi);
            float s = std::sin(phi);
            for (int seg = 0; seg <= segments; ++seg)
            {
                float theta = glm::two_pi<float>() * static_cast<float>(seg) / static_cast<float>(segments);
                glm::vec3 n(s * std::cos(theta), y, s * std::sin(theta));
                glm::vec3 p = n * radius + glm::vec3(0, offset, 0);
                m.vertices.push_back({ p, n, { static_cast<float>(seg) / segments, static_cast<float>(r) / ringsTotal } });
            }
        }
        const uint32_t stride = static_cast<uint32_t>(segments + 1);
        for (int r = 0; r < ringsTotal; ++r)
            for (int seg = 0; seg < segments; ++seg)
            {
                uint32_t a = r * stride + seg, b = a + stride;
                m.indices.insert(m.indices.end(), { a, a + 1, b, b, a + 1, b + 1 });
            }
        return m;
    }

    MeshData Cylinder(int segments)
    {
        MeshData m;
        const float r = 0.5f, h = 1.0f; // Unity's cylinder is 2 units tall
        for (int side = 0; side < 2; ++side)
        {
            float y = side == 0 ? -h : h;
            for (int s = 0; s <= segments; ++s)
            {
                float t = glm::two_pi<float>() * static_cast<float>(s) / segments;
                glm::vec3 n(std::cos(t), 0, std::sin(t));
                m.vertices.push_back({ glm::vec3(n.x * r, y, n.z * r), n, { static_cast<float>(s) / segments, static_cast<float>(side) } });
            }
        }
        const uint32_t stride = segments + 1;
        for (int s = 0; s < segments; ++s)
        {
            uint32_t a = s, b = s + stride;
            m.indices.insert(m.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
        for (int cap = 0; cap < 2; ++cap)
        {
            float y = cap == 0 ? -h : h;
            glm::vec3 n(0, cap == 0 ? -1.0f : 1.0f, 0);
            uint32_t center = static_cast<uint32_t>(m.vertices.size());
            m.vertices.push_back({ { 0, y, 0 }, n, { 0.5f, 0.5f } });
            for (int s = 0; s <= segments; ++s)
            {
                float t = glm::two_pi<float>() * static_cast<float>(s) / segments;
                m.vertices.push_back({ { std::cos(t) * r, y, std::sin(t) * r }, n, { std::cos(t) * 0.5f + 0.5f, std::sin(t) * 0.5f + 0.5f } });
            }
            for (int s = 0; s < segments; ++s)
            {
                uint32_t a = center + 1 + s;
                if (cap == 0) m.indices.insert(m.indices.end(), { center, a, a + 1 });
                else m.indices.insert(m.indices.end(), { center, a + 1, a });
            }
        }
        return m;
    }

    MeshData Plane(float size, int divisions)
    {
        MeshData m;
        for (int z = 0; z <= divisions; ++z)
            for (int x = 0; x <= divisions; ++x)
            {
                float u = static_cast<float>(x) / divisions, v = static_cast<float>(z) / divisions;
                m.vertices.push_back({ { (u - 0.5f) * size, 0, (v - 0.5f) * size }, { 0, 1, 0 }, { u, v } });
            }
        const uint32_t stride = divisions + 1;
        for (int z = 0; z < divisions; ++z)
            for (int x = 0; x < divisions; ++x)
            {
                uint32_t a = z * stride + x, b = a + stride;
                m.indices.insert(m.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
            }
        return m;
    }

    MeshData Quad()
    {
        MeshData m;
        AddQuad(m, { 0, 0, 0 }, { 0.5f, 0, 0 }, { 0, 0.5f, 0 }, { 0, 0, 1 });
        return m;
    }
}

MeshData GeneratePrimitive(PrimitiveType type)
{
    MeshData m;
    switch (type)
    {
    case PrimitiveType::Cube: m = Cube(); break;
    case PrimitiveType::Sphere: m = Capsule(0.5f, 0.0f, 48, 32); break;
    case PrimitiveType::Capsule: m = Capsule(0.5f, 1.0f, 48, 32); break;
    case PrimitiveType::Cylinder: m = Cylinder(48); break;
    case PrimitiveType::Plane: m = Plane(10.0f, 10); break;
    case PrimitiveType::Quad: m = Quad(); break;
    default: break;
    }
    if (!m.vertices.empty())
    {
        m.boundsMin = m.boundsMax = m.vertices[0].position;
        for (const auto& v : m.vertices)
        {
            m.boundsMin = glm::min(m.boundsMin, v.position);
            m.boundsMax = glm::max(m.boundsMax, v.position);
        }
    }
    return m;
}

float RaycastMesh(const MeshData& mesh, const glm::vec3& origin, const glm::vec3& dir)
{
    // Slab test against local bounds first.
    glm::vec3 inv = 1.0f / dir;
    glm::vec3 t0 = (mesh.boundsMin - glm::vec3(1e-4f) - origin) * inv;
    glm::vec3 t1 = (mesh.boundsMax + glm::vec3(1e-4f) - origin) * inv;
    glm::vec3 tmin = glm::min(t0, t1), tmax = glm::max(t0, t1);
    float enter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
    float exit = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
    if (exit < 0.0f || enter > exit) return -1.0f;

    // Moller-Trumbore, two-sided.
    float best = -1.0f;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        glm::vec3 e1 = b - a, e2 = c - a;
        glm::vec3 p = glm::cross(dir, e2);
        float det = glm::dot(e1, p);
        if (std::fabs(det) < 1e-9f) continue;
        float invDet = 1.0f / det;
        glm::vec3 s = origin - a;
        float u = glm::dot(s, p) * invDet;
        if (u < 0.0f || u > 1.0f) continue;
        glm::vec3 q = glm::cross(s, e1);
        float v = glm::dot(dir, q) * invDet;
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = glm::dot(e2, q) * invDet;
        if (t > 0.0f && (best < 0.0f || t < best)) best = t;
    }
    return best;
}
