#include "anim/BlendMask.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    const char* kHeader = "TheEngineMask";
}

bool BlendMask::IsMaskFile(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".mask") return false;
    std::ifstream in(path);
    std::string header;
    in >> header;
    return header == kHeader;
}

bool BlendMask::Load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    return Load(in);
}

bool BlendMask::Load(std::istream& in)
{
    std::string header;
    int version = 0;
    in >> header >> version;
    if (header != kHeader) return false;
    std::string line;
    std::getline(in, line);
    std::vector<std::string> loaded;
    while (std::getline(in, line))
    {
        std::istringstream row(line);
        std::string key, bone;
        if (!(row >> key)) continue;
        if (key != "bone") continue;
        if (row >> std::quoted(bone))
            if (!bone.empty()) loaded.push_back(bone);
    }
    bones = std::move(loaded);
    return true;
}

bool BlendMask::LoadString(const std::string& text)
{
    std::istringstream in(text);
    return Load(in);
}

bool BlendMask::Save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    return Save(out);
}

bool BlendMask::Save(std::ostream& out) const
{
    out << kHeader << ' ' << kCurrentVersion << "\n";
    for (const std::string& b : bones) out << "bone " << std::quoted(b) << "\n";
    return static_cast<bool>(out);
}

std::string BlendMask::ToString() const
{
    std::ostringstream out;
    Save(out);
    return out.str();
}

bool BlendMask::Contains(const std::string& bone) const
{
    return std::find(bones.begin(), bones.end(), bone) != bones.end();
}

void BlendMask::Add(const std::string& bone)
{
    if (bone.empty() || Contains(bone)) return;
    bones.push_back(bone);
}

void BlendMask::Remove(const std::string& bone)
{
    bones.erase(std::remove(bones.begin(), bones.end(), bone), bones.end());
}
