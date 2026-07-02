#pragma once
#include "pch.h"
#include <format>
#include <string>
#include <io.h>
#include <cstdio>
#include <winrt/Windows.Storage.h>

// Returns the directory (with a trailing backslash) the log file should live in.
//
// The plugin runs as an AppContainer / app-silo packaged app (see
// Package.appxmanifest -> uap18:TrustLevel="appContainer"). Inside that sandbox
// %TEMP% / GetTempPathW is redirected into the package's private, hard-to-find
// AC folder (...\Packages\<PFN>\AC\Temp), so we instead write to the package's
// LocalState folder, which is guaranteed writable and easy to locate at
//   %LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalState
// When running unpackaged (no package identity) we fall back to %TEMP%.
inline std::wstring GetLogDirectory()
{
    try
    {
        auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
        std::wstring dir = localFolder.Path().c_str();
        if (!dir.empty())
        {
            if (dir.back() != L'\\')
            {
                dir += L'\\';
            }
            return dir;
        }
    }
    catch (...)
    {
        // No package identity, or COM/WinRT is not initialized on this thread yet.
        // Fall back to the temp directory below.
    }

    wchar_t tempPath[MAX_PATH] = {};
    const DWORD length = ::GetTempPathW(MAX_PATH, tempPath);
    return (length > 0 && length < MAX_PATH) ? std::wstring(tempPath, length) : std::wstring();
}

// Builds the full path of the log file: <log dir>\SamplePlugin_<pid>.log
// The process id is included so that concurrent plugin instances do not clobber
// each other's logs.
inline std::wstring GetLogFilePath()
{
    return GetLogDirectory() + std::format(L"SamplePlugin_{}.log", ::GetCurrentProcessId());
}

// Redirects stdout (and stderr) to a log file exactly once per process, so every
// Log()/std::cout/std::wcout write lands in the file instead of a terminal
// console window. Thread-safe via C++ "magic static" initialization.
inline const std::wstring& EnsureLogFileOpened()
{
    static const std::wstring logFilePath = []() -> std::wstring
    {
        const std::wstring path = GetLogFilePath();

        FILE* fp = nullptr;
        const errno_t reopenResult = _wfreopen_s(&fp, path.c_str(), L"w", stdout);
        if (reopenResult == 0 && fp != nullptr)
        {
            // Route stderr to the same file so nothing is lost.
            _dup2(_fileno(stdout), _fileno(stderr));

            // Disable buffering so the log stays current even if the out-of-proc
            // server is torn down abruptly.
            setvbuf(stdout, nullptr, _IONBF, 0);
            setvbuf(stderr, nullptr, _IONBF, 0);

            // Surface the log location for anyone attached via a debugger / DebugView.
            ::OutputDebugStringW((L"SamplePlugin logging to: " + path + L"\n").c_str());
        }
        else
        {
            // Redirect failed: report where we tried and why, so logs aren't lost silently.
            ::OutputDebugStringW(std::format(L"SamplePlugin failed to open log file '{}' (errno {})\n",
                path, static_cast<int>(reopenResult)).c_str());
        }

        return path;
    }();

    return logFilePath;
}

inline void Log(const wchar_t* message)
{
    EnsureLogFileOpened();
    wprintf(L"%s\n", message);
}

inline std::wstring GetErrorDescription(HRESULT hr)
{
    wchar_t* errorMsg = nullptr;
    std::wstring errorStr;

    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&errorMsg), 0, nullptr) != 0)
    {
        errorStr = errorMsg;
        LocalFree(errorMsg);

        while (!errorStr.empty() && (errorStr.back() == L'\n' || errorStr.back() == L'\r'))
        {
            errorStr.pop_back();
        }
    }
    return errorStr;
}

inline std::wstring Log(const wchar_t* functionName, HRESULT hr)
{
    const wchar_t* statusText = SUCCEEDED(hr) ? L"succeeded" : L"failed";

    std::wstring errorDescription;
    if (!SUCCEEDED(hr))
    {
        errorDescription = GetErrorDescription(hr);
    }

    std::wstring message = std::format(L"{} {} with HRESULT 0x{:08X}{}",
        functionName,
        statusText,
        static_cast<unsigned int>(hr),
        (!errorDescription.empty() ? std::format(L": {}", errorDescription) : L""));

    Log(message.c_str());

    return message;
}