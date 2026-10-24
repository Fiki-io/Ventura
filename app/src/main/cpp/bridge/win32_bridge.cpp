#include "win32_bridge.h"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ventura {

namespace {
constexpr uint32_t kComIDirect3D9ObjVa       = 0x60001000;
constexpr uint32_t kComIDirect3D9VtblVa      = 0x60001100;
constexpr uint32_t kComIDirect3DDevice9ObjVa = 0x60002000;
constexpr uint32_t kComIDirect3DDevice9VtblVa= 0x60002100;
constexpr uint32_t kComID3DXFontObjVa        = 0x60003000;
constexpr uint32_t kComID3DXFontVtblVa       = 0x60003100;
constexpr uint32_t kComID3DXSpriteObjVa      = 0x60004000;
constexpr uint32_t kComID3DXSpriteVtblVa     = 0x60004100;
constexpr uint32_t kComIDirectInput8ObjVa    = 0x60005000;
constexpr uint32_t kComIDirectInput8VtblVa   = 0x60005100;
constexpr uint32_t kComIDirectInputDevObjVa  = 0x60006000;
constexpr uint32_t kComIDirectInputDevVtblVa = 0x60006100;
constexpr uint32_t kComIDirectSound8ObjVa    = 0x60007000;
constexpr uint32_t kComIDirectSound8VtblVa   = 0x60007100;
constexpr uint32_t kComIDirectSoundBufObjVa  = 0x60007500;
constexpr uint32_t kComIDirectSoundBufVtblVa = 0x60007600;
constexpr uint32_t kComID3D9VbVtblVa         = 0x60008100;
constexpr uint32_t kComID3D9IbVtblVa         = 0x60008300;
constexpr uint32_t kComID3D9TexVtblVa        = 0x60008500;

uint32_t PopStackArg(MemoryArena& arena, CpuState32& cpu) {
    uint32_t val = arena.ReadU32(cpu.esp);
    cpu.esp += 4;
    return val;
}

uint32_t DefaultStdcallBytesFor(const std::string& fn) {
    if (fn == "GetCommandLineA" || fn == "GetCommandLineW" || fn == "GetTickCount" ||
        fn == "timeGetTime" || fn == "GetLastError" || fn == "WSAGetLastError" ||
        fn == "WSACleanup" || fn == "GetACP" || fn == "GetOEMCP" ||
        fn == "GetEnvironmentStrings" || fn == "GetEnvironmentStringsA" ||
        fn == "GetEnvironmentStringsW" || fn == "GetActiveWindow" ||
        fn == "GetForegroundWindow" || fn == "GetDesktopWindow" || fn == "TlsAlloc" ||
        fn == "GetVersion" || fn == "GetCurrentThreadId" || fn == "GetCurrentProcessId" ||
        fn == "GetCurrentProcess" || fn == "GetCurrentThread" || fn == "IsDebuggerPresent" ||
        fn == "CoUninitialize" || fn == "ReleaseCapture" || fn == "GetCapture" ||
        fn == "GetDoubleClickTime") {
        return 0;
    }
    if (fn == "GetModuleHandleA" || fn == "GetModuleHandleW" || fn == "LoadLibraryA" ||
        fn == "LoadLibraryW" || fn == "FreeLibrary" || fn == "CloseHandle" ||
        fn == "QueryPerformanceCounter" || fn == "QueryPerformanceFrequency" ||
        fn == "Sleep" || fn == "ExitProcess" || fn == "RegisterClassA" || fn == "UpdateWindow" ||
        fn == "TranslateMessage" || fn == "DispatchMessageA" || fn == "GetAsyncKeyState" ||
        fn == "GetKeyState" || fn == "ShowCursor" || fn == "SetFocus" || fn == "GetDC" ||
        fn == "RegCloseKey" || fn == "timeBeginPeriod" || fn == "timeEndPeriod" ||
        fn == "closesocket" || fn == "inet_addr" || fn == "htons" || fn == "htonl" ||
        fn == "ntohs" || fn == "ntohl" || fn == "gethostbyname" || fn == "Direct3DCreate9" ||
        fn == "GetStartupInfoA" || fn == "GetStartupInfoW" || fn == "GetStdHandle" ||
        fn == "GetFileType" || fn == "FreeEnvironmentStringsA" || fn == "FreeEnvironmentStringsW" ||
        fn == "InitializeCriticalSection" || fn == "EnterCriticalSection" ||
        fn == "LeaveCriticalSection" || fn == "DeleteCriticalSection" ||
        fn == "TryEnterCriticalSection" || fn == "InterlockedIncrement" ||
        fn == "InterlockedDecrement" || fn == "TlsGetValue" || fn == "TlsFree" ||
        fn == "FlsAlloc" || fn == "FlsGetValue" || fn == "FlsFree" ||
        fn == "SetCurrentDirectoryA" || fn == "FindClose" || fn == "PostQuitMessage" ||
        fn == "GetVersionExA" || fn == "GetVersionExW" || fn == "SetHandleCount" ||
        fn == "SetLastError" || fn == "GlobalMemoryStatus" || fn == "GetSystemInfo" ||
        fn == "GetSystemTimeAsFileTime" || fn == "GetLocalTime" || fn == "GetSystemTime" ||
        fn == "GetTimeZoneInformation" || fn == "SetUnhandledExceptionFilter" ||
        fn == "UnhandledExceptionFilter" || fn == "SetErrorMode" || fn == "GetSystemMetrics" ||
        fn == "CoInitialize" || fn == "DestroyWindow" || fn == "SetForegroundWindow" ||
        fn == "BringWindowToTop" || fn == "SetActiveWindow" || fn == "SetCapture" ||
        fn == "ClipCursor" || fn == "SetCursor" || fn == "GetKeyboardState" ||
        fn == "GetKeyboardLayout" || fn == "CharUpperA" || fn == "CharLowerA" ||
        fn == "HeapDestroy" || fn == "FlushFileBuffers" || fn == "SetEndOfFile" ||
        fn == "DeleteFileA" || fn == "RemoveDirectoryA" || fn == "GetFileAttributesA" ||
        fn == "GetDriveTypeA" || fn == "SetEvent" || fn == "ResetEvent" ||
        fn == "ReleaseMutex" || fn == "ResumeThread" || fn == "SuspendThread" ||
        fn == "GetThreadPriority" || fn == "EncodePointer" || fn == "DecodePointer" ||
        fn == "IsValidCodePage" || fn == "lstrlenA" || fn == "OutputDebugStringA" ||
        fn == "GlobalFree" || fn == "GlobalLock" || fn == "GlobalUnlock" || fn == "LocalFree" ||
        fn == "GetStockObject" || fn == "CreateCompatibleDC" || fn == "DeleteDC" ||
        fn == "DeleteObject" || fn == "CreateSolidBrush" || fn == "SwapBuffers") {
        return 4;
    }
    if (fn == "GetProcAddress" || fn == "ShowWindow" || fn == "GetClientRect" ||
        fn == "GetWindowRect" || fn == "ReleaseDC" || fn == "GetDeviceCaps" ||
        fn == "ScreenToClient" || fn == "ClientToScreen" || fn == "SetCursorPos" ||
        fn == "GetCursorPos" || fn == "WSAStartup" || fn == "GetFileSize" ||
        fn == "D3DXCreateSprite" || fn == "TlsSetValue" || fn == "FlsSetValue" ||
        fn == "InterlockedExchange" || fn == "GetCurrentDirectoryA" ||
        fn == "FindFirstFileA" || fn == "FindNextFileA" || fn == "WaitForSingleObject" ||
        fn == "GetCPInfo" || fn == "EnumDisplaySettingsA" || fn == "ChangeDisplaySettingsA" ||
        fn == "InitializeCriticalSectionAndSpinCount" || fn == "SetStdHandle" ||
        fn == "MoveFileA" || fn == "CreateDirectoryA" || fn == "SetFileAttributesA" ||
        fn == "GetTempPathA" || fn == "WinExec" || fn == "TerminateThread" ||
        fn == "SetThreadPriority" || fn == "GetExitCodeThread" || fn == "IsValidLocale" ||
        fn == "lstrcpyA" || fn == "lstrcatA" || fn == "lstrcmpA" || fn == "lstrcmpiA" ||
        fn == "GlobalAlloc" || fn == "LocalAlloc" || fn == "CoInitializeEx" ||
        fn == "SetWindowTextA" || fn == "LoadIconA" || fn == "LoadCursorA" ||
        fn == "MapVirtualKeyA" || fn == "SelectObject" || fn == "SetBkMode" ||
        fn == "SetBkColor" || fn == "SetTextColor" || fn == "ChoosePixelFormat" ||
        fn == "SystemTimeToFileTime" || fn == "FileTimeToLocalFileTime" ||
        fn == "FileTimeToSystemTime") {
        return 8;
    }
    if (fn == "HeapCreate" || fn == "HeapAlloc" || fn == "HeapFree" || fn == "HeapSize" ||
        fn == "VirtualFree" || fn == "VirtualQuery" || fn == "SetWindowLongA" ||
        fn == "GetWindowLongA" || fn == "socket" || fn == "bind" || fn == "connect" ||
        fn == "ioctlsocket" || fn == "DirectSoundCreate8" || fn == "EAXDirectSoundCreate" ||
        fn == "EAXDirectSoundCreate8" || fn == "GetModuleFileNameA" || fn == "GetModuleFileNameW" ||
        fn == "AdjustWindowRect" || fn == "CreateMutexA" || fn == "OpenMutexA" ||
        fn == "ReleaseSemaphore" || fn == "InterlockedCompareExchange" ||
        fn == "GetShortPathNameA" || fn == "CopyFileA" || fn == "lstrcpynA" ||
        fn == "MulDiv" || fn == "OffsetRect" || fn == "IntersectRect" || fn == "PtInRect" ||
        fn == "GetWindowTextA" || fn == "GetKeyNameTextA" || fn == "GetObjectA" ||
        fn == "CreatePen" || fn == "CreateCompatibleBitmap" || fn == "SetPixelFormat") {
        return 12;
    }
    if (fn == "VirtualAlloc" || fn == "VirtualProtect" || fn == "SetFilePointer" ||
        fn == "GetMessageA" || fn == "DefWindowProcA" || fn == "HeapReAlloc" ||
        fn == "CreateEventA" || fn == "GetStringTypeW" || fn == "GetLocaleInfoA" ||
        fn == "GetLocaleInfoW" || fn == "RtlUnwind" || fn == "RaiseException" ||
        fn == "GetFullPathNameA" || fn == "GetTempFileNameA" || fn == "WaitForMultipleObjects" ||
        fn == "CreateSemaphoreA" || fn == "SystemParametersInfoA" || fn == "AdjustWindowRectEx" ||
        fn == "MessageBoxA" || fn == "SendMessageA" || fn == "PostMessageA" ||
        fn == "GetTextExtentPoint32A" || fn == "DirectDrawCreateEx") {
        return 16;
    }
    if (fn == "ReadFile" || fn == "WriteFile" || fn == "PeekMessageA" || fn == "CallWindowProcA" ||
        fn == "RegOpenKeyExA" || fn == "select" || fn == "setsockopt" || fn == "DirectInput8Create" ||
        fn == "GetStringTypeA" || fn == "GetDiskFreeSpaceA" || fn == "CoCreateInstance" ||
        fn == "SetRect" || fn == "ToAscii" || fn == "TextOutA") {
        return 20;
    }
    if (fn == "RegQueryValueExA" || fn == "sendto" || fn == "recvfrom" ||
        fn == "MultiByteToWideChar" || fn == "CreateThread" || fn == "LCMapStringA" ||
        fn == "LCMapStringW" || fn == "CompareStringA" || fn == "CompareStringW" ||
        fn == "MoveWindow" || fn == "ToAsciiEx" || fn == "CreateDIBSection") {
        return 24;
    }
    if (fn == "CreateFileA" || fn == "CreateFileW" || fn == "DuplicateHandle" ||
        fn == "SetWindowPos") {
        return 28;
    }
    if (fn == "WideCharToMultiByte" || fn == "GetVolumeInformationA") return 32;
    if (fn == "BitBlt") return 36;
    if (fn == "CreateProcessA") return 40;
    if (fn == "StretchBlt") return 44;
    if (fn == "CreateWindowExA" || fn == "D3DXCreateFontA") return 48;
    if (fn == "CreateFontA") return 56;
    return 0;
}
}  // namespace

Win32Bridge::Win32Bridge() = default;

Win32Bridge::~Win32Bridge() {
    Shutdown();
}

void Win32Bridge::Shutdown() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    for (auto& kv : open_files_) {
        if (kv.second != nullptr) {
            std::fclose(kv.second);
        }
    }
    open_files_.clear();
    for (auto& sk : open_sockets_) {
        if (sk.second >= 0) {
            ::close(sk.second);
        }
    }
    open_sockets_.clear();
    tls_slots_.clear();
    message_queue_.clear();
}

uint32_t Win32Bridge::RegisterThunk(const std::string& dll, const std::string& fn, uint32_t stack_pop_bytes) {
    uint32_t resolved_pop = (stack_pop_bytes != 0) ? stack_pop_bytes : DefaultStdcallBytesFor(fn);
    for (auto& existing : thunks_) {
        if (existing.function_name == fn && existing.dll_name == dll) {
            if (resolved_pop != 0 && existing.stack_pop_bytes == 0) {
                existing.stack_pop_bytes = resolved_pop;
            }
            return existing.thunk_va;
        }
    }
    Win32ThunkEntry entry;
    entry.thunk_va = next_thunk_va_;
    entry.dll_name = dll;
    entry.function_name = fn;
    entry.stack_pop_bytes = resolved_pop;
    va_to_index_[entry.thunk_va] = thunks_.size();
    thunks_.push_back(entry);
    next_thunk_va_ += 4;
    return entry.thunk_va;
}

void Win32Bridge::Initialize(
    MemoryArena& arena,
    D3D9SurfaceRenderer* renderer,
    const std::string& game_root_dir,
    const std::string& player_nickname,
    const std::string& command_line
) {
    Shutdown();
    renderer_ = renderer;
    game_root_dir_ = game_root_dir;
    player_nickname_ = player_nickname;
    thunks_.clear();
    va_to_index_.clear();
    next_thunk_va_ = kWin32ThunkTableBaseVa;
    hooked_wndproc_va_ = 0;
    total_syscalls_ = 0;
    next_tls_index_ = 1;
    std::memset(key_state_, 0, sizeof(key_state_));

    arena.SetupTebPebAndCommandLine(command_line);

    // Core KERNEL32, USER32, GDI32, ADVAPI32, WINMM, WS2_32, D3D9, DInput8, DSound8 & Vorbis Thunks
    RegisterThunk("KERNEL32.dll", "GetCommandLineA", 0);
    RegisterThunk("KERNEL32.dll", "GetModuleHandleA", 4);
    RegisterThunk("KERNEL32.dll", "GetModuleFileNameA", 12);
    RegisterThunk("KERNEL32.dll", "LoadLibraryA", 4);
    RegisterThunk("KERNEL32.dll", "GetProcAddress", 8);
    RegisterThunk("KERNEL32.dll", "FreeLibrary", 4);
    RegisterThunk("KERNEL32.dll", "VirtualAlloc", 16);
    RegisterThunk("KERNEL32.dll", "VirtualFree", 12);
    RegisterThunk("KERNEL32.dll", "VirtualProtect", 16);
    RegisterThunk("KERNEL32.dll", "HeapCreate", 12);
    RegisterThunk("KERNEL32.dll", "HeapAlloc", 12);
    RegisterThunk("KERNEL32.dll", "HeapReAlloc", 16);
    RegisterThunk("KERNEL32.dll", "HeapFree", 12);
    RegisterThunk("KERNEL32.dll", "HeapSize", 12);
    RegisterThunk("KERNEL32.dll", "CreateFileA", 28);
    RegisterThunk("KERNEL32.dll", "ReadFile", 20);
    RegisterThunk("KERNEL32.dll", "WriteFile", 20);
    RegisterThunk("KERNEL32.dll", "SetFilePointer", 16);
    RegisterThunk("KERNEL32.dll", "GetFileSize", 8);
    RegisterThunk("KERNEL32.dll", "CloseHandle", 4);
    RegisterThunk("KERNEL32.dll", "GetTickCount", 0);
    RegisterThunk("KERNEL32.dll", "QueryPerformanceCounter", 4);
    RegisterThunk("KERNEL32.dll", "QueryPerformanceFrequency", 4);
    RegisterThunk("KERNEL32.dll", "Sleep", 4);
    RegisterThunk("KERNEL32.dll", "ExitProcess", 4);
    RegisterThunk("KERNEL32.dll", "GetStartupInfoA", 4);
    RegisterThunk("KERNEL32.dll", "GetStdHandle", 4);
    RegisterThunk("KERNEL32.dll", "GetFileType", 4);
    RegisterThunk("KERNEL32.dll", "GetLastError", 0);
    RegisterThunk("KERNEL32.dll", "SetLastError", 4);
    RegisterThunk("KERNEL32.dll", "GetACP", 0);
    RegisterThunk("KERNEL32.dll", "GetOEMCP", 0);
    RegisterThunk("KERNEL32.dll", "GetCPInfo", 8);
    RegisterThunk("KERNEL32.dll", "GetEnvironmentStrings", 0);
    RegisterThunk("KERNEL32.dll", "GetEnvironmentStringsW", 0);
    RegisterThunk("KERNEL32.dll", "FreeEnvironmentStringsA", 4);
    RegisterThunk("KERNEL32.dll", "FreeEnvironmentStringsW", 4);
    RegisterThunk("KERNEL32.dll", "MultiByteToWideChar", 24);
    RegisterThunk("KERNEL32.dll", "WideCharToMultiByte", 32);
    RegisterThunk("KERNEL32.dll", "InitializeCriticalSection", 4);
    RegisterThunk("KERNEL32.dll", "EnterCriticalSection", 4);
    RegisterThunk("KERNEL32.dll", "LeaveCriticalSection", 4);
    RegisterThunk("KERNEL32.dll", "DeleteCriticalSection", 4);
    RegisterThunk("KERNEL32.dll", "TlsAlloc", 0);
    RegisterThunk("KERNEL32.dll", "TlsGetValue", 4);
    RegisterThunk("KERNEL32.dll", "TlsSetValue", 8);
    RegisterThunk("KERNEL32.dll", "TlsFree", 4);
    RegisterThunk("KERNEL32.dll", "InterlockedIncrement", 4);
    RegisterThunk("KERNEL32.dll", "InterlockedDecrement", 4);
    RegisterThunk("KERNEL32.dll", "InterlockedExchange", 8);
    RegisterThunk("KERNEL32.dll", "GetCurrentDirectoryA", 8);
    RegisterThunk("KERNEL32.dll", "SetCurrentDirectoryA", 4);
    RegisterThunk("KERNEL32.dll", "FindFirstFileA", 8);
    RegisterThunk("KERNEL32.dll", "FindNextFileA", 8);
    RegisterThunk("KERNEL32.dll", "FindClose", 4);

    RegisterThunk("USER32.dll", "RegisterClassA", 4);
    RegisterThunk("USER32.dll", "CreateWindowExA", 48);
    RegisterThunk("USER32.dll", "ShowWindow", 8);
    RegisterThunk("USER32.dll", "UpdateWindow", 4);
    RegisterThunk("USER32.dll", "PeekMessageA", 20);
    RegisterThunk("USER32.dll", "GetMessageA", 16);
    RegisterThunk("USER32.dll", "TranslateMessage", 4);
    RegisterThunk("USER32.dll", "DispatchMessageA", 4);
    RegisterThunk("USER32.dll", "DefWindowProcA", 16);
    RegisterThunk("USER32.dll", "GetClientRect", 8);
    RegisterThunk("USER32.dll", "GetWindowRect", 8);
    RegisterThunk("USER32.dll", "GetAsyncKeyState", 4);
    RegisterThunk("USER32.dll", "GetKeyState", 4);
    RegisterThunk("USER32.dll", "SetWindowLongA", 12);
    RegisterThunk("USER32.dll", "GetWindowLongA", 8);
    RegisterThunk("USER32.dll", "CallWindowProcA", 20);
    RegisterThunk("USER32.dll", "ShowCursor", 4);
    RegisterThunk("USER32.dll", "GetSystemMetrics", 4);
    RegisterThunk("USER32.dll", "GetDesktopWindow", 0);
    RegisterThunk("USER32.dll", "GetDC", 4);
    RegisterThunk("USER32.dll", "ReleaseDC", 8);

    RegisterThunk("ADVAPI32.dll", "RegOpenKeyExA", 20);
    RegisterThunk("ADVAPI32.dll", "RegQueryValueExA", 24);
    RegisterThunk("ADVAPI32.dll", "RegCloseKey", 4);

    RegisterThunk("WINMM.dll", "timeGetTime", 0);
    RegisterThunk("WINMM.dll", "timeBeginPeriod", 4);
    RegisterThunk("WINMM.dll", "timeEndPeriod", 4);

    RegisterThunk("WS2_32.dll", "WSAStartup", 8);
    RegisterThunk("WS2_32.dll", "WSACleanup", 0);
    RegisterThunk("WS2_32.dll", "WSAGetLastError", 0);
    RegisterThunk("WS2_32.dll", "socket", 12);
    RegisterThunk("WS2_32.dll", "bind", 12);
    RegisterThunk("WS2_32.dll", "connect", 12);
    RegisterThunk("WS2_32.dll", "sendto", 24);
    RegisterThunk("WS2_32.dll", "recvfrom", 24);
    RegisterThunk("WS2_32.dll", "select", 20);
    RegisterThunk("WS2_32.dll", "ioctlsocket", 12);
    RegisterThunk("WS2_32.dll", "setsockopt", 20);
    RegisterThunk("WS2_32.dll", "closesocket", 4);
    RegisterThunk("WS2_32.dll", "inet_addr", 4);
    RegisterThunk("WS2_32.dll", "htons", 4);
    RegisterThunk("WS2_32.dll", "htonl", 4);
    RegisterThunk("WS2_32.dll", "ntohs", 4);
    RegisterThunk("WS2_32.dll", "ntohl", 4);
    RegisterThunk("WS2_32.dll", "gethostbyname", 4);

    RegisterThunk("d3d9.dll", "Direct3DCreate9", 4);
    RegisterThunk("d3dx9_25.dll", "D3DXCreateFontA", 48);
    RegisterThunk("d3dx9_25.dll", "D3DXCreateSprite", 8);
    RegisterThunk("dinput8.dll", "DirectInput8Create", 20);
    RegisterThunk("ddraw.dll", "DirectDrawCreateEx", 16);
    RegisterThunk("dsound.dll", "DirectSoundCreate8", 12);
    RegisterThunk("eax.dll", "EAXDirectSoundCreate", 12);
    RegisterThunk("eax.dll", "EAXDirectSoundCreate8", 12);
    // vorbisfile.dll uses __cdecl (caller cleans stack, so stack_pop_bytes = 0)
    RegisterThunk("vorbisfile.dll", "ov_open_callbacks", 0);
    RegisterThunk("vorbisfile.dll", "ov_read", 0);
    RegisterThunk("vorbisfile.dll", "ov_clear", 0);
    RegisterThunk("vorbisfile.dll", "ov_info", 0);
    RegisterThunk("vorbisfile.dll", "ov_pcm_total", 0);
    RegisterThunk("vorbisfile.dll", "ov_time_total", 0);

    SetupComVtables(arena);
}

void Win32Bridge::SetupComVtables(MemoryArena& arena) {
    // Helper for generic __stdcall COM stubs that clean `arg_bytes` from stack and return S_OK (0)
    auto make_com_stub = [&](uint32_t arg_bytes) -> uint32_t {
        return RegisterThunk("COM_STUB", "ComOk_" + std::to_string(arg_bytes), arg_bytes);
    };

    // 1. IDirect3D9 Object & Vtable (17 methods)
    arena.WriteU32(kComIDirect3D9ObjVa, kComIDirect3D9VtblVa);
    static const uint32_t kD3D9ArgBytes[17] = {
        12, 4, 4, 8, 4, 12, 12, 12, 24, 28, 28, 24, 24, 20, 16, 8, 28
    };
    uint32_t d3d9_create_dev = RegisterThunk("COM_IDirect3D9", "CreateDevice", 28);
    uint32_t d3d9_get_adapter_count = RegisterThunk("COM_IDirect3D9", "GetAdapterCount", 4);
    uint32_t d3d9_get_mode_count = RegisterThunk("COM_IDirect3D9", "GetAdapterModeCount", 12);
    uint32_t d3d9_enum_modes = RegisterThunk("COM_IDirect3D9", "EnumAdapterModes", 20);
    uint32_t d3d9_get_display_mode = RegisterThunk("COM_IDirect3D9", "GetAdapterDisplayMode", 12);
    uint32_t d3d9_get_caps = RegisterThunk("COM_IDirect3D9", "GetDeviceCaps", 16);

    for (uint32_t i = 0; i < 17; ++i) {
        uint32_t target = make_com_stub(kD3D9ArgBytes[i]);
        if (i == 4) target = d3d9_get_adapter_count;
        else if (i == 6) target = d3d9_get_mode_count;
        else if (i == 7) target = d3d9_enum_modes;
        else if (i == 8) target = d3d9_get_display_mode;
        else if (i == 14) target = d3d9_get_caps;
        else if (i == 16) target = d3d9_create_dev;
        arena.WriteU32(kComIDirect3D9VtblVa + (i * 4), target);
    }

    // 2. IDirect3DVertexBuffer9, IDirect3DIndexBuffer9 & IDirect3DTexture9 Vtables
    uint32_t vb_lock_thunk = RegisterThunk("COM_IDirect3DVertexBuffer9", "Lock", 20);
    uint32_t vb_unlock_thunk = RegisterThunk("COM_IDirect3DVertexBuffer9", "Unlock", 4);
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t target = make_com_stub(4);
        if (i == 11) target = vb_lock_thunk;
        else if (i == 12) target = vb_unlock_thunk;
        arena.WriteU32(kComID3D9VbVtblVa + (i * 4), target);
    }

    uint32_t ib_lock_thunk = RegisterThunk("COM_IDirect3DIndexBuffer9", "Lock", 20);
    uint32_t ib_unlock_thunk = RegisterThunk("COM_IDirect3DIndexBuffer9", "Unlock", 4);
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t target = make_com_stub(4);
        if (i == 11) target = ib_lock_thunk;
        else if (i == 12) target = ib_unlock_thunk;
        arena.WriteU32(kComID3D9IbVtblVa + (i * 4), target);
    }

    uint32_t tex_lock_thunk = RegisterThunk("COM_IDirect3DTexture9", "LockRect", 20);
    uint32_t tex_unlock_thunk = RegisterThunk("COM_IDirect3DTexture9", "UnlockRect", 8);
    for (uint32_t i = 0; i < 24; ++i) {
        uint32_t target = make_com_stub(8);
        if (i == 19) target = tex_lock_thunk;
        else if (i == 20) target = tex_unlock_thunk;
        arena.WriteU32(kComID3D9TexVtblVa + (i * 4), target);
    }

    // 3. IDirect3DDevice9 Object & Vtable (119 methods)
    arena.WriteU32(kComIDirect3DDevice9ObjVa, kComIDirect3DDevice9VtblVa);
    uint32_t dev_present_thunk   = RegisterThunk("COM_IDirect3DDevice9", "Present", 20);
    uint32_t dev_create_tex      = RegisterThunk("COM_IDirect3DDevice9", "CreateTexture", 36);
    uint32_t dev_create_vb       = RegisterThunk("COM_IDirect3DDevice9", "CreateVertexBuffer", 28);
    uint32_t dev_create_ib       = RegisterThunk("COM_IDirect3DDevice9", "CreateIndexBuffer", 28);
    uint32_t dev_begin_thunk     = RegisterThunk("COM_IDirect3DDevice9", "BeginScene", 4);
    uint32_t dev_end_thunk       = RegisterThunk("COM_IDirect3DDevice9", "EndScene", 4);
    uint32_t dev_clear_thunk     = RegisterThunk("COM_IDirect3DDevice9", "Clear", 28);
    uint32_t dev_set_transform   = RegisterThunk("COM_IDirect3DDevice9", "SetTransform", 12);
    uint32_t dev_set_render_state= RegisterThunk("COM_IDirect3DDevice9", "SetRenderState", 12);
    uint32_t dev_set_texture     = RegisterThunk("COM_IDirect3DDevice9", "SetTexture", 12);
    uint32_t dev_draw_prim       = RegisterThunk("COM_IDirect3DDevice9", "DrawPrimitive", 16);
    uint32_t dev_draw_idx_thunk  = RegisterThunk("COM_IDirect3DDevice9", "DrawIndexedPrimitive", 28);
    uint32_t dev_draw_up_thunk   = RegisterThunk("COM_IDirect3DDevice9", "DrawPrimitiveUP", 20);
    uint32_t dev_set_fvf         = RegisterThunk("COM_IDirect3DDevice9", "SetFVF", 8);
    uint32_t dev_set_stream_src  = RegisterThunk("COM_IDirect3DDevice9", "SetStreamSource", 20);
    uint32_t dev_set_indices     = RegisterThunk("COM_IDirect3DDevice9", "SetIndices", 8);

    for (uint32_t i = 0; i < 119; ++i) {
        uint32_t target = make_com_stub(12);
        if (i == 17) target = dev_present_thunk;
        else if (i == 23) target = dev_create_tex;
        else if (i == 26) target = dev_create_vb;
        else if (i == 27) target = dev_create_ib;
        else if (i == 41) target = dev_begin_thunk;
        else if (i == 42) target = dev_end_thunk;
        else if (i == 43) target = dev_clear_thunk;
        else if (i == 44) target = dev_set_transform;
        else if (i == 57) target = dev_set_render_state;
        else if (i == 65) target = dev_set_texture;
        else if (i == 67 || i == 69) target = make_com_stub(16); // SetTextureStageState / SetSamplerState
        else if (i == 81) target = dev_draw_prim;
        else if (i == 82) target = dev_draw_idx_thunk;
        else if (i == 83) target = dev_draw_up_thunk;
        else if (i == 89) target = dev_set_fvf;
        else if (i == 92 || i == 107) target = make_com_stub(8); // SetVertexShader / SetPixelShader
        else if (i == 100) target = dev_set_stream_src;
        else if (i == 104) target = dev_set_indices;
        arena.WriteU32(kComIDirect3DDevice9VtblVa + (i * 4), target);
    }

    // Store active IDirect3DDevice9* at global GTA SA 1.0 US pointer 0x00C97C28
    arena.WriteU32(kGtaSaD3D9DevicePtrVa, kComIDirect3DDevice9ObjVa);
    arena.WriteU32(kGtaSaHwndVa, 0x00010020u);

    // 4. ID3DXFont & ID3DXSprite Vtables (for SA-MP chatbox, scoreboard & dialogs)
    arena.WriteU32(kComID3DXFontObjVa, kComID3DXFontVtblVa);
    uint32_t font_draw_thunk = RegisterThunk("COM_ID3DXFont", "DrawTextA", 28);
    for (uint32_t i = 0; i < 18; ++i) {
        arena.WriteU32(kComID3DXFontVtblVa + (i * 4), (i == 14 || i == 15) ? font_draw_thunk : make_com_stub(8));
    }
    arena.WriteU32(kComID3DXSpriteObjVa, kComID3DXSpriteVtblVa);
    for (uint32_t i = 0; i < 16; ++i) {
        arena.WriteU32(kComID3DXSpriteVtblVa + (i * 4), make_com_stub(8));
    }

    // 5. IDirectInput8 & IDirectInputDevice8 Vtables
    arena.WriteU32(kComIDirectInput8ObjVa, kComIDirectInput8VtblVa);
    uint32_t di8_create_dev = RegisterThunk("COM_IDirectInput8", "CreateDevice", 16);
    for (uint32_t i = 0; i < 12; ++i) {
        arena.WriteU32(kComIDirectInput8VtblVa + (i * 4), i == 3 ? di8_create_dev : make_com_stub(8));
    }
    arena.WriteU32(kComIDirectInputDevObjVa, kComIDirectInputDevVtblVa);
    uint32_t di8_get_state = RegisterThunk("COM_IDirectInputDevice8", "GetDeviceState", 12);
    for (uint32_t i = 0; i < 32; ++i) {
        uint32_t target = make_com_stub(8);
        if (i == 7 || i == 8) target = make_com_stub(4); // Acquire / Unacquire
        else if (i == 9) target = di8_get_state;
        else if (i == 11) target = make_com_stub(8); // SetDataFormat
        else if (i == 13) target = make_com_stub(12); // SetCooperativeLevel
        arena.WriteU32(kComIDirectInputDevVtblVa + (i * 4), target);
    }

    // 6. IDirectSound8 & IDirectSoundBuffer8 Vtables (for GTA SA CAEAudioHardware)
    arena.WriteU32(kComIDirectSound8ObjVa, kComIDirectSound8VtblVa);
    uint32_t ds8_create_buf = RegisterThunk("COM_IDirectSound8", "CreateSoundBuffer", 16);
    for (uint32_t i = 0; i < 12; ++i) {
        uint32_t target = make_com_stub(8);
        if (i == 3) target = ds8_create_buf;
        else if (i == 6) target = make_com_stub(12); // SetCooperativeLevel
        arena.WriteU32(kComIDirectSound8VtblVa + (i * 4), target);
    }
    arena.WriteU32(kComIDirectSoundBufObjVa, kComIDirectSoundBufVtblVa);
    uint32_t dsbuf_lock = RegisterThunk("COM_IDirectSoundBuffer8", "Lock", 32);
    uint32_t dsbuf_unlock = RegisterThunk("COM_IDirectSoundBuffer8", "Unlock", 20);
    for (uint32_t i = 0; i < 24; ++i) {
        uint32_t target = make_com_stub(8);
        if (i == 11) target = dsbuf_lock;
        else if (i == 12) target = make_com_stub(16); // Play
        else if (i == 18) target = make_com_stub(4);  // Stop
        else if (i == 19) target = dsbuf_unlock;
        arena.WriteU32(kComIDirectSoundBufVtblVa + (i * 4), target);
    }
}

std::string Win32Bridge::NormalizeWindowsPath(const std::string& win_path) const {
    std::string rel = win_path;
    for (char& c : rel) {
        if (c == '\\') c = '/';
    }
    while (!rel.empty() && (rel[0] == '.' || rel[0] == '/')) {
        rel.erase(rel.begin());
    }
    if (game_root_dir_.empty()) return rel;
    return game_root_dir_ + "/" + rel;
}

uint32_t Win32Bridge::ResolveProcAddress(const std::string& dll_name, const std::string& func_name) {
    for (const auto& entry : thunks_) {
        if (entry.function_name == func_name) {
            return entry.thunk_va;
        }
    }
    return RegisterThunk(dll_name, func_name, DefaultStdcallBytesFor(func_name));
}

bool Win32Bridge::IsThunkAddress(uint32_t va) const {
    return va >= kWin32ThunkTableBaseVa && va < next_thunk_va_;
}

void Win32Bridge::EnqueueKeyEvent(uint32_t virtual_key, bool is_down) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (virtual_key < 256) {
        key_state_[virtual_key] = is_down ? 0x80 : 0x00;
    }
    Win32Msg msg{};
    msg.hwnd = 0x00010020u;
    msg.message = is_down ? kWmKeyDown : kWmKeyUp;
    msg.wparam = virtual_key;
    msg.lparam = is_down ? 0x00010001u : 0xC0010001u;
    message_queue_.push_back(msg);
}

void Win32Bridge::EnqueueCharEvent(uint32_t char_code) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    Win32Msg msg{};
    msg.hwnd = 0x00010020u;
    msg.message = kWmChar;
    msg.wparam = char_code;
    msg.lparam = 1;
    message_queue_.push_back(msg);
}

void Win32Bridge::EnqueueMouseEvent(int32_t x, int32_t y, bool left_down) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    mouse_x_ = x;
    mouse_y_ = y;
    bool state_changed = (mouse_left_down_ != left_down);
    mouse_left_down_ = left_down;

    Win32Msg msg{};
    msg.hwnd = 0x00010020u;
    msg.message = state_changed ? (left_down ? kWmLButtonDown : kWmLButtonUp) : kWmMouseMove;
    msg.wparam = left_down ? 1u : 0u;
    msg.lparam = (static_cast<uint32_t>(y & 0xFFFF) << 16) | static_cast<uint32_t>(x & 0xFFFF);
    msg.pt_x = x;
    msg.pt_y = y;
    message_queue_.push_back(msg);
}

std::string Win32Bridge::GetLastSyscallSummary() const {
    std::lock_guard<std::mutex> lock(input_mutex_);
    return last_syscall_summary_;
}

bool Win32Bridge::DispatchThunkCall(uint32_t thunk_va, MemoryArena& arena, CpuState32& cpu) {
    auto it = va_to_index_.find(thunk_va);
    if (it == va_to_index_.end()) {
        return false;
    }

    Win32ThunkEntry& entry = thunks_[it->second];
    entry.invocation_count++;
    total_syscalls_++;

    uint32_t return_eip = PopStackArg(arena, cpu);
    uint32_t initial_esp = cpu.esp;
    const std::string& fn = entry.function_name;

    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        last_syscall_summary_ = entry.dll_name + "!" + fn;
    }

    if (entry.dll_name == "COM_STUB") {
        cpu.esp = initial_esp + entry.stack_pop_bytes;
        cpu.eax = 0; // S_OK
        cpu.eip = return_eip;
        return true;
    }

    if (fn == "GetCommandLineA") {
        cpu.eax = kCommandLineVa;
    } else if (fn == "GetVersion") {
        cpu.eax = 0x0A280105u; // Windows XP SP2 (5.1 build 2600)
    } else if (fn == "GetVersionExA") {
        uint32_t lp_ver = PopStackArg(arena, cpu);
        if (lp_ver != 0 && arena.IsValidRange(lp_ver, 20)) {
            arena.WriteU32(lp_ver + 4, 5);    // dwMajorVersion = 5
            arena.WriteU32(lp_ver + 8, 1);    // dwMinorVersion = 1
            arena.WriteU32(lp_ver + 12, 2600);// dwBuildNumber = 2600
            arena.WriteU32(lp_ver + 16, 2);   // VER_PLATFORM_WIN32_NT
        }
        cpu.eax = 1;
    } else if (fn == "SetHandleCount") {
        cpu.eax = PopStackArg(arena, cpu);
    } else if (fn == "GetCurrentThreadId") {
        cpu.eax = 0x00001000u;
    } else if (fn == "GetCurrentProcessId") {
        cpu.eax = 0x00002000u;
    } else if (fn == "GetCurrentProcess") {
        cpu.eax = 0xFFFFFFFFu;
    } else if (fn == "GetCurrentThread") {
        cpu.eax = 0xFFFFFFFEu;
    } else if (fn == "IsDebuggerPresent") {
        cpu.eax = 0;
    } else if (fn == "GlobalMemoryStatus") {
        uint32_t lp_mem = PopStackArg(arena, cpu);
        if (lp_mem != 0 && arena.IsValidRange(lp_mem, 32)) {
            arena.WriteU32(lp_mem + 0, 32);
            arena.WriteU32(lp_mem + 4, 20);
            arena.WriteU32(lp_mem + 8, 536870912u);  // 512 MB TotalPhys
            arena.WriteU32(lp_mem + 12, 402653184u); // 384 MB AvailPhys
            arena.WriteU32(lp_mem + 16, 1073741824u);
            arena.WriteU32(lp_mem + 20, 805306368u);
            arena.WriteU32(lp_mem + 24, 2147352576u);
            arena.WriteU32(lp_mem + 28, 1879048192u);
        }
        cpu.eax = 1;
    } else if (fn == "GetSystemInfo") {
        uint32_t lp_sys = PopStackArg(arena, cpu);
        if (lp_sys != 0 && arena.IsValidRange(lp_sys, 36)) {
            uint8_t zero[36] = {0};
            arena.WriteBytes(lp_sys, zero, 36);
            arena.WriteU32(lp_sys + 4, 4096);        // dwPageSize
            arena.WriteU32(lp_sys + 8, 0x00010000u); // lpMinimumApplicationAddress
            arena.WriteU32(lp_sys + 12, 0x7FFEFFFFu);// lpMaximumApplicationAddress
            arena.WriteU32(lp_sys + 16, 0x0000000Fu);// dwActiveProcessorMask
            arena.WriteU32(lp_sys + 20, 4);          // dwNumberOfProcessors
            arena.WriteU32(lp_sys + 24, 586);        // dwProcessorType
            arena.WriteU32(lp_sys + 28, 65536);      // dwAllocationGranularity
        }
        cpu.eax = 1;
    } else if (fn == "GetSystemMetrics") {
        uint32_t idx = PopStackArg(arena, cpu);
        if (idx == 0) cpu.eax = 1280;      // SM_CXSCREEN
        else if (idx == 1) cpu.eax = 720;  // SM_CYSCREEN
        else cpu.eax = 0;
    } else if (fn == "GetStringTypeA" || fn == "GetStringTypeW") {
        uint32_t is_a = (fn == "GetStringTypeA") ? 1u : 0u;
        if (is_a) PopStackArg(arena, cpu); // Locale
        PopStackArg(arena, cpu);           // dwInfoType
        PopStackArg(arena, cpu);           // lpSrcStr
        int32_t cch = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t lp_chartype = PopStackArg(arena, cpu);
        int32_t count = (cch < 0) ? 16 : std::min<int32_t>(cch, 256);
        if (lp_chartype != 0 && count > 0 && arena.IsValidRange(lp_chartype, static_cast<uint32_t>(count * 2))) {
            for (int32_t i = 0; i < count; ++i) {
                arena.WriteU16(lp_chartype + static_cast<uint32_t>(i * 2), 0x0001u);
            }
        }
        cpu.eax = 1;
    } else if (fn == "LCMapStringA" || fn == "LCMapStringW") {
        PopStackArg(arena, cpu); // Locale
        PopStackArg(arena, cpu); // dwMapFlags
        uint32_t src_va = PopStackArg(arena, cpu);
        int32_t cch_src = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t dst_va = PopStackArg(arena, cpu);
        int32_t cch_dst = static_cast<int32_t>(PopStackArg(arena, cpu));
        int32_t len = (cch_src <= 0) ? 4 : cch_src;
        if (cch_dst == 0) {
            cpu.eax = static_cast<uint32_t>(len);
        } else {
            if (dst_va != 0 && src_va != 0 && arena.IsValidRange(dst_va, static_cast<uint32_t>(len * 2))) {
                const uint8_t* s = arena.GetHostPointer(src_va);
                if (s) arena.WriteBytes(dst_va, s, static_cast<uint32_t>(len));
            }
            cpu.eax = static_cast<uint32_t>(len);
        }
    } else if (fn == "MultiByteToWideChar") {
        PopStackArg(arena, cpu); // CodePage
        PopStackArg(arena, cpu); // dwFlags
        uint32_t src_va = PopStackArg(arena, cpu);
        int32_t cb_mb = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t dst_va = PopStackArg(arena, cpu);
        int32_t cch_wc = static_cast<int32_t>(PopStackArg(arena, cpu));
        std::string s = (src_va != 0) ? arena.ReadCString(src_va, cb_mb > 0 ? static_cast<size_t>(cb_mb) : 256u) : "";
        uint32_t needed = static_cast<uint32_t>(s.size() + 1);
        if (cch_wc == 0 || dst_va == 0) {
            cpu.eax = needed;
        } else {
            uint32_t write_n = std::min<uint32_t>(needed, static_cast<uint32_t>(cch_wc));
            for (uint32_t i = 0; i < write_n; ++i) {
                uint16_t ch = (i < s.size()) ? static_cast<uint8_t>(s[i]) : 0u;
                arena.WriteU16(dst_va + i * 2, ch);
            }
            cpu.eax = write_n;
        }
    } else if (fn == "WideCharToMultiByte") {
        PopStackArg(arena, cpu); // CodePage
        PopStackArg(arena, cpu); // dwFlags
        uint32_t src_va = PopStackArg(arena, cpu);
        int32_t cch_wc = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t dst_va = PopStackArg(arena, cpu);
        int32_t cb_mb = static_cast<int32_t>(PopStackArg(arena, cpu));
        PopStackArg(arena, cpu); // lpDefaultChar
        uint32_t used_def_va = PopStackArg(arena, cpu);
        if (used_def_va != 0) arena.WriteU32(used_def_va, 0);
        uint32_t len = 0;
        if (src_va != 0) {
            uint32_t max_c = (cch_wc > 0) ? static_cast<uint32_t>(cch_wc) : 256u;
            while (len < max_c) {
                uint16_t w = arena.ReadU16(src_va + len * 2);
                len++;
                if (w == 0 && cch_wc < 0) break;
            }
        }
        if (len == 0) len = 1;
        if (cb_mb == 0 || dst_va == 0) {
            cpu.eax = len;
        } else {
            uint32_t write_n = std::min<uint32_t>(len, static_cast<uint32_t>(cb_mb));
            for (uint32_t i = 0; i < write_n; ++i) {
                uint8_t c = (src_va != 0) ? static_cast<uint8_t>(arena.ReadU16(src_va + i * 2) & 0x7Fu) : 0u;
                arena.WriteU8(dst_va + i, c);
            }
            cpu.eax = write_n;
        }
    } else if (fn == "GetModuleHandleA") {
        uint32_t mod_name_va = PopStackArg(arena, cpu);
        if (mod_name_va == 0) {
            cpu.eax = kGtaSaImageBase;
        } else {
            std::string name = arena.ReadCString(mod_name_va);
            cpu.eax = (name.find("samp") != std::string::npos) ? kSampModuleBaseVa : kGtaSaImageBase;
        }
    } else if (fn == "GetModuleFileNameA") {
        PopStackArg(arena, cpu); // hModule
        uint32_t buf_va = PopStackArg(arena, cpu);
        uint32_t size = PopStackArg(arena, cpu);
        std::string fake_path = "C:\\GTA San Andreas\\gta_sa.exe";
        if (buf_va != 0 && size > 0) {
            arena.WriteCString(buf_va, fake_path);
        }
        cpu.eax = static_cast<uint32_t>(fake_path.size());
    } else if (fn == "LoadLibraryA") {
        uint32_t lib_name_va = PopStackArg(arena, cpu);
        std::string lib_name = arena.ReadCString(lib_name_va);
        cpu.eax = (lib_name.find("samp") != std::string::npos) ? kSampModuleBaseVa : 0x71000000u;
    } else if (fn == "GetProcAddress") {
        PopStackArg(arena, cpu); // hMod
        uint32_t proc_name_va = PopStackArg(arena, cpu);
        std::string proc_name = (proc_name_va > 0x00010000u)
            ? arena.ReadCString(proc_name_va)
            : ("Ordinal_" + std::to_string(proc_name_va));
        cpu.eax = ResolveProcAddress("DYNAMIC", proc_name);
    } else if (fn == "FreeLibrary") {
        PopStackArg(arena, cpu);
        cpu.eax = 1;
    } else if (fn == "VirtualAlloc") {
        uint32_t addr = PopStackArg(arena, cpu);
        uint32_t size = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // type
        uint32_t prot = PopStackArg(arena, cpu);
        cpu.eax = arena.VirtualAllocSim(addr, size, prot);
    } else if (fn == "VirtualFree") {
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        cpu.eax = 1;
    } else if (fn == "VirtualProtect") {
        uint32_t addr = PopStackArg(arena, cpu);
        uint32_t size = PopStackArg(arena, cpu);
        uint32_t new_prot = PopStackArg(arena, cpu);
        uint32_t old_prot_ptr = PopStackArg(arena, cpu);
        uint32_t old_p = 0x40;
        bool ok = arena.VirtualProtectSim(addr, size, new_prot, &old_p);
        if (old_prot_ptr != 0) arena.WriteU32(old_prot_ptr, old_p);
        cpu.eax = ok ? 1 : 0;
    } else if (fn == "HeapCreate") {
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        cpu.eax = kHeapArenaBaseVa;
    } else if (fn == "HeapAlloc") {
        PopStackArg(arena, cpu); // hHeap
        uint32_t flags = PopStackArg(arena, cpu);
        uint32_t bytes = PopStackArg(arena, cpu);
        cpu.eax = arena.HeapAllocSim(bytes, (flags & 0x08u) != 0);
    } else if (fn == "HeapReAlloc") {
        PopStackArg(arena, cpu); // hHeap
        uint32_t flags = PopStackArg(arena, cpu);
        uint32_t old_mem = PopStackArg(arena, cpu);
        uint32_t new_bytes = PopStackArg(arena, cpu);
        uint32_t new_va = arena.HeapAllocSim(new_bytes, (flags & 0x08u) != 0);
        if (old_mem != 0 && new_va != 0 && arena.IsValidRange(old_mem, 16)) {
            const uint8_t* src = arena.GetHostPointer(old_mem);
            if (src) arena.WriteBytes(new_va, src, std::min<uint32_t>(new_bytes, 4096u));
        }
        cpu.eax = new_va;
    } else if (fn == "HeapFree") {
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        cpu.eax = 1;
    } else if (fn == "HeapSize") {
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        cpu.eax = 4096;
    } else if (fn == "GetStartupInfoA") {
        uint32_t info_va = PopStackArg(arena, cpu);
        if (info_va != 0 && arena.IsValidRange(info_va, 68)) {
            uint8_t zero[68] = {0};
            arena.WriteBytes(info_va, zero, 68);
            arena.WriteU32(info_va, 68);
        }
        cpu.eax = 0;
    } else if (fn == "GetStdHandle") {
        PopStackArg(arena, cpu);
        cpu.eax = 0x00000010u;
    } else if (fn == "GetFileType") {
        PopStackArg(arena, cpu);
        cpu.eax = 2; // FILE_TYPE_CHAR
    } else if (fn == "GetLastError") {
        cpu.eax = last_win32_error_;
    } else if (fn == "SetLastError") {
        last_win32_error_ = PopStackArg(arena, cpu);
        cpu.eax = 0;
    } else if (fn == "GetACP" || fn == "GetOEMCP") {
        cpu.eax = 1252;
    } else if (fn == "GetCPInfo") {
        PopStackArg(arena, cpu); // CodePage
        uint32_t lp_cpinfo = PopStackArg(arena, cpu);
        if (lp_cpinfo != 0 && arena.IsValidRange(lp_cpinfo, 20)) {
            uint8_t zero[20] = {0};
            arena.WriteBytes(lp_cpinfo, zero, 20);
            arena.WriteU32(lp_cpinfo, 1); // MaxCharSize = 1
            arena.WriteU8(lp_cpinfo + 4, '?');
        }
        cpu.eax = 1;
    } else if (fn == "GetEnvironmentStrings" || fn == "GetEnvironmentStringsA" || fn == "GetEnvironmentStringsW") {
        uint32_t env_va = arena.HeapAllocSim(64, true);
        cpu.eax = env_va;
    } else if (fn == "FreeEnvironmentStringsA" || fn == "FreeEnvironmentStringsW") {
        PopStackArg(arena, cpu);
        cpu.eax = 1;
    } else if (fn == "TlsAlloc" || fn == "FlsAlloc") {
        if (fn == "FlsAlloc") PopStackArg(arena, cpu);
        cpu.eax = next_tls_index_++;
    } else if (fn == "TlsGetValue" || fn == "FlsGetValue") {
        uint32_t idx = PopStackArg(arena, cpu);
        auto tit = tls_slots_.find(idx);
        cpu.eax = (tit != tls_slots_.end()) ? tit->second : 0;
    } else if (fn == "TlsSetValue" || fn == "FlsSetValue") {
        uint32_t idx = PopStackArg(arena, cpu);
        uint32_t val = PopStackArg(arena, cpu);
        tls_slots_[idx] = val;
        cpu.eax = 1;
    } else if (fn == "TlsFree" || fn == "FlsFree") {
        uint32_t idx = PopStackArg(arena, cpu);
        tls_slots_.erase(idx);
        cpu.eax = 1;
    } else if (fn == "InterlockedIncrement") {
        uint32_t ptr = PopStackArg(arena, cpu);
        uint32_t v = arena.ReadU32(ptr) + 1;
        arena.WriteU32(ptr, v);
        cpu.eax = v;
    } else if (fn == "InterlockedDecrement") {
        uint32_t ptr = PopStackArg(arena, cpu);
        uint32_t v = arena.ReadU32(ptr) - 1;
        arena.WriteU32(ptr, v);
        cpu.eax = v;
    } else if (fn == "InterlockedExchange") {
        uint32_t ptr = PopStackArg(arena, cpu);
        uint32_t new_v = PopStackArg(arena, cpu);
        uint32_t old_v = arena.ReadU32(ptr);
        arena.WriteU32(ptr, new_v);
        cpu.eax = old_v;
    } else if (fn == "CreateFileA") {
        uint32_t path_va = PopStackArg(arena, cpu);
        uint32_t access = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        std::string win_path = arena.ReadCString(path_va);
        std::string host_path = NormalizeWindowsPath(win_path);
        const char* mode = (access & 0x40000000u) ? "r+b" : "rb";
        FILE* fp = std::fopen(host_path.c_str(), mode);
        if (!fp && (access & 0x40000000u)) {
            fp = std::fopen(host_path.c_str(), "wb");
        }
        if (fp) {
            uint32_t h = next_file_handle_++;
            open_files_[h] = fp;
            cpu.eax = h;
        } else {
            cpu.eax = 0xFFFFFFFFu;
        }
    } else if (fn == "ReadFile") {
        uint32_t hfile = PopStackArg(arena, cpu);
        uint32_t buf_va = PopStackArg(arena, cpu);
        uint32_t bytes_to_read = PopStackArg(arena, cpu);
        uint32_t bytes_read_ptr = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        auto fit = open_files_.find(hfile);
        uint8_t* dst = arena.GetHostPointer(buf_va);
        if (fit != open_files_.end() && fit->second != nullptr && dst != nullptr &&
            arena.IsValidRange(buf_va, bytes_to_read)) {
            size_t n = std::fread(dst, 1, bytes_to_read, fit->second);
            if (bytes_read_ptr != 0) arena.WriteU32(bytes_read_ptr, static_cast<uint32_t>(n));
            cpu.eax = 1;
        } else {
            if (bytes_read_ptr != 0) arena.WriteU32(bytes_read_ptr, 0);
            cpu.eax = 0;
        }
    } else if (fn == "WriteFile") {
        uint32_t hfile = PopStackArg(arena, cpu);
        uint32_t buf_va = PopStackArg(arena, cpu);
        uint32_t bytes_to_write = PopStackArg(arena, cpu);
        uint32_t bytes_written_ptr = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        auto fit = open_files_.find(hfile);
        const uint8_t* src = arena.GetHostPointer(buf_va);
        if (fit != open_files_.end() && fit->second != nullptr && src != nullptr &&
            arena.IsValidRange(buf_va, bytes_to_write)) {
            size_t n = std::fwrite(src, 1, bytes_to_write, fit->second);
            if (bytes_written_ptr != 0) arena.WriteU32(bytes_written_ptr, static_cast<uint32_t>(n));
            cpu.eax = 1;
        } else {
            if (bytes_written_ptr != 0) arena.WriteU32(bytes_written_ptr, bytes_to_write);
            cpu.eax = 1;
        }
    } else if (fn == "SetFilePointer") {
        uint32_t hfile = PopStackArg(arena, cpu);
        int32_t dist = static_cast<int32_t>(PopStackArg(arena, cpu));
        PopStackArg(arena, cpu);
        uint32_t method = PopStackArg(arena, cpu);
        auto fit = open_files_.find(hfile);
        if (fit != open_files_.end() && fit->second != nullptr) {
            int whence = (method == 1) ? SEEK_CUR : (method == 2 ? SEEK_END : SEEK_SET);
            std::fseek(fit->second, dist, whence);
            cpu.eax = static_cast<uint32_t>(std::ftell(fit->second));
        } else {
            cpu.eax = 0xFFFFFFFFu;
        }
    } else if (fn == "GetFileSize") {
        uint32_t hfile = PopStackArg(arena, cpu);
        uint32_t high_ptr = PopStackArg(arena, cpu);
        auto fit = open_files_.find(hfile);
        if (fit != open_files_.end() && fit->second != nullptr) {
            long cur = std::ftell(fit->second);
            std::fseek(fit->second, 0, SEEK_END);
            long sz = std::ftell(fit->second);
            std::fseek(fit->second, cur, SEEK_SET);
            if (high_ptr != 0) arena.WriteU32(high_ptr, 0);
            cpu.eax = static_cast<uint32_t>(sz);
        } else {
            cpu.eax = 0xFFFFFFFFu;
        }
    } else if (fn == "CloseHandle") {
        uint32_t hfile = PopStackArg(arena, cpu);
        auto fit = open_files_.find(hfile);
        if (fit != open_files_.end()) {
            if (fit->second) std::fclose(fit->second);
            open_files_.erase(fit);
        }
        cpu.eax = 1;
    } else if (fn == "GetTickCount" || fn == "timeGetTime") {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        cpu.eax = static_cast<uint32_t>(ms & 0xFFFFFFFFu);
    } else if (fn == "QueryPerformanceCounter") {
        uint32_t lp_count = PopStackArg(arena, cpu);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        if (lp_count != 0) {
            arena.WriteU32(lp_count, static_cast<uint32_t>(ns & 0xFFFFFFFFu));
            arena.WriteU32(lp_count + 4, static_cast<uint32_t>((ns >> 32) & 0xFFFFFFFFu));
        }
        cpu.eax = 1;
    } else if (fn == "QueryPerformanceFrequency") {
        uint32_t lp_freq = PopStackArg(arena, cpu);
        if (lp_freq != 0) {
            arena.WriteU32(lp_freq, 1000000000u);
            arena.WriteU32(lp_freq + 4, 0u);
        }
        cpu.eax = 1;
    }
    // --- Direct3D 9 & COM Device Methods ---
    else if (fn == "Direct3DCreate9") {
        PopStackArg(arena, cpu);
        cpu.eax = kComIDirect3D9ObjVa;
    } else if (fn == "GetAdapterCount" || fn == "GetAdapterModeCount") {
        cpu.esp = initial_esp + entry.stack_pop_bytes;
        cpu.eax = 1;
    } else if (fn == "EnumAdapterModes" || fn == "GetAdapterDisplayMode") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // Adapter
        if (fn == "EnumAdapterModes") {
            PopStackArg(arena, cpu); // Format
            PopStackArg(arena, cpu); // Mode
        }
        uint32_t p_mode = PopStackArg(arena, cpu);
        if (p_mode != 0 && arena.IsValidRange(p_mode, 16)) {
            arena.WriteU32(p_mode + 0, 1280); // Width
            arena.WriteU32(p_mode + 4, 720);  // Height
            arena.WriteU32(p_mode + 8, 60);   // RefreshRate
            arena.WriteU32(p_mode + 12, 22);  // D3DFMT_X8R8G8B8
        }
        cpu.eax = 0;
    } else if (fn == "CreateDevice" && entry.dll_name == "COM_IDirect3D9") {
        for (int i = 0; i < 6; ++i) PopStackArg(arena, cpu);
        uint32_t pp_device_out = PopStackArg(arena, cpu);
        if (pp_device_out != 0) {
            arena.WriteU32(pp_device_out, kComIDirect3DDevice9ObjVa);
        }
        arena.WriteU32(kGtaSaD3D9DevicePtrVa, kComIDirect3DDevice9ObjVa);
        cpu.eax = 0;
    } else if (fn == "CreateVertexBuffer" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t length = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Usage
        uint32_t fvf = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Pool
        uint32_t pp_vb = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pSharedHandle
        uint32_t vb_va = renderer_ ? renderer_->CreateVertexBuffer(arena, length, fvf, kComID3D9VbVtblVa) : 0;
        if (pp_vb != 0) arena.WriteU32(pp_vb, vb_va);
        cpu.eax = 0;
    } else if (fn == "Lock" && entry.dll_name == "COM_IDirect3DVertexBuffer9") {
        uint32_t vb_this = PopStackArg(arena, cpu);
        uint32_t offset_to_lock = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // SizeToLock
        uint32_t ppb_data = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Flags
        uint32_t base_va = renderer_ ? renderer_->LockVertexBuffer(vb_this) : 0;
        if (ppb_data != 0) arena.WriteU32(ppb_data, base_va + offset_to_lock);
        cpu.eax = 0;
    } else if (fn == "Unlock" && entry.dll_name == "COM_IDirect3DVertexBuffer9") {
        uint32_t vb_this = PopStackArg(arena, cpu);
        if (renderer_) renderer_->UnlockVertexBuffer(vb_this);
        cpu.eax = 0;
    } else if (fn == "CreateIndexBuffer" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t length = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Usage
        uint32_t format = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Pool
        uint32_t pp_ib = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pSharedHandle
        uint32_t ib_va = renderer_ ? renderer_->CreateIndexBuffer(arena, length, format, kComID3D9IbVtblVa) : 0;
        if (pp_ib != 0) arena.WriteU32(pp_ib, ib_va);
        cpu.eax = 0;
    } else if (fn == "Lock" && entry.dll_name == "COM_IDirect3DIndexBuffer9") {
        uint32_t ib_this = PopStackArg(arena, cpu);
        uint32_t offset_to_lock = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // SizeToLock
        uint32_t ppb_data = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Flags
        uint32_t base_va = renderer_ ? renderer_->LockIndexBuffer(ib_this) : 0;
        if (ppb_data != 0) arena.WriteU32(ppb_data, base_va + offset_to_lock);
        cpu.eax = 0;
    } else if (fn == "Unlock" && entry.dll_name == "COM_IDirect3DIndexBuffer9") {
        uint32_t ib_this = PopStackArg(arena, cpu);
        if (renderer_) renderer_->UnlockIndexBuffer(ib_this);
        cpu.eax = 0;
    } else if (fn == "CreateTexture" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t width = PopStackArg(arena, cpu);
        uint32_t height = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Levels
        PopStackArg(arena, cpu); // Usage
        uint32_t format = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Pool
        uint32_t pp_tex = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pSharedHandle
        uint32_t tex_va = renderer_ ? renderer_->CreateTexture(arena, width, height, format, kComID3D9TexVtblVa) : 0;
        if (pp_tex != 0) arena.WriteU32(pp_tex, tex_va);
        cpu.eax = 0;
    } else if (fn == "LockRect" && entry.dll_name == "COM_IDirect3DTexture9") {
        uint32_t tex_this = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Level
        uint32_t p_locked_rect = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pRect
        PopStackArg(arena, cpu); // Flags
        uint32_t pitch = 0, bits_va = 0;
        if (renderer_ && renderer_->LockTextureRect(tex_this, &pitch, &bits_va) && p_locked_rect != 0) {
            arena.WriteU32(p_locked_rect + 0, pitch);
            arena.WriteU32(p_locked_rect + 4, bits_va);
        }
        cpu.eax = 0;
    } else if (fn == "UnlockRect" && entry.dll_name == "COM_IDirect3DTexture9") {
        uint32_t tex_this = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Level
        if (renderer_) renderer_->UnlockTextureRect(arena, tex_this);
        cpu.eax = 0;
    } else if (fn == "SetTransform" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t state = PopStackArg(arena, cpu);
        uint32_t mat_va = PopStackArg(arena, cpu);
        if (renderer_ && mat_va != 0 && arena.IsValidRange(mat_va, 64)) {
            const float* m = reinterpret_cast<const float*>(arena.GetHostPointer(mat_va));
            renderer_->SetTransform(state, m);
        }
        cpu.eax = 0;
    } else if (fn == "SetRenderState" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t state = PopStackArg(arena, cpu);
        uint32_t val = PopStackArg(arena, cpu);
        if (renderer_) renderer_->SetRenderState(state, val);
        cpu.eax = 0;
    } else if (fn == "SetTexture" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t stage = PopStackArg(arena, cpu);
        uint32_t tex_va = PopStackArg(arena, cpu);
        if (renderer_) renderer_->SetTexture(stage, tex_va);
        cpu.eax = 0;
    } else if (fn == "SetFVF" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t fvf = PopStackArg(arena, cpu);
        if (renderer_) renderer_->SetFVF(fvf);
        cpu.eax = 0;
    } else if (fn == "SetStreamSource" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t stream_num = PopStackArg(arena, cpu);
        uint32_t vb_va = PopStackArg(arena, cpu);
        uint32_t offset = PopStackArg(arena, cpu);
        uint32_t stride = PopStackArg(arena, cpu);
        if (renderer_) renderer_->SetStreamSource(stream_num, vb_va, offset, stride);
        cpu.eax = 0;
    } else if (fn == "SetIndices" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t ib_va = PopStackArg(arena, cpu);
        if (renderer_) renderer_->SetIndices(ib_va);
        cpu.eax = 0;
    } else if (fn == "Clear" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // Count
        PopStackArg(arena, cpu); // pRects
        PopStackArg(arena, cpu); // Flags
        uint32_t color = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Z
        PopStackArg(arena, cpu); // Stencil
        if (renderer_) renderer_->OnD3D9Clear(color);
        cpu.eax = 0;
    } else if (fn == "BeginScene") {
        PopStackArg(arena, cpu);
        if (renderer_) renderer_->OnD3D9BeginScene();
        cpu.eax = 0;
    } else if (fn == "EndScene") {
        PopStackArg(arena, cpu);
        if (renderer_) renderer_->OnD3D9EndScene();
        cpu.eax = 0;
    } else if (fn == "Present") {
        for (int i = 0; i < 5; ++i) PopStackArg(arena, cpu);
        if (renderer_) renderer_->OnD3D9Present();
        cpu.eax = 0;
    } else if (fn == "DrawPrimitive" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t prim_type = PopStackArg(arena, cpu);
        uint32_t start_vert = PopStackArg(arena, cpu);
        uint32_t prim_count = PopStackArg(arena, cpu);
        if (renderer_) renderer_->DrawPrimitive(arena, prim_type, start_vert, prim_count);
        cpu.eax = 0;
    } else if (fn == "DrawIndexedPrimitive" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t prim_type = PopStackArg(arena, cpu);
        int32_t base_v = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t min_v = PopStackArg(arena, cpu);
        uint32_t num_v = PopStackArg(arena, cpu);
        uint32_t start_idx = PopStackArg(arena, cpu);
        uint32_t prim_count = PopStackArg(arena, cpu);
        if (renderer_) renderer_->DrawIndexedPrimitive(arena, prim_type, base_v, min_v, num_v, start_idx, prim_count);
        cpu.eax = 0;
    } else if (fn == "DrawPrimitiveUP" && entry.dll_name == "COM_IDirect3DDevice9") {
        PopStackArg(arena, cpu); // this
        uint32_t prim_type = PopStackArg(arena, cpu);
        uint32_t prim_count = PopStackArg(arena, cpu);
        uint32_t verts_va = PopStackArg(arena, cpu);
        uint32_t stride = PopStackArg(arena, cpu);
        if (renderer_) renderer_->DrawPrimitiveUP(arena, prim_type, prim_count, verts_va, stride);
        cpu.eax = 0;
    } else if (fn == "D3DXCreateFontA") {
        for (int i = 0; i < 11; ++i) PopStackArg(arena, cpu);
        uint32_t pp_font = PopStackArg(arena, cpu);
        if (pp_font != 0) arena.WriteU32(pp_font, kComID3DXFontObjVa);
        cpu.eax = 0;
    } else if (fn == "DrawTextA" && entry.dll_name == "COM_ID3DXFont") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // pSprite
        uint32_t str_va = PopStackArg(arena, cpu);
        int32_t count = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t rect_va = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // Format
        uint32_t color = PopStackArg(arena, cpu);
        std::string text = arena.ReadCString(str_va, count > 0 ? static_cast<size_t>(count) : 256u);
        int32_t x = 32, y = 32;
        if (rect_va != 0 && arena.IsValidRange(rect_va, 16)) {
            x = static_cast<int32_t>(arena.ReadU32(rect_va + 0));
            y = static_cast<int32_t>(arena.ReadU32(rect_va + 4));
        }
        if (renderer_) renderer_->SubmitD3DXFontText(x, y, color, text);
        cpu.eax = 18;
    } else if (fn == "D3DXCreateSprite") {
        PopStackArg(arena, cpu);
        uint32_t pp_sprite = PopStackArg(arena, cpu);
        if (pp_sprite != 0) arena.WriteU32(pp_sprite, kComID3DXSpriteObjVa);
        cpu.eax = 0;
    }
    // --- DirectInput 8 & DirectSound 8 ---
    else if (fn == "DirectInput8Create") {
        for (int i = 0; i < 3; ++i) PopStackArg(arena, cpu);
        uint32_t ppv_out = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        if (ppv_out != 0) arena.WriteU32(ppv_out, kComIDirectInput8ObjVa);
        cpu.eax = 0;
    } else if (fn == "CreateDevice" && entry.dll_name == "COM_IDirectInput8") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // rguid
        uint32_t lplp_dev = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pUnkOuter
        if (lplp_dev != 0) arena.WriteU32(lplp_dev, kComIDirectInputDevObjVa);
        cpu.eax = 0;
    } else if (fn == "GetDeviceState" && entry.dll_name == "COM_IDirectInputDevice8") {
        PopStackArg(arena, cpu); // this
        uint32_t cb_data = PopStackArg(arena, cpu);
        uint32_t lpv_data = PopStackArg(arena, cpu);
        std::lock_guard<std::mutex> lock(input_mutex_);
        if (lpv_data != 0 && cb_data <= 256 && arena.IsValidRange(lpv_data, cb_data)) {
            arena.WriteBytes(lpv_data, key_state_, cb_data);
        }
        cpu.eax = 0;
    } else if (fn == "DirectSoundCreate8" || fn == "EAXDirectSoundCreate" || fn == "EAXDirectSoundCreate8") {
        PopStackArg(arena, cpu); // pcGuidDevice
        uint32_t pp_ds8 = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pUnkOuter
        if (pp_ds8 != 0) arena.WriteU32(pp_ds8, kComIDirectSound8ObjVa);
        cpu.eax = 0;
    } else if (fn == "CreateSoundBuffer" && entry.dll_name == "COM_IDirectSound8") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // pcDSBufferDesc
        uint32_t pp_dsbuf = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // pUnkOuter
        if (pp_dsbuf != 0) arena.WriteU32(pp_dsbuf, kComIDirectSoundBufObjVa);
        cpu.eax = 0;
    } else if (fn == "Lock" && entry.dll_name == "COM_IDirectSoundBuffer8") {
        PopStackArg(arena, cpu); // this
        PopStackArg(arena, cpu); // dwOffset
        uint32_t dw_bytes = PopStackArg(arena, cpu);
        uint32_t ppv_audio1 = PopStackArg(arena, cpu);
        uint32_t pdw_bytes1 = PopStackArg(arena, cpu);
        uint32_t ppv_audio2 = PopStackArg(arena, cpu);
        uint32_t pdw_bytes2 = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // dwFlags
        uint32_t audio_va = arena.HeapAllocSim(std::max(64u, dw_bytes), false);
        if (ppv_audio1 != 0) arena.WriteU32(ppv_audio1, audio_va);
        if (pdw_bytes1 != 0) arena.WriteU32(pdw_bytes1, dw_bytes);
        if (ppv_audio2 != 0) arena.WriteU32(ppv_audio2, 0);
        if (pdw_bytes2 != 0) arena.WriteU32(pdw_bytes2, 0);
        cpu.eax = 0;
    } else if (fn == "Unlock" && entry.dll_name == "COM_IDirectSoundBuffer8") {
        for (int i = 0; i < 5; ++i) PopStackArg(arena, cpu);
        cpu.eax = 0;
    }
    // --- Windowing & Message Loop ---
    else if (fn == "CreateWindowExA") {
        for (int i = 0; i < 12; ++i) PopStackArg(arena, cpu);
        cpu.eax = 0x00010020u;
    } else if (fn == "GetClientRect" || fn == "GetWindowRect") {
        PopStackArg(arena, cpu); // hWnd
        uint32_t rect_va = PopStackArg(arena, cpu);
        if (rect_va != 0 && arena.IsValidRange(rect_va, 16)) {
            arena.WriteU32(rect_va + 0, 0);
            arena.WriteU32(rect_va + 4, 0);
            arena.WriteU32(rect_va + 8, 1280);
            arena.WriteU32(rect_va + 12, 720);
        }
        cpu.eax = 1;
    } else if (fn == "SetWindowLongA") {
        PopStackArg(arena, cpu); // hWnd
        int32_t idx = static_cast<int32_t>(PopStackArg(arena, cpu));
        uint32_t new_long = PopStackArg(arena, cpu);
        uint32_t prev = hooked_wndproc_va_;
        if (idx == -4) {
            hooked_wndproc_va_ = new_long;
        }
        cpu.eax = prev;
    } else if (fn == "PeekMessageA") {
        uint32_t lp_msg = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        uint32_t remove_msg = PopStackArg(arena, cpu);

        std::lock_guard<std::mutex> lock(input_mutex_);
        if (!message_queue_.empty()) {
            Win32Msg msg = message_queue_.front();
            if (remove_msg & 1u) {
                message_queue_.pop_front();
            }
            if (lp_msg != 0 && arena.IsValidRange(lp_msg, sizeof(Win32Msg))) {
                arena.WriteBytes(lp_msg, reinterpret_cast<const uint8_t*>(&msg), sizeof(Win32Msg));
            }
            cpu.eax = 1;
        } else {
            cpu.eax = 0;
        }
    } else if (fn == "GetAsyncKeyState" || fn == "GetKeyState") {
        uint32_t vkey = PopStackArg(arena, cpu) & 0xFFu;
        std::lock_guard<std::mutex> lock(input_mutex_);
        cpu.eax = (key_state_[vkey] != 0) ? 0x8000u : 0u;
    } else if (fn == "RegQueryValueExA") {
        PopStackArg(arena, cpu);
        uint32_t val_name_va = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        PopStackArg(arena, cpu);
        uint32_t data_va = PopStackArg(arena, cpu);
        uint32_t cb_data_va = PopStackArg(arena, cpu);
        std::string val_name = arena.ReadCString(val_name_va);
        if (val_name == "PlayerName" && data_va != 0) {
            arena.WriteCString(data_va, player_nickname_);
            if (cb_data_va != 0) {
                arena.WriteU32(cb_data_va, static_cast<uint32_t>(player_nickname_.size() + 1));
            }
        }
        cpu.eax = 0;
    }
    // --- Real WS2_32.dll POSIX Socket Bridge for SA-MP RakNet ---
    else if (fn == "WSAStartup") {
        PopStackArg(arena, cpu); // wVersionRequested
        uint32_t lp_wsa_data = PopStackArg(arena, cpu);
        if (lp_wsa_data != 0 && arena.IsValidRange(lp_wsa_data, 32)) {
            arena.WriteU16(lp_wsa_data + 0, 0x0202);
            arena.WriteU16(lp_wsa_data + 2, 0x0202);
        }
        cpu.eax = 0;
    } else if (fn == "WSACleanup") {
        cpu.eax = 0;
    } else if (fn == "WSAGetLastError") {
        cpu.eax = last_wsa_error_;
    } else if (fn == "socket") {
        PopStackArg(arena, cpu); // af
        int type = static_cast<int>(PopStackArg(arena, cpu));
        PopStackArg(arena, cpu); // protocol
        int fd = ::socket(AF_INET, (type == 2) ? SOCK_DGRAM : SOCK_STREAM, 0);
        if (fd >= 0) {
            uint32_t sh = next_socket_handle_++;
            open_sockets_[sh] = fd;
            cpu.eax = sh;
        } else {
            cpu.eax = 0xFFFFFFFFu;
        }
    } else if (fn == "closesocket") {
        uint32_t sh = PopStackArg(arena, cpu);
        auto sit = open_sockets_.find(sh);
        if (sit != open_sockets_.end()) {
            if (sit->second >= 0) ::close(sit->second);
            open_sockets_.erase(sit);
        }
        cpu.eax = 0;
    } else if (fn == "ioctlsocket") {
        uint32_t sh = PopStackArg(arena, cpu);
        uint32_t cmd = PopStackArg(arena, cpu);
        uint32_t argp_va = PopStackArg(arena, cpu);
        auto sit = open_sockets_.find(sh);
        if (sit != open_sockets_.end() && cmd == 0x8004667Eu) { // FIONBIO
            uint32_t nonblock = (argp_va != 0) ? arena.ReadU32(argp_va) : 1;
            int flags = ::fcntl(sit->second, F_GETFL, 0);
            if (flags >= 0) {
                ::fcntl(sit->second, F_SETFL, nonblock ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
            }
        }
        cpu.eax = 0;
    } else if (fn == "bind") {
        uint32_t sh = PopStackArg(arena, cpu);
        uint32_t addr_va = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // namelen
        auto sit = open_sockets_.find(sh);
        if (sit != open_sockets_.end() && addr_va != 0 && arena.IsValidRange(addr_va, 16)) {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = arena.ReadU16(addr_va + 2);
            sin.sin_addr.s_addr = arena.ReadU32(addr_va + 4);
            ::bind(sit->second, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        }
        cpu.eax = 0;
    } else if (fn == "sendto") {
        uint32_t sh = PopStackArg(arena, cpu);
        uint32_t buf_va = PopStackArg(arena, cpu);
        uint32_t len = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // flags
        uint32_t to_va = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // tolen
        auto sit = open_sockets_.find(sh);
        const uint8_t* data = arena.GetHostPointer(buf_va);
        if (sit != open_sockets_.end() && data && to_va != 0 && arena.IsValidRange(to_va, 8)) {
            sockaddr_in sin{};
            sin.sin_family = AF_INET;
            sin.sin_port = arena.ReadU16(to_va + 2);
            sin.sin_addr.s_addr = arena.ReadU32(to_va + 4);
            ssize_t sent = ::sendto(sit->second, data, len, 0, reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
            cpu.eax = (sent >= 0) ? static_cast<uint32_t>(sent) : 0xFFFFFFFFu;
        } else {
            cpu.eax = len;
        }
    } else if (fn == "recvfrom") {
        uint32_t sh = PopStackArg(arena, cpu);
        uint32_t buf_va = PopStackArg(arena, cpu);
        uint32_t len = PopStackArg(arena, cpu);
        PopStackArg(arena, cpu); // flags
        uint32_t from_va = PopStackArg(arena, cpu);
        uint32_t fromlen_va = PopStackArg(arena, cpu);
        auto sit = open_sockets_.find(sh);
        uint8_t* dst = arena.GetHostPointer(buf_va);
        if (sit != open_sockets_.end() && dst && arena.IsValidRange(buf_va, len)) {
            sockaddr_in sin{};
            socklen_t slen = sizeof(sin);
            ssize_t rcv = ::recvfrom(sit->second, dst, len, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&sin), &slen);
            if (rcv >= 0) {
                if (from_va != 0 && arena.IsValidRange(from_va, 16)) {
                    arena.WriteU16(from_va + 0, AF_INET);
                    arena.WriteU16(from_va + 2, sin.sin_port);
                    arena.WriteU32(from_va + 4, sin.sin_addr.s_addr);
                }
                if (fromlen_va != 0) arena.WriteU32(fromlen_va, 16);
                last_wsa_error_ = 0;
                cpu.eax = static_cast<uint32_t>(rcv);
            } else {
                last_wsa_error_ = 10035; // WSAEWOULDBLOCK
                cpu.eax = 0xFFFFFFFFu;
            }
        } else {
            last_wsa_error_ = 10035;
            cpu.eax = 0xFFFFFFFFu;
        }
    } else if (fn == "inet_addr") {
        uint32_t cp_va = PopStackArg(arena, cpu);
        std::string ip_str = arena.ReadCString(cp_va);
        cpu.eax = static_cast<uint32_t>(::inet_addr(ip_str.c_str()));
    } else if (fn == "htons" || fn == "ntohs") {
        uint32_t v = PopStackArg(arena, cpu) & 0xFFFFu;
        cpu.eax = htons(static_cast<uint16_t>(v));
    } else if (fn == "htonl" || fn == "ntohl") {
        uint32_t v = PopStackArg(arena, cpu);
        cpu.eax = htonl(v);
    } else {
        cpu.eax = 1;
    }

    // Strict __stdcall stack discipline: always advance initial_esp by entry.stack_pop_bytes
    if (entry.dll_name != "vorbisfile.dll") {
        cpu.esp = initial_esp + entry.stack_pop_bytes;
    }
    cpu.eip = return_eip;
    return true;
}

}  // namespace ventura
