#pragma once

#include <iosfwd>
#include <string>
#include <vector>

// Blend mask asset (.mask), stored as text: the bones a layer drives.
//
// A bone in the mask takes the layer's pose together with all of its descendants (the same rule as a
// layer's inline mask list), so "spine_01" alone already means "everything from the spine up". Masks
// are authored in the Blend Mask window and referenced by Animator Controller layers, which lets one
// mask be shared by several controllers (upper body over a lower-body locomotion layer, and so on).
struct BlendMask
{
    static constexpr int kCurrentVersion = 1;

    std::vector<std::string> bones; // include list; order is the order they were ticked in

    bool Load(const std::string& path);
    bool Load(std::istream& in);
    bool LoadString(const std::string& text);
    bool Save(const std::string& path) const;
    bool Save(std::ostream& out) const;
    std::string ToString() const; // serialized form (undo snapshots, editor state)

    // `.mask` written by TheEngine. Unity's YAML Avatar Masks are skipped.
    static bool IsMaskFile(const std::string& path);
    bool Contains(const std::string& bone) const;
    void Add(const std::string& bone);    // keeps the list free of duplicates
    void Remove(const std::string& bone);
};
