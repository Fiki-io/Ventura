#pragma once

#include "ventura_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ventura {

struct ImgAssetRecord {
    std::string name;
    uint32_t sector_offset = 0;
    uint64_t byte_offset = 0;
    uint32_t byte_size = 0;
    bool is_dff_model = false;
    bool is_txd_texture = false;
    uint32_t rw_section_type = 0;
    uint32_t rw_version = 0;
};

struct ImgArchiveSummary {
    bool valid = false;
    std::string archive_path;
    std::string magic;
    uint32_t total_entries = 0;
    uint32_t dff_count = 0;
    uint32_t txd_count = 0;
    std::vector<ImgAssetRecord> entries;
    std::string error_message;
};

class ImgVer2Reader {
public:
    static ImgArchiveSummary ParseArchive(const std::string& img_path, size_t max_preview_entries = 250);
};

}  // namespace ventura
