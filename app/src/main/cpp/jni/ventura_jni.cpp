#include <jni.h>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "../include/ventura_types.h"
#include "../core/memory_arena.h"
#include "../loader/pe32_loader.h"
#include "../aot/x86_aot_engine.h"
#include "../bridge/win32_bridge.h"
#include "../bridge/d3d9_surface_renderer.h"
#include "../bridge/winsock_samp_net.h"
#include "../archive/img_ver2_reader.h"

#ifdef __ANDROID__
#include <android/native_window_jni.h>
#endif

namespace {
ventura::MemoryArena g_arena;
ventura::D3D9SurfaceRenderer g_d3d9_renderer;
ventura::Win32Bridge g_win32_bridge;
ventura::CpuState32 g_cpu_state;
std::atomic<bool> g_process_running{false};
std::thread g_execution_thread;

std::mutex g_session_meta_mutex;
std::string g_session_mode_label = "SINGLEPLAYER (GTA_SA.EXE)";
std::string g_session_module_label = "GTA_SA.EXE @ 0x00400000";
std::string g_session_input_label = "INPUT READY (TOUCH / WASD / F6 CHAT)";
bool g_binary_loaded_ok = false;

void ExecutionWorkerLoop() {
    while (g_process_running.load()) {
        if (g_binary_loaded_ok && !g_cpu_state.halted) {
            ventura::X86AotEngine::ExecuteSlice(g_arena, g_win32_bridge, g_cpu_state, 32768);
        }

        ventura::D3D9FrameTelemetry tel = g_d3d9_renderer.GetTelemetry();
        std::string mode_str;
        std::string mod_str;
        std::string inp_str;
        {
            std::lock_guard<std::mutex> lock(g_session_meta_mutex);
            mode_str = g_session_mode_label;
            mod_str = g_session_module_label;
            inp_str = g_session_input_label;
        }

        std::ostringstream line1;
        line1 << "VENTURA ARM64 ENGINE | " << mode_str;

        std::ostringstream line2;
        line2 << mod_str
              << " | EIP: 0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << g_cpu_state.eip
              << " | ESP: 0x" << std::setw(8) << g_cpu_state.esp << std::dec;

        std::ostringstream line3;
        line3 << "RETIRED INST: " << g_cpu_state.retired_instructions
              << " | WIN32/D3D9 SYSCALLS: " << g_win32_bridge.GetTotalSyscalls()
              << " | FRAMES: " << tel.frame_index
              << " | D3D9 DRAWS: " << tel.draw_calls;

        std::vector<std::string> hud_lines = {
            line1.str(),
            line2.str(),
            line3.str(),
            inp_str
        };
        g_d3d9_renderer.SetHudStatusLines(hud_lines);
        g_d3d9_renderer.OnD3D9Present();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Release EGLSurface & EGLContext on the worker thread that owns them
    g_d3d9_renderer.ShutdownOnWorkerThread();
}
}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeInitArena(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jint /*base_va*/,
    jint /*arena_size*/
) {
    bool ok = g_arena.Initialize();
    if (ok) {
        g_win32_bridge.Initialize(g_arena, &g_d3d9_renderer, "", "Player", "gta_sa.exe");
        g_cpu_state = ventura::CpuState32{};
    }
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeLaunchGameProcess(
    JNIEnv* env,
    jobject /*thiz*/,
    jstring game_dir_jstr,
    jboolean multiplayer_samp,
    jstring server_ip_jstr,
    jint server_port,
    jstring nickname_jstr,
    jstring password_jstr
) {
    g_process_running.store(false);
    if (g_execution_thread.joinable()) {
        g_execution_thread.join();
    }

    auto get_utf = [&](jstring jstr) -> std::string {
        if (!jstr) return "";
        const char* cstr = env->GetStringUTFChars(jstr, nullptr);
        std::string out(cstr ? cstr : "");
        if (cstr) env->ReleaseStringUTFChars(jstr, cstr);
        return out;
    };

    std::string game_dir = get_utf(game_dir_jstr);
    std::string ip = get_utf(server_ip_jstr);
    std::string nick = get_utf(nickname_jstr);
    std::string pass = get_utf(password_jstr);

    std::ostringstream cmd;
    cmd << "gta_sa.exe";
    if (multiplayer_samp == JNI_TRUE) {
        cmd << " -c -n " << nick << " -h " << ip << " -p " << server_port;
        if (!pass.empty()) {
            cmd << " -z " << pass;
        }
    }

    if (!g_arena.Initialize()) {
        return JNI_FALSE;
    }
    g_win32_bridge.Initialize(g_arena, &g_d3d9_renderer, game_dir, nick, cmd.str());
    g_cpu_state = ventura::CpuState32{};

    std::string exe_path = game_dir + "/gta_sa.exe";
    ventura::PeImageSummary gta_summary;
    bool gta_ok = ventura::Pe32Loader::LoadModuleIntoProcess(
        exe_path,
        ventura::kGtaSaImageBase,
        g_arena,
        g_win32_bridge,
        &gta_summary
    );

    g_binary_loaded_ok = gta_ok;

    std::ostringstream mod_info;
    if (gta_ok) {
        mod_info << "GTA_SA.EXE (" << (gta_summary.file_size / 1024) << " KB, EP 0x"
                 << std::uppercase << std::hex << gta_summary.entry_point_va << std::dec << ")";
        g_cpu_state.eip = gta_summary.entry_point_va;
    } else {
        mod_info << "WAITING FOR GTA_SA.EXE IN RUNTIME DIR";
    }

    if (gta_ok && multiplayer_samp == JNI_TRUE) {
        std::string samp_path = game_dir + "/samp.dll";
        ventura::PeImageSummary samp_summary;
        if (ventura::Pe32Loader::LoadModuleIntoProcess(
                samp_path,
                ventura::kSampModuleBaseVa,
                g_arena,
                g_win32_bridge,
                &samp_summary
            ) && samp_summary.entry_point_va != 0) {
            g_cpu_state.esp -= 16;
            g_arena.WriteU32(g_cpu_state.esp + 0, gta_summary.entry_point_va);
            g_arena.WriteU32(g_cpu_state.esp + 4, ventura::kSampModuleBaseVa);
            g_arena.WriteU32(g_cpu_state.esp + 8, 1u); // DLL_PROCESS_ATTACH
            g_arena.WriteU32(g_cpu_state.esp + 12, 0u);
            g_cpu_state.eip = samp_summary.entry_point_va;
            mod_info << " + SAMP.DLL (0x10000000)";
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_session_meta_mutex);
        if (multiplayer_samp == JNI_TRUE) {
            std::ostringstream m;
            m << "SA-MP 0.3.7 | " << nick << " @ " << ip << ":" << server_port;
            g_session_mode_label = m.str();
        } else {
            g_session_mode_label = "SINGLEPLAYER | GTA SAN ANDREAS v1.0 US";
        }
        g_session_module_label = mod_info.str();
        g_session_input_label = "INPUT READY | TOUCH / WASD / F6 CHAT";
    }

    g_process_running.store(true);
    g_execution_thread = std::thread(ExecutionWorkerLoop);
    return gta_ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeStopGameProcess(
    JNIEnv* /*env*/,
    jobject /*thiz*/
) {
    g_process_running.store(false);
    if (g_execution_thread.joinable()) {
        g_execution_thread.join();
    }
}

JNIEXPORT void JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeSendKeyEvent(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jint virtual_key,
    jboolean is_down
) {
    g_win32_bridge.EnqueueKeyEvent(static_cast<uint32_t>(virtual_key), is_down == JNI_TRUE);
    if (is_down == JNI_TRUE) {
        std::lock_guard<std::mutex> lock(g_session_meta_mutex);
        std::ostringstream oss;
        oss << "LAST INPUT: VK 0x" << std::uppercase << std::hex << virtual_key << std::dec << " DOWN";
        g_session_input_label = oss.str();
    }
}

JNIEXPORT void JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeSendCharEvent(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jint char_code
) {
    g_win32_bridge.EnqueueCharEvent(static_cast<uint32_t>(char_code));
    std::lock_guard<std::mutex> lock(g_session_meta_mutex);
    std::ostringstream oss;
    oss << "WM_CHAR SENT: '" << static_cast<char>(char_code & 0x7F) << "' (0x"
        << std::uppercase << std::hex << char_code << ")";
    g_session_input_label = oss.str();
}

JNIEXPORT void JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeSendPointerEvent(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jint x,
    jint y,
    jboolean pressed
) {
    g_win32_bridge.EnqueueMouseEvent(x, y, pressed == JNI_TRUE);
}

JNIEXPORT jstring JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeInspectPe32(
    JNIEnv* env,
    jobject /*thiz*/,
    jstring file_path_jstr
) {
    const char* path_cstr = env->GetStringUTFChars(file_path_jstr, nullptr);
    std::string path(path_cstr ? path_cstr : "");
    if (path_cstr) env->ReleaseStringUTFChars(file_path_jstr, path_cstr);

    ventura::PeImageSummary summary = ventura::Pe32Loader::InspectBinaryFile(path);
    std::ostringstream oss;
    oss << (summary.valid ? "VALID" : "INVALID") << "|"
        << summary.file_size << "|"
        << summary.machine << "|"
        << summary.image_base << "|"
        << summary.entry_point_rva << "|"
        << summary.entry_point_va << "|"
        << summary.size_of_image << "|"
        << summary.checksum << "|"
        << summary.sections.size() << "|"
        << summary.imports.size() << "|"
        << summary.error_message;
    return env->NewStringUTF(oss.str().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeQuerySampServer(
    JNIEnv* env,
    jobject /*thiz*/,
    jstring host_jstr,
    jint port
) {
    const char* host_cstr = env->GetStringUTFChars(host_jstr, nullptr);
    std::string host(host_cstr ? host_cstr : "127.0.0.1");
    if (host_cstr) env->ReleaseStringUTFChars(host_jstr, host_cstr);

    ventura::SampServerQueryResult res = ventura::WinsockSampNet::QueryServerUdp(
        host,
        static_cast<uint16_t>(port),
        2000
    );

    std::ostringstream oss;
    oss << (res.reachable ? "ONLINE" : "OFFLINE") << "|"
        << res.ip << "|"
        << res.port << "|"
        << res.active_players << "|"
        << res.max_players << "|"
        << res.rtt_ping_ms << "|"
        << res.hostname << "|"
        << res.gamemode << "|"
        << res.language << "|"
        << res.error_detail;
    return env->NewStringUTF(oss.str().c_str());
}

JNIEXPORT void JNICALL
Java_com_example_engine_jni_VenturaNativeBridge_nativeAttachSurface(
    JNIEnv* env,
    jobject /*thiz*/,
    jobject surface
) {
#ifdef __ANDROID__
    if (surface != nullptr) {
        ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
        g_d3d9_renderer.AttachWindow(win);
        if (win != nullptr) {
            ANativeWindow_release(win);
        }
    } else {
        g_d3d9_renderer.DetachWindow();
    }
#else
    (void)env;
    (void)surface;
#endif
}

}  // extern "C"
