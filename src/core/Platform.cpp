#include "core/Platform.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace Platform
{
#ifdef _WIN32
    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    namespace
    {
        struct ComScope
        {
            HRESULT hr;
            ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); }
            ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
        };

        std::vector<std::string> RunFileDialog(const std::string& title, const std::string& startDir, bool folders, bool multi)
        {
            std::vector<std::string> result;
            ComScope com;
            IFileOpenDialog* dialog = nullptr;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) return result;

            DWORD options = 0;
            dialog->GetOptions(&options);
            options |= FOS_FORCEFILESYSTEM;
            if (folders) options |= FOS_PICKFOLDERS;
            if (multi) options |= FOS_ALLOWMULTISELECT;
            dialog->SetOptions(options);
            dialog->SetTitle(Widen(title).c_str());
            if (!startDir.empty() && fs::exists(startDir))
            {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(Widen(fs::absolute(startDir).string()).c_str(), nullptr, IID_PPV_ARGS(&folder))))
                {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }

            if (SUCCEEDED(dialog->Show(GetActiveWindow())))
            {
                IShellItemArray* items = nullptr;
                if (SUCCEEDED(dialog->GetResults(&items)))
                {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i)
                    {
                        IShellItem* item = nullptr;
                        if (FAILED(items->GetItemAt(i, &item))) continue;
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                        {
                            result.push_back(Narrow(path));
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                    items->Release();
                }
            }
            dialog->Release();
            return result;
        }
    }

    std::string PickFolder(const std::string& title, const std::string& startDir)
    {
        auto r = RunFileDialog(title, startDir, true, false);
        return r.empty() ? std::string() : r[0];
    }

    std::vector<std::string> PickFiles(const std::string& title, const std::string& startDir)
    {
        return RunFileDialog(title, startDir, false, true);
    }

    bool MoveToRecycleBin(const std::string& path)
    {
        // SHFileOperation needs a double-null-terminated absolute path.
        std::wstring from = Widen(fs::absolute(path).string());
        from.push_back(L'\0');
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
    }

    void RevealInExplorer(const std::string& path)
    {
        std::wstring args = L"/select,\"" + Widen(fs::absolute(path).make_preferred().string()) + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void OpenWithDefaultApp(const std::string& path)
    {
        ShellExecuteW(nullptr, L"open", Widen(fs::absolute(path).string()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    std::string AppDataDir()
    {
        PWSTR roaming = nullptr;
        std::string dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
        {
            dir = Narrow(roaming) + "\\TheEngine";
            CoTaskMemFree(roaming);
        }
        else
        {
            dir = "TheEngineData";
        }
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }

    std::string DocumentsDir()
    {
        PWSTR docs = nullptr;
        std::string dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        {
            dir = Narrow(docs);
            CoTaskMemFree(docs);
        }
        return dir;
    }

    std::string ExecutablePath()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return Narrow(path);
    }

    std::string ExecutableDir()
    {
        return fs::path(ExecutablePath()).parent_path().string();
    }

    bool LaunchSelf(const std::vector<std::string>& args)
    {
        std::wstring cmd = L"\"" + Widen(ExecutablePath()) + L"\"";
        for (const auto& a : args) cmd += L" \"" + Widen(a) + L"\"";
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::wstring workDir = Widen(ExecutableDir());
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) return false;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    ProcessResult RunProcess(const std::string& commandLine, const std::string& workingDir)
    {
        ProcessResult result;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE readPipe = nullptr, writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return result;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = Widen(commandLine);
        std::wstring dir = Widen(workingDir);
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                 dir.empty() ? nullptr : dir.c_str(), &si, &pi);
        CloseHandle(writePipe);
        if (!ok)
        {
            CloseHandle(readPipe);
            result.output = "Failed to start: " + commandLine;
            return result;
        }

        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
            result.output.append(buffer, read);
        CloseHandle(readPipe);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result.exitCode = static_cast<int>(code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return result;
    }

    std::string FindOnPath(const std::string& exe)
    {
        wchar_t found[MAX_PATH];
        if (SearchPathW(nullptr, Widen(exe).c_str(), L".exe", MAX_PATH, found, nullptr)) return Narrow(found);
        return {};
    }
#else
    std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
    std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
    std::string PickFolder(const std::string&, const std::string&) { return {}; }
    std::vector<std::string> PickFiles(const std::string&, const std::string&) { return {}; }
    bool MoveToRecycleBin(const std::string& path) { std::error_code ec; return fs::remove_all(path, ec) > 0; }
    void RevealInExplorer(const std::string&) {}
    void OpenWithDefaultApp(const std::string&) {}
    std::string AppDataDir() { return "TheEngineData"; }
    std::string DocumentsDir() { return "."; }
    std::string ExecutablePath() { return {}; }
    std::string ExecutableDir() { return "."; }
    bool LaunchSelf(const std::vector<std::string>&) { return false; }
    ProcessResult RunProcess(const std::string&, const std::string&) { return {}; }
    std::string FindOnPath(const std::string&) { return {}; }
#endif
}
