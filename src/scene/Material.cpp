#include "scene/Material.h"

#include <fstream>
#include <iomanip>
#include <sstream>

bool MaterialAsset::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file) return false;
    std::string header;
    int version = 0;
    file >> header >> version;
    if (header != "TheEngineMaterial") return false;

    MaterialAsset m;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        std::istringstream in(line);
        std::string key;
        if (!(in >> key)) continue;
        if (key == "albedo") in >> m.albedo.x >> m.albedo.y >> m.albedo.z;
        else if (key == "albedoMap") in >> std::quoted(m.albedoMap);
        else if (key == "normalMap") in >> std::quoted(m.normalMap);
        else if (key == "maskMap") in >> std::quoted(m.maskMap);
        else if (key == "metallic") in >> m.metallic;
        else if (key == "smoothness") in >> m.smoothness;
        else if (key == "normalStrength") in >> m.normalStrength;
        else if (key == "tiling") in >> m.tiling.x >> m.tiling.y;
        else if (key == "emission") in >> m.emission.x >> m.emission.y >> m.emission.z;
    }
    *this = m;
    return true;
}

bool MaterialAsset::Save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    out << "TheEngineMaterial 1\n";
    out << "shader \"Standard\"\n";
    out << "albedo " << albedo.x << ' ' << albedo.y << ' ' << albedo.z << "\n";
    out << "albedoMap " << std::quoted(albedoMap) << "\n";
    out << "normalMap " << std::quoted(normalMap) << "\n";
    out << "maskMap " << std::quoted(maskMap) << "\n";
    out << "metallic " << metallic << "\n";
    out << "smoothness " << smoothness << "\n";
    out << "normalStrength " << normalStrength << "\n";
    out << "tiling " << tiling.x << ' ' << tiling.y << "\n";
    out << "emission " << emission.x << ' ' << emission.y << ' ' << emission.z << "\n";
    return static_cast<bool>(out);
}
