#include "memory_arena.h"
#include <sys/mman.h>
#include <cstring>

namespace ventura {

MemoryArena::MemoryArena() = default;

MemoryArena::~MemoryArena() {
    Shutdown();
}

bool MemoryArena::AllocateSegment(uint32_t base_va, uint32_t size_bytes) {
    void* mapped = mmap(
        nullptr,
        size_bytes,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (mapped == MAP_FAILED) {
        return false;
    }
    std::memset(mapped, 0, size_bytes);
    RegionSegment seg;
    seg.base_va = base_va;
    seg.size_bytes = size_bytes;
    seg.host_ptr = static_cast<uint8_t*>(mapped);
    segments_.push_back(seg);
    return true;
}

bool MemoryArena::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& seg : segments_) {
        if (seg.host_ptr != nullptr) {
            munmap(seg.host_ptr, seg.size_bytes);
        }
    }
    segments_.clear();
    next_heap_va_ = kDynamicHeapBaseVa;

    // 1. Primary Image & Stack Segment: 0x00400000 .. 0x02400000 (32 MB for gta_sa.exe + Stack)
    if (!AllocateSegment(kGtaSaImageBase, 0x02000000)) return false;
    // 2. SA-MP & ASI DLL Segment: 0x10000000 .. 0x12000000 (32 MB for samp.dll & plugins)
    if (!AllocateSegment(kSampModuleBaseVa, 0x02000000)) return false;
    // 3. Dynamic Process Heap Segment: 0x20000000 .. 0x24000000 (64 MB for VirtualAlloc/HeapAlloc)
    if (!AllocateSegment(kDynamicHeapBaseVa, 0x04000000)) return false;
    // 4. COM Vtable & Win32 Thunk Segment: 0x60000000 .. 0x71000000 (D3D9, DInput8, Thunk table)
    if (!AllocateSegment(kComVtableArenaBaseVa, 0x01000000)) return false;
    if (!AllocateSegment(kWin32ThunkTableBaseVa, 0x00400000)) return false;
    // 5. TEB / PEB / CommandLine Segment: 0x7FFD0000 .. 0x7FFF0000
    if (!AllocateSegment(0x7FFD0000, 0x00020000)) return false;

    SetupTebPebAndCommandLine("gta_sa.exe");
    return true;
}

void MemoryArena::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& seg : segments_) {
        if (seg.host_ptr != nullptr) {
            munmap(seg.host_ptr, seg.size_bytes);
            seg.host_ptr = nullptr;
        }
    }
    segments_.clear();
}

void MemoryArena::SetupTebPebAndCommandLine(const std::string& command_line) {
    // Initialize Windows Thread Environment Block (TEB) at FS:[0x00] (0x7FFDF000)
    WriteU32(kTebBaseVa + 0x00, 0xFFFFFFFFu);     // Initial SEH ExceptionList frame
    WriteU32(kTebBaseVa + 0x04, 0x01C00000u);     // StackBase (high address)
    WriteU32(kTebBaseVa + 0x08, 0x01A00000u);     // StackLimit (low address)
    WriteU32(kTebBaseVa + 0x18, kTebBaseVa);      // TEB Linear Self-Pointer (FS:[0x18])
    WriteU32(kTebBaseVa + 0x24, 0x00000DC0u);     // CurrentThreadId
    WriteU32(kTebBaseVa + 0x30, kPebBaseVa);      // Pointer to PEB (FS:[0x30])

    // Initialize Process Environment Block (PEB) at 0x7FFDE000
    WriteU32(kPebBaseVa + 0x08, kGtaSaImageBase); // ImageBaseAddress
    WriteU32(kPebBaseVa + 0x18, kDynamicHeapBaseVa); // ProcessHeap

    // Write launch command line string at 0x7FFDD000
    WriteCString(kCommandLineVa, command_line);
}

MemoryArena::RegionSegment* MemoryArena::FindSegment(uint32_t va, uint32_t length) {
    for (auto& seg : segments_) {
        if (va >= seg.base_va) {
            uint64_t offset = static_cast<uint64_t>(va - seg.base_va);
            if (offset + length <= static_cast<uint64_t>(seg.size_bytes)) {
                return &seg;
            }
        }
    }
    return nullptr;
}

const MemoryArena::RegionSegment* MemoryArena::FindSegment(uint32_t va, uint32_t length) const {
    for (const auto& seg : segments_) {
        if (va >= seg.base_va) {
            uint64_t offset = static_cast<uint64_t>(va - seg.base_va);
            if (offset + length <= static_cast<uint64_t>(seg.size_bytes)) {
                return &seg;
            }
        }
    }
    return nullptr;
}

bool MemoryArena::IsValidRange(uint32_t va, uint32_t length) const {
    return FindSegment(va, length) != nullptr;
}

uint8_t* MemoryArena::GetHostPointer(uint32_t va) {
    RegionSegment* seg = FindSegment(va, 1);
    return seg ? (seg->host_ptr + (va - seg->base_va)) : nullptr;
}

const uint8_t* MemoryArena::GetHostPointer(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, 1);
    return seg ? (seg->host_ptr + (va - seg->base_va)) : nullptr;
}

uint32_t MemoryArena::ReadU32(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, sizeof(uint32_t));
    if (!seg) return 0;
    uint32_t val = 0;
    std::memcpy(&val, seg->host_ptr + (va - seg->base_va), sizeof(uint32_t));
    return val;
}

uint16_t MemoryArena::ReadU16(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, sizeof(uint16_t));
    if (!seg) return 0;
    uint16_t val = 0;
    std::memcpy(&val, seg->host_ptr + (va - seg->base_va), sizeof(uint16_t));
    return val;
}

uint8_t MemoryArena::ReadU8(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, sizeof(uint8_t));
    if (!seg) return 0;
    return seg->host_ptr[va - seg->base_va];
}

float MemoryArena::ReadF32(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, sizeof(float));
    if (!seg) return 0.0f;
    float val = 0.0f;
    std::memcpy(&val, seg->host_ptr + (va - seg->base_va), sizeof(float));
    return val;
}

double MemoryArena::ReadF64(uint32_t va) const {
    const RegionSegment* seg = FindSegment(va, sizeof(double));
    if (!seg) return 0.0;
    double val = 0.0;
    std::memcpy(&val, seg->host_ptr + (va - seg->base_va), sizeof(double));
    return val;
}

std::string MemoryArena::ReadCString(uint32_t va, size_t max_len) const {
    const uint8_t* ptr = GetHostPointer(va);
    if (!ptr) return "";
    size_t len = 0;
    while (len < max_len && IsValidRange(va + static_cast<uint32_t>(len), 1) && ptr[len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(ptr), len);
}

bool MemoryArena::WriteU32(uint32_t va, uint32_t value) {
    RegionSegment* seg = FindSegment(va, sizeof(uint32_t));
    if (!seg) return false;
    std::memcpy(seg->host_ptr + (va - seg->base_va), &value, sizeof(uint32_t));
    return true;
}

bool MemoryArena::WriteU16(uint32_t va, uint16_t value) {
    RegionSegment* seg = FindSegment(va, sizeof(uint16_t));
    if (!seg) return false;
    std::memcpy(seg->host_ptr + (va - seg->base_va), &value, sizeof(uint16_t));
    return true;
}

bool MemoryArena::WriteU8(uint32_t va, uint8_t value) {
    RegionSegment* seg = FindSegment(va, sizeof(uint8_t));
    if (!seg) return false;
    seg->host_ptr[va - seg->base_va] = value;
    return true;
}

bool MemoryArena::WriteF32(uint32_t va, float value) {
    RegionSegment* seg = FindSegment(va, sizeof(float));
    if (!seg) return false;
    std::memcpy(seg->host_ptr + (va - seg->base_va), &value, sizeof(float));
    return true;
}

bool MemoryArena::WriteF64(uint32_t va, double value) {
    RegionSegment* seg = FindSegment(va, sizeof(double));
    if (!seg) return false;
    std::memcpy(seg->host_ptr + (va - seg->base_va), &value, sizeof(double));
    return true;
}

bool MemoryArena::WriteBytes(uint32_t va, const uint8_t* src, size_t length) {
    if (!src) return false;
    RegionSegment* seg = FindSegment(va, static_cast<uint32_t>(length));
    if (!seg) return false;
    std::memcpy(seg->host_ptr + (va - seg->base_va), src, length);
    return true;
}

bool MemoryArena::WriteCString(uint32_t va, const std::string& str) {
    if (!WriteBytes(va, reinterpret_cast<const uint8_t*>(str.data()), str.size())) {
        return false;
    }
    return WriteU8(va + static_cast<uint32_t>(str.size()), 0);
}

uint32_t MemoryArena::VirtualAllocSim(uint32_t requested_va, uint32_t size_bytes, uint32_t /*protection*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t aligned_size = (size_bytes + 0xFFFu) & ~0xFFFu;
    if (aligned_size == 0) aligned_size = 4096;

    if (requested_va != 0 && FindSegment(requested_va, aligned_size) != nullptr) {
        return requested_va;
    }
    RegionSegment* heap_seg = FindSegment(next_heap_va_, aligned_size);
    if (!heap_seg) {
        return 0;
    }
    uint32_t allocated = next_heap_va_;
    std::memset(heap_seg->host_ptr + (allocated - heap_seg->base_va), 0, aligned_size);
    next_heap_va_ += aligned_size;
    return allocated;
}

uint32_t MemoryArena::HeapAllocSim(uint32_t size_bytes, bool zero_init) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t aligned_size = (size_bytes + 15u) & ~15u;
    if (aligned_size == 0) aligned_size = 16;

    RegionSegment* heap_seg = FindSegment(next_heap_va_, aligned_size);
    if (!heap_seg) {
        return 0;
    }
    uint32_t allocated = next_heap_va_;
    if (zero_init) {
        std::memset(heap_seg->host_ptr + (allocated - heap_seg->base_va), 0, aligned_size);
    }
    next_heap_va_ += aligned_size;
    return allocated;
}

bool MemoryArena::VirtualProtectSim(uint32_t va, uint32_t size_bytes, uint32_t /*new_protect*/, uint32_t* old_protect) {
    if (!IsValidRange(va, size_bytes)) {
        return false;
    }
    if (old_protect != nullptr) {
        *old_protect = 0x40; // PAGE_EXECUTE_READWRITE
    }
    return true;
}

}  // namespace ventura
