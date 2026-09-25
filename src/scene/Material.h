#pragma once

#include <glm/glm.hpp>

#include <string>

// Material asset (.mat): Unity "Standard" shader style metallic/smoothness workflow.
struct MaterialAsset
{
    glm::vec3 albedo{ 1.0f };
    std::string albedoMap;   // texture paths relative to the project root; empty = none
    std::string normalMap;
    std::string maskMap;     // glTF style: G = roughness, B = metallic (multiplied with the factors)
    float metallic = 0.0f;
    float smoothness = 0.5f;
    float normalStrength = 1.0f;
    glm::vec2 tiling{ 1.0f };
    glm::vec3 emission{ 0.0f };

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};
