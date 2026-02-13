#include "pch.h"
#include "IWTSVirtualChannelCallbackImpl.h"
#include "IWTSWindowChangedCallbackImpl.h"

// Generate a simple GUID-like string for pipe name
std::string GenerateTestPipeName() 
{
    GUID guid;
    CoCreateGuid(&guid);

    wchar_t buffer[64];
    StringFromGUID2(guid, buffer, 64);

    Log(buffer);

    std::wstring ws(buffer);
    std::string s(ws.begin(), ws.end());

    return s + "zoomhdx";  // Mimic Zoom's naming convention
}

void PrintLastError(std::string operation) 
{
    DWORD err = GetLastError();

    LPSTR msg = nullptr;

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        0,
        (LPSTR)&msg,
        0,
        nullptr
    );

    std::cout << operation << "; Error " << err << ": " << (msg ? msg : "Unknown error") << std::endl;

    if (msg)
    {
        LocalFree(msg);
    }
}


HANDLE TestCreateNamedPipe(const char* pipeName, bool useDefaultSecurity, const char* testName) {
    std::string fullPipeName = std::string("\\\\.\\pipe\\") + pipeName;

    std::cout << "--- " << testName << " ---" << std::endl;
    std::cout << "Pipe Name: " << fullPipeName << std::endl;
    std::cout << "Security: " << (useDefaultSecurity ? "Default (NULL)" : "Custom (NULL DACL - Allow All)") << std::endl;

    HANDLE hPipe = INVALID_HANDLE_VALUE;

    if (useDefaultSecurity) 
    {
        // Method 1: Default security (NULL) - similar to what Zoom might be using
        hPipe = CreateNamedPipeA(
            fullPipeName.c_str(),
            PIPE_ACCESS_DUPLEX,                    // Read/write access
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,  // Byte-type pipe
            1,                                      // Max instances
            4096,                                   // Output buffer size
            4096,                                   // Input buffer size
            0,                                      // Default timeout
            NULL                                    // Default security attributes
        );
    }
    else 
    {
        // Method 2: Explicit security allowing everyone
        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);  // NULL DACL = allow all

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;

        hPipe = CreateNamedPipeA(
            fullPipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            &sa
        );
    }

    if (hPipe == INVALID_HANDLE_VALUE) 
    {
        PrintLastError("CreateNamedPipe");
        Log(L"Result: FAILURE");
        return INVALID_HANDLE_VALUE;
    }

    std::cout << "[SUCCESS] Pipe created successfully! Handle: 0x" << std::hex << hPipe << std::dec << std::endl;
    Log(L"Result: SUCCESS");
    return hPipe;
}

void TestNamespaceVariant(const char* baseName, const char* namespacePrefix, const char* testName) 
{
    std::string fullPipeName;
    if (namespacePrefix && strlen(namespacePrefix) > 0) 
    {
        fullPipeName = std::string("\\\\.\\pipe\\") + namespacePrefix + "\\" + baseName;
    }
    else 
    {
        fullPipeName = std::string("\\\\.\\pipe\\") + baseName;
    }

    std::cout << "--- " << testName << " ---" << std::endl;
    std::cout << "Pipe Name: " << fullPipeName << std::endl;

    HANDLE hPipe = CreateNamedPipeA(
        fullPipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) 
    {
        PrintLastError("CreateNamedPipe");
        std::cout << "Result: FAILURE" << std::endl << std::endl;
    }
    else 
    {
        std::cout << "[SUCCESS] Pipe created successfully! Handle: 0x" << std::hex << hPipe << std::dec << std::endl;
        Log(L"Result: SUCCESS");
        CloseHandle(hPipe);
    }
}

CToyPluginVirtualChannelCallbackImpl::CToyPluginVirtualChannelCallbackImpl(
    ComPtr<IWTSVirtualChannel> channel, ComPtr<IWTSWindowInfoService> pWindowInfoService) 
    : _pChannel(channel), _pWindowInfoService(pWindowInfoService)
{
    _pWindowChangedCallback = Make<CToyPluginWindowChangedCallbackImpl>();
}

IFACEMETHODIMP CToyPluginVirtualChannelCallbackImpl::OnDataReceived(ULONG cbSize, BYTE* pBuffer)
{
    Log(L"Entering CToyPluginVirtualChannelCallbackImpl::OnDataReceived");
    std::string data(reinterpret_cast<char*>(pBuffer), cbSize);
    std::cout << "Plugin received: " << data << std::endl;

    const char* response = "Goku left the power pole in between Kami's lookout and Coren Tower.";
    _pChannel->Write(strlen(response), (BYTE*)response, nullptr);
    
    // Note: We are passing a nullptr here since, this is just a sample. In real world scenario, you should pass a valid HWND of the window which we want to track.
    _pWindowInfoService->SubscribeWindowChanged(nullptr, _pWindowChangedCallback.Get());

    std::cout << "***** Creating named pipe. *****" << std::endl;
    // Generate test pipe name (similar to Zoom's format)
    std::string testPipeName = GenerateTestPipeName();
    std::cout << "Generated Test Pipe Name: " << testPipeName << std::endl;

    Log(L"=== Starting Pipe Creation Tests ===\n\n");

    // Test 1: Default security (NULL) - This is likely what Zoom uses
    HANDLE hPipe1 = TestCreateNamedPipe(testPipeName.c_str(), true, "Test 1: Default Security (NULL)");
    if (hPipe1 != INVALID_HANDLE_VALUE) 
    {
        CloseHandle(hPipe1);
    }

    // Test 2: Explicit permissive security
    std::string testPipeName2 = GenerateTestPipeName();
    HANDLE hPipe2 = TestCreateNamedPipe(testPipeName2.c_str(), false, "Test 2: Explicit Permissive Security (NULL DACL)");
    if (hPipe2 != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe2);
    }

    // Test 3: Global namespace
    std::string testPipeName3 = GenerateTestPipeName();
    TestNamespaceVariant(testPipeName3.c_str(), "Global", "Test 3: Global Namespace");

    // Test 4: Local namespace
    std::string testPipeName4 = GenerateTestPipeName();
    TestNamespaceVariant(testPipeName4.c_str(), "Local", "Test 4: Local Namespace");

    // Test 5: Hardcoded name similar to Zoom's actual name
    TestNamespaceVariant("TESTGUID-1234-5678-ABCD-EF0123456789zoomhdx", "", "Test 5: Zoom-style Pipe Name");

    // Test 6: Simple short name
    TestNamespaceVariant("ZoomPipeTest", "", "Test 6: Simple Short Name");

    Log(L"========================================\n");
    Log(L"  Test Complete\n");
    Log(L"========================================\n\n");

    Log(L"Exiting CToyPluginVirtualChannelCallbackImpl::OnDataReceived");

    return S_OK;
}

IFACEMETHODIMP CToyPluginVirtualChannelCallbackImpl::OnClose(void)
{
    Log(L"Entering CToyPluginVirtualChannelCallbackImpl::OnClose"); 
    Log(L"Exiting CToyPluginVirtualChannelCallbackImpl::OnClose");
    return S_OK;
}
