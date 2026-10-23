#pragma once

#include "ventura_types.h"
#include "../core/memory_arena.h"
#include "d3d9_surface_renderer.h"
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ventura {

struct Win32ThunkEntry {
    uint32_t thunk_va = 0;
    std::string dll_name;
    std::string function_name;
    uint32_t stack_pop_bytes = 0;
    uint64_t invocation_count = 0;
};

class Win32Bridge {
public:
    Win32Bridge();
    ~Win32Bridge();

    void Initialize(
        MemoryArena& arena,
        D3D9SurfaceRenderer* renderer,
        const std::string& game_root_dir,
        const std::string& player_nickname,
        const std::string& command_line
    );

    void Shutdown();

    uint32_t ResolveProcAddress(const std::string& dll_name, const std::string& func_name);
    bool IsThunkAddress(uint32_t va) const;
    bool DispatchThunkCall(uint32_t thunk_va, MemoryArena& arena, CpuState32& cpu);

    void EnqueueKeyEvent(uint32_t virtual_key, bool is_down);
    void EnqueueCharEvent(uint32_t char_code);
    void EnqueueMouseEvent(int32_t x, int32_t y, bool left_down);

    uint32_t GetHookedWndProcVa() const { return hooked_wndproc_va_; }
    uint64_t GetTotalSyscalls() const { return total_syscalls_; }

private:
    uint32_t RegisterThunk(const std::string& dll, const std::string& fn, uint32_t stack_pop_bytes = 0);
    void SetupComVtables(MemoryArena& arena);
    std::string NormalizeWindowsPath(const std::string& win_path) const;

    D3D9SurfaceRenderer* renderer_ = nullptr;
    std::string game_root_dir_;
    std::string player_nickname_;
    std::vector<Win32ThunkEntry> thunks_;
    std::unordered_map<uint32_t, size_t> va_to_index_;
    std::unordered_map<uint32_t, FILE*> open_files_;
    std::unordered_map<uint32_t, int> open_sockets_;
    std::unordered_map<uint32_t, uint32_t> tls_slots_;
    uint32_t next_file_handle_ = 0x00001000;
    uint32_t next_socket_handle_ = 0x00002000;
    uint32_t next_tls_index_ = 1;
    uint32_t last_win32_error_ = 0;
    uint32_t last_wsa_error_ = 0;
    uint32_t next_thunk_va_ = kWin32ThunkTableBaseVa;
    uint32_t hooked_wndproc_va_ = 0;
    uint64_t total_syscalls_ = 0;

    std::deque<Win32Msg> message_queue_;
    uint8_t key_state_[256] = {0};
    int32_t mouse_x_ = 640;
    int32_t mouse_y_ = 360;
    bool mouse_left_down_ = false;
    mutable std::mutex input_mutex_;
};

}  // namespace ventura
