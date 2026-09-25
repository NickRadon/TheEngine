#pragma once

#include "render/VulkanContext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Unity's built-in primitives.
enum class PrimitiveType : int { None = 0, Cube, Sphere, Capsule, Cylinder, Plane, Quad, Count };

const char* PrimitiveName(PrimitiveType type);
bool PrimitiveFromName(const std::string& name, PrimitiveType& out);

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };
};

MeshData GeneratePrimitive(PrimitiveType type);

// GPU mesh; keeps the CPU data around for picking.
struct Mesh
{
    MeshData data;
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;
    uint32_t indexCount = 0;
};

// Ray/mesh intersection in mesh local space. Returns the closest hit distance or < 0.
float RaycastMesh(const MeshData& mesh, const glm::vec3& origin, const glm::vec3& dir);
