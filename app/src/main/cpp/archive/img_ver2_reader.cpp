#include "img_ver2_reader.h"
#include <cstdio>
#include <cstring>

namespace ventura {

ImgArchiveSummary ImgVer2Reader::ParseArchive(const std::string& img_path, size_t max_preview_entries) {
    ImgArchiveSummary summary;
    summary.archive_path = img_path;

    FILE* fp = std::fopen(img_path.c_str(), "rb");
    if (!fp) {
        summary.error_message = "File arsip tidak ditemukan: " + img_path;
        return summary;
    }

    ImgVer2Header hdr{};
    if (std::fread(&hdr, sizeof(ImgVer2Header), 1, fp) != 1) {
        std::fclose(fp);
        summary.error_message = "Gagal membaca 8-byte header VER2.";
        return summary;
    }

    summary.magic = std::string(hdr.magic, 4);
    if (summary.magic != "VER2") {
        std::fclose(fp);
        summary.error_message = "Header arsip bukan VER2 (ditemukan: " + summary.magic + ").";
        return summary;
    }

    summary.total_entries = hdr.entry_count;
    uint32_t count_to_read = hdr.entry_count > 65536 ? 65536 : hdr.entry_count;

    std::vector<ImgVer2DirectoryEntry> raw_entries(count_to_read);
    size_t read_items = std::fread(raw_entries.data(), sizeof(ImgVer2DirectoryEntry), count_to_read, fp);

    for (size_t i = 0; i < read_items; ++i) {
        const auto& raw = raw_entries[i];
        char clean_name[25] = {0};
        std::memcpy(clean_name, raw.file_name, 24);
        std::string name_str(clean_name);

        uint16_t sectors = raw.streaming_size_sectors != 0 ? raw.streaming_size_sectors : raw.archive_size_sectors;
        ImgAssetRecord rec;
        rec.name = name_str;
        rec.sector_offset = raw.sector_offset;
        rec.byte_offset = static_cast<uint64_t>(raw.sector_offset) * kImgSectorSize;
        rec.byte_size = static_cast<uint32_t>(sectors) * kImgSectorSize;

        if (name_str.size() >= 4) {
            std::string ext = name_str.substr(name_str.size() - 4);
            for (char& c : ext) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            }
            if (ext == ".dff") {
                rec.is_dff_model = true;
                summary.dff_count++;
            } else if (ext == ".txd") {
                rec.is_txd_texture = true;
                summary.txd_count++;
            }
        }

        if (summary.entries.size() < max_preview_entries) {
            // Read 12-byte RenderWare stream header (section_type, section_size, rw_version)
            if (std::fseek(fp, static_cast<long>(rec.byte_offset), SEEK_SET) == 0) {
                uint32_t rw_hdr[3] = {0, 0, 0};
                if (std::fread(rw_hdr, sizeof(uint32_t), 3, fp) == 3) {
                    rec.rw_section_type = rw_hdr[0];
                    rec.rw_version = rw_hdr[2];
                }
            }
            summary.entries.push_back(rec);
        }
    }

    std::fclose(fp);
    summary.valid = true;
    return summary;
}

}  // namespace ventura
