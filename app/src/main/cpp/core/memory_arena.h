#pragma once

#include "ventura_types.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace ventura {

class MemoryArena {
public:
    MemoryArena();
    ~MemoryArena();

    bool Initialize();
    void Shutdown();

    bool IsValidRange(uint32_t va, uint32_t length) const;
    uint8_t* GetHostPointer(uint32_t va);
    const uint8_t* GetHostPointer(uint32_t va) const;

    uint32_t ReadU32(uint32_t va) const;
    uint16_t ReadU16(uint32_t va) const;
    uint8_t ReadU8(uint32_t va) const;
    float ReadF32(uint32_t va) const;
    double ReadF64(uint32_t va) const;
    std::string ReadCString(uint32_t va, size_t max_len = 512) const;

    bool WriteU32(uint32_t va, uint32_t value);
    bool WriteU16(uint32_t va, uint16_t value);
    bool WriteU8(uint32_t va, uint8_t value);
    bool WriteF32(uint32_t va, float value);
    bool WriteF64(uint32_t va, double value);
    bool WriteBytes(uint32_t va, const uint8_t* src, size_t length);
    bool WriteCString(uint32_t va, const std::string& str);

    uint32_t VirtualAllocSim(uint32_t requested_va, uint32_t size_bytes, uint32_t protection);
    uint32_t HeapAllocSim(uint32_t size_bytes, bool zero_init = true);
    bool VirtualProtectSim(uint32_t va, uint32_t size_bytes, uint32_t new_protect, uint32_t* old_protect);

    void SetupTebPebAndCommandLine(const std::string& command_line);

private:
    struct RegionSegment {
        uint32_t base_va = 0;
        uint32_t size_bytes = 0;
        uint8_t* host_ptr = nullptr;
    };

    RegionSegment* FindSegment(uint32_t va, uint32_t length);
    const RegionSegment* FindSegment(uint32_t va, uint32_t length) const;
    bool AllocateSegment(uint32_t base_va, uint32_t size_bytes);

    std::vector<RegionSegment> segments_;
    uint32_t next_heap_va_ = kDynamicHeapBaseVa;
    mutable std::mutex mutex_;
};

}  // namespace ventura
