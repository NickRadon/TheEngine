#pragma once

#include <string>
#include <vector>

// Thin OS layer (Windows implementation in Platform.cpp). All paths are UTF-8.
namespace Platform
{
    // Native folder / file pickers. Return "" (or empty list) when cancelled.
    std::string PickFolder(const std::string& title, const std::string& startDir = "");
    std::vector<std::string> PickFiles(const std::string& title, const std::string& startDir = "");

    // Moves a file or folder to the Recycle Bin (recoverable), instead of deleting it permanently.
    bool MoveToRecycleBin(const std::string& path);
    void RevealInExplorer(const std::string& path);
    void OpenWithDefaultApp(const std::string& path);

    std::string AppDataDir();        // %APPDATA%/TheEngine (created on demand)
    std::string DocumentsDir();      // the user's Documents folder
    std::string ExecutablePath();
    std::string ExecutableDir();

    // Starts a new instance of this executable (used to go back to the project launcher).
    bool LaunchSelf(const std::vector<std::string>& args);

    // Runs a command line, waits for it and returns combined stdout/stderr.
    struct ProcessResult
    {
        int exitCode = -1;
        std::string output;
    };
    ProcessResult RunProcess(const std::string& commandLine, const std::string& workingDir);

    // Locates an executable on PATH (e.g. "dotnet"). Returns "" if not found.
    std::string FindOnPath(const std::string& exe);

    std::wstring Widen(const std::string& utf8);
    std::string Narrow(const std::wstring& wide);
}
