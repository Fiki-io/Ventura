#pragma once

#include "ventura_types.h"
#include "../core/memory_arena.h"
#include "../bridge/win32_bridge.h"
#include <string>

namespace ventura {

class Pe32Loader {
public:
    static PeImageSummary InspectBinaryFile(const std::string& file_path);

    static bool LoadModuleIntoProcess(
        const std::string& file_path,
        uint32_t preferred_base_va,
        MemoryArena& arena,
        Win32Bridge& win32_bridge,
        PeImageSummary* out_summary
    );

private:
    static uint32_t RvaToFileOffset(uint32_t rva, const std::vector<PeSectionInfo>& sections);
    static void ApplyBaseRelocations(
        MemoryArena& arena,
        uint32_t mapped_base_va,
        int32_t delta,
        uint32_t reloc_rva,
        uint32_t reloc_size
    );
};

}  // namespace ventura
