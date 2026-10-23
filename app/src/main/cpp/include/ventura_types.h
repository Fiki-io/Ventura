#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace ventura {

constexpr uint16_t kPeDosSignature = 0x5A4D;        // "MZ"
constexpr uint32_t kPeNtSignature = 0x00004550;     // "PE\0\0"
constexpr uint16_t kPeMachineI386 = 0x014C;         // Intel 386
constexpr uint16_t kPeOptionalHdr32Magic = 0x010B;  // PE32

// GTA San Andreas v1.0 US & SA-MP Virtual Memory Layout
constexpr uint32_t kGtaSaImageBase = 0x00400000;
constexpr uint32_t kGtaSaEntryPointRva = 0x00424570;
constexpr uint32_t kGtaSaEntryPointVa = 0x00824570;
constexpr uint32_t kGtaSaInitialiseGameVa = 0x00748CFB;
constexpr uint32_t kGtaSaCGameProcessVa = 0x0053EB0D;
constexpr uint32_t kGtaSaRenderLoopVa = 0x0053E960;
constexpr uint32_t kGtaSaD3D9DevicePtrVa = 0x00C97C28;
constexpr uint32_t kGtaSaDirectInput8PtrVa = 0x00C8CFC0;
constexpr uint32_t kGtaSaHwndVa = 0x00C97C1C;
constexpr uint32_t kGtaSaFrameCounterVa = 0x00B7CB84;

constexpr uint32_t kSampModuleBaseVa = 0x10000000;
constexpr uint32_t kAsiModuleBaseVa = 0x18000000;
constexpr uint32_t kDynamicHeapBaseVa = 0x20000000;
constexpr uint32_t kHeapArenaBaseVa = kDynamicHeapBaseVa;
constexpr uint32_t kComVtableArenaBaseVa = 0x60000000;
constexpr uint32_t kWin32ThunkTableBaseVa = 0x70000000;
constexpr uint32_t kTebBaseVa = 0x7FFDF000;
constexpr uint32_t kPebBaseVa = 0x7FFDE000;
constexpr uint32_t kCommandLineVa = 0x7FFDD000;

constexpr uint32_t kImgSectorSize = 2048;

// Windows Message & Virtual Key Constants for WndProc & DirectInput8
constexpr uint32_t kWmDestroy = 0x0002;
constexpr uint32_t kWmSize = 0x0005;
constexpr uint32_t kWmSetFocus = 0x0007;
constexpr uint32_t kWmPaint = 0x000F;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmKeyDown = 0x0100;
constexpr uint32_t kWmKeyUp = 0x0101;
constexpr uint32_t kWmChar = 0x0102;
constexpr uint32_t kWmMouseMove = 0x0200;
constexpr uint32_t kWmLButtonDown = 0x0201;
constexpr uint32_t kWmLButtonUp = 0x0202;
constexpr uint32_t kWmRButtonDown = 0x0204;
constexpr uint32_t kWmRButtonUp = 0x0205;

constexpr uint32_t kVkTab = 0x09;
constexpr uint32_t kVkReturn = 0x0D;
constexpr uint32_t kVkShift = 0x10;
constexpr uint32_t kVkEscape = 0x1B;
constexpr uint32_t kVkSpace = 0x20;
constexpr uint32_t kVkF6 = 0x75;

#pragma pack(push, 1)
struct PeDosHeader {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t e_lfanew;
};

struct PeFileHeader {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
};

struct PeDataDirectory {
    uint32_t virtual_address;
    uint32_t size;
};

struct PeOptionalHeader32 {
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint32_t base_of_data;
    uint32_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_version;
    uint16_t minor_os_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t check_sum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t size_of_stack_reserve;
    uint32_t size_of_stack_commit;
    uint32_t size_of_heap_reserve;
    uint32_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    PeDataDirectory data_directory[16];
};

struct PeSectionHeader {
    char name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
};

struct PeImportDescriptor {
    uint32_t original_first_thunk;
    uint32_t time_date_stamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;
    uint32_t first_thunk;
};

struct PeBaseRelocationBlock {
    uint32_t page_rva;
    uint32_t block_size;
};

struct Win32Msg {
    uint32_t hwnd;
    uint32_t message;
    uint32_t wparam;
    uint32_t lparam;
    uint32_t time;
    int32_t pt_x;
    int32_t pt_y;
};

struct ImgVer2Header {
    char magic[4];  // "VER2"
    uint32_t entry_count;
};

struct ImgVer2DirectoryEntry {
    uint32_t sector_offset;
    uint16_t streaming_size_sectors;
    uint16_t archive_size_sectors;
    char file_name[24];
};
#pragma pack(pop)

struct XmmRegister {
    float f32[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Complete x86-32 + x87 FPU + SSE + FS Segment CPU Context
struct CpuState32 {
    uint32_t eax = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    uint32_t ebx = 0;
    uint32_t esp = 0x01BF0000;
    uint32_t ebp = 0x01BF0000;
    uint32_t esi = 0;
    uint32_t edi = 0;
    uint32_t eip = kGtaSaEntryPointVa;
    uint32_t eflags = 0x00000202;
    uint32_t fs_base = kTebBaseVa;

    // x87 80-bit/64-bit FPU Register Stack
    double fpu_st[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    int32_t fpu_top = 0;
    uint16_t fpu_status_word = 0;
    uint16_t fpu_control_word = 0x037F;

    // SSE XMM0-XMM7 Registers
    XmmRegister xmm[8];

    uint64_t retired_instructions = 0;
    bool halted = false;
};

struct PeSectionInfo {
    std::string name;
    uint32_t virtual_address = 0;
    uint32_t virtual_size = 0;
    uint32_t raw_offset = 0;
    uint32_t raw_size = 0;
    uint32_t characteristics = 0;
};

struct PeImportModule {
    std::string dll_name;
    std::vector<std::string> functions;
    uint32_t iat_rva = 0;
};

struct PeImageSummary {
    bool valid = false;
    std::string file_path;
    uint64_t file_size = 0;
    uint16_t machine = 0;
    uint32_t image_base = 0;
    uint32_t entry_point_rva = 0;
    uint32_t entry_point_va = 0;
    uint32_t size_of_image = 0;
    uint32_t reloc_rva = 0;
    uint32_t reloc_size = 0;
    uint32_t checksum = 0;
    std::vector<PeSectionInfo> sections;
    std::vector<PeImportModule> imports;
    std::string error_message;
};

}  // namespace ventura
