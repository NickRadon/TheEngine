// Minimal GLSL -> SPIR-V compiler built on the glslang library.
// Usage: shaderc_mini <input.(vert|frag|comp)> <output.spv> [include_dir]
// Exists because glslang's own CLI requires Python at build time.

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

class Includer : public glslang::TShader::Includer
{
public:
    explicit Includer(std::vector<std::string> dirs) : m_Dirs(std::move(dirs)) {}

    IncludeResult* includeLocal(const char* header, const char*, size_t) override { return Find(header); }
    IncludeResult* includeSystem(const char* header, const char*, size_t) override { return Find(header); }

    void releaseInclude(IncludeResult* result) override
    {
        if (!result) return;
        delete static_cast<std::string*>(result->userData);
        delete result;
    }

private:
    IncludeResult* Find(const char* header)
    {
        for (const auto& dir : m_Dirs)
        {
            auto* content = new std::string();
            std::string path = dir + "/" + header;
            if (ReadFile(path, *content))
                return new IncludeResult(path, content->data(), content->size(), content);
            delete content;
        }
        return nullptr;
    }

    std::vector<std::string> m_Dirs;
};

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <input> <output.spv> [include_dir]\n", argv[0]);
        return 1;
    }

    const std::string input = argv[1];
    const std::string output = argv[2];

    EShLanguage stage;
    const std::string ext = input.substr(input.find_last_of('.') + 1);
    if (ext == "vert") stage = EShLangVertex;
    else if (ext == "frag") stage = EShLangFragment;
    else if (ext == "comp") stage = EShLangCompute;
    else { std::fprintf(stderr, "unknown shader stage for %s\n", input.c_str()); return 1; }

    std::string source;
    if (!ReadFile(input, source)) { std::fprintf(stderr, "cannot read %s\n", input.c_str()); return 1; }

    std::vector<std::string> includeDirs;
    includeDirs.push_back(input.substr(0, input.find_last_of("/\\")));
    if (argc > 3) includeDirs.push_back(argv[3]);

    glslang::InitializeProcess();
    int result = 0;
    {
        glslang::TShader shader(stage);
        const char* src = source.c_str();
        const char* name = input.c_str();
        shader.setStringsWithLengthsAndNames(&src, nullptr, &name, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

        Includer includer(includeDirs);
        const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 460, false, messages, includer))
        {
            std::fprintf(stderr, "%s\n%s\n", shader.getInfoLog(), shader.getInfoDebugLog());
            result = 1;
        }
        else
        {
            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(messages))
            {
                std::fprintf(stderr, "%s\n", program.getInfoLog());
                result = 1;
            }
            else
            {
                std::vector<unsigned int> spirv;
                glslang::SpvOptions options;
                options.validate = false;
                glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &options);
                std::ofstream out(output, std::ios::binary);
                out.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(unsigned int));
                if (!out) { std::fprintf(stderr, "cannot write %s\n", output.c_str()); result = 1; }
            }
        }
    }
    glslang::FinalizeProcess();
    return result;
}
