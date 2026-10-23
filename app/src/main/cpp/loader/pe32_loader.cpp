#include "pe32_loader.h"
#include <cstdio>
#include <cstring>

namespace ventura {

uint32_t Pe32Loader::RvaToFileOffset(uint32_t rva, const std::vector<PeSectionInfo>& sections) {
    for (const auto& sec : sections) {
        uint32_t span = sec.virtual_size > sec.raw_size ? sec.virtual_size : sec.raw_size;
        if (rva >= sec.virtual_address && rva < (sec.virtual_address + span)) {
            uint32_t delta = rva - sec.virtual_address;
            if (delta < sec.raw_size) {
                return sec.raw_offset + delta;
            }
        }
    }
    return rva;
}

PeImageSummary Pe32Loader::InspectBinaryFile(const std::string& file_path) {
    PeImageSummary summary;
    summary.file_path = file_path;

    FILE* fp = std::fopen(file_path.c_str(), "rb");
    if (!fp) {
        summary.error_message = "File tidak ditemukan: " + file_path;
        return summary;
    }

    std::fseek(fp, 0, SEEK_END);
    long fsize = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);

    if (fsize < static_cast<long>(sizeof(PeDosHeader))) {
        std::fclose(fp);
        summary.error_message = "File terlalu kecil untuk PE32.";
        return summary;
    }
    summary.file_size = static_cast<uint64_t>(fsize);

    std::vector<uint8_t> buffer(static_cast<size_t>(fsize));
    size_t read_bytes = std::fread(buffer.data(), 1, buffer.size(), fp);
    std::fclose(fp);

    if (read_bytes != buffer.size()) {
        summary.error_message = "Gagal membaca file PE32.";
        return summary;
    }

    PeDosHeader dos{};
    std::memcpy(&dos, buffer.data(), sizeof(PeDosHeader));
    if (dos.e_magic != kPeDosSignature || dos.e_lfanew < 0 ||
        static_cast<size_t>(dos.e_lfanew + 4 + sizeof(PeFileHeader)) > buffer.size()) {
        summary.error_message = "Header DOS MZ tidak valid.";
        return summary;
    }

    uint32_t nt_sig = 0;
    std::memcpy(&nt_sig, buffer.data() + dos.e_lfanew, sizeof(uint32_t));
    if (nt_sig != kPeNtSignature) {
        summary.error_message = "Header PE00 tidak valid.";
        return summary;
    }

    PeFileHeader file_hdr{};
    size_t file_hdr_off = static_cast<size_t>(dos.e_lfanew) + 4;
    std::memcpy(&file_hdr, buffer.data() + file_hdr_off, sizeof(PeFileHeader));
    summary.machine = file_hdr.machine;
    if (file_hdr.machine != kPeMachineI386) {
        summary.error_message = "Biner bukan PE32 x86 (0x014C).";
        return summary;
    }

    size_t opt_hdr_off = file_hdr_off + sizeof(PeFileHeader);
    if (opt_hdr_off + sizeof(PeOptionalHeader32) > buffer.size()) {
        summary.error_message = "OptionalHeader32 terpotong.";
        return summary;
    }

    PeOptionalHeader32 opt_hdr{};
    std::memcpy(&opt_hdr, buffer.data() + opt_hdr_off, sizeof(PeOptionalHeader32));
    if (opt_hdr.magic != kPeOptionalHdr32Magic) {
        summary.error_message = "Bukan biner PE32 32-bit.";
        return summary;
    }

    summary.image_base = opt_hdr.image_base;
    summary.entry_point_rva = opt_hdr.address_of_entry_point;
    summary.entry_point_va = opt_hdr.image_base + opt_hdr.address_of_entry_point;
    summary.size_of_image = opt_hdr.size_of_image;
    summary.checksum = opt_hdr.check_sum;
    summary.reloc_rva = opt_hdr.data_directory[5].virtual_address;
    summary.reloc_size = opt_hdr.data_directory[5].size;

    size_t sec_table_off = opt_hdr_off + file_hdr.size_of_optional_header;
    for (uint16_t i = 0; i < file_hdr.number_of_sections; ++i) {
        size_t cur_off = sec_table_off + (i * sizeof(PeSectionHeader));
        if (cur_off + sizeof(PeSectionHeader) > buffer.size()) break;
        PeSectionHeader sec_hdr{};
        std::memcpy(&sec_hdr, buffer.data() + cur_off, sizeof(PeSectionHeader));

        char clean_name[9] = {0};
        std::memcpy(clean_name, sec_hdr.name, 8);
        PeSectionInfo info;
        info.name = std::string(clean_name);
        info.virtual_address = sec_hdr.virtual_address;
        info.virtual_size = sec_hdr.virtual_size;
        info.raw_offset = sec_hdr.pointer_to_raw_data;
        info.raw_size = sec_hdr.size_of_raw_data;
        info.characteristics = sec_hdr.characteristics;
        summary.sections.push_back(info);
    }

    uint32_t import_rva = opt_hdr.data_directory[1].virtual_address;
    uint32_t import_size = opt_hdr.data_directory[1].size;
    if (import_rva != 0 && import_size >= sizeof(PeImportDescriptor)) {
        uint32_t import_off = RvaToFileOffset(import_rva, summary.sections);
        while (import_off + sizeof(PeImportDescriptor) <= buffer.size()) {
            PeImportDescriptor desc{};
            std::memcpy(&desc, buffer.data() + import_off, sizeof(PeImportDescriptor));
            if (desc.name_rva == 0 && desc.first_thunk == 0) break;

            PeImportModule mod;
            mod.iat_rva = desc.first_thunk;
            uint32_t name_off = RvaToFileOffset(desc.name_rva, summary.sections);
            if (name_off < buffer.size()) {
                const char* dll_cstr = reinterpret_cast<const char*>(buffer.data() + name_off);
                size_t max_len = buffer.size() - name_off;
                mod.dll_name = std::string(dll_cstr, strnlen(dll_cstr, max_len));
            }

            uint32_t thunk_rva = desc.original_first_thunk != 0 ? desc.original_first_thunk : desc.first_thunk;
            uint32_t thunk_off = RvaToFileOffset(thunk_rva, summary.sections);
            int guard = 0;
            while (thunk_off + sizeof(uint32_t) <= buffer.size() && guard < 1024) {
                uint32_t thunk_val = 0;
                std::memcpy(&thunk_val, buffer.data() + thunk_off, sizeof(uint32_t));
                if (thunk_val == 0) break;
                if ((thunk_val & 0x80000000u) != 0) {
                    uint16_t ordinal = static_cast<uint16_t>(thunk_val & 0xFFFFu);
                    mod.functions.push_back("Ordinal#" + std::to_string(ordinal));
                } else {
                    uint32_t hint_off = RvaToFileOffset(thunk_val, summary.sections);
                    if (hint_off + 2 < buffer.size()) {
                        const char* fn_cstr = reinterpret_cast<const char*>(buffer.data() + hint_off + 2);
                        size_t max_len = buffer.size() - (hint_off + 2);
                        mod.functions.push_back(std::string(fn_cstr, strnlen(fn_cstr, max_len)));
                    }
                }
                thunk_off += sizeof(uint32_t);
                ++guard;
            }
            summary.imports.push_back(mod);
            import_off += sizeof(PeImportDescriptor);
        }
    }

    summary.valid = true;
    return summary;
}

void Pe32Loader::ApplyBaseRelocations(
    MemoryArena& arena,
    uint32_t mapped_base_va,
    int32_t delta,
    uint32_t reloc_rva,
    uint32_t reloc_size
) {
    if (delta == 0 || reloc_rva == 0 || reloc_size < sizeof(PeBaseRelocationBlock)) {
        return;
    }
    uint32_t cursor_va = mapped_base_va + reloc_rva;
    uint32_t end_va = cursor_va + reloc_size;

    while (cursor_va + sizeof(PeBaseRelocationBlock) <= end_va && arena.IsValidRange(cursor_va, 8)) {
        uint32_t page_rva = arena.ReadU32(cursor_va);
        uint32_t block_size = arena.ReadU32(cursor_va + 4);
        if (block_size < sizeof(PeBaseRelocationBlock)) break;

        uint32_t entries_count = (block_size - sizeof(PeBaseRelocationBlock)) / sizeof(uint16_t);
        for (uint32_t i = 0; i < entries_count; ++i) {
            uint16_t entry = arena.ReadU16(cursor_va + 8 + (i * 2));
            uint8_t type = static_cast<uint8_t>((entry >> 12) & 0xFu);
            uint16_t offset = static_cast<uint16_t>(entry & 0x0FFFu);
            if (type == 3) { // IMAGE_REL_BASED_HIGHLOW
                uint32_t patch_va = mapped_base_va + page_rva + offset;
                if (arena.IsValidRange(patch_va, 4)) {
                    uint32_t orig_val = arena.ReadU32(patch_va);
                    arena.WriteU32(patch_va, static_cast<uint32_t>(static_cast<int64_t>(orig_val) + delta));
                }
            }
        }
        cursor_va += block_size;
    }
}

bool Pe32Loader::LoadModuleIntoProcess(
    const std::string& file_path,
    uint32_t preferred_base_va,
    MemoryArena& arena,
    Win32Bridge& win32_bridge,
    PeImageSummary* out_summary
) {
    PeImageSummary summary = InspectBinaryFile(file_path);
    if (!summary.valid) {
        if (out_summary) *out_summary = summary;
        return false;
    }

    FILE* fp = std::fopen(file_path.c_str(), "rb");
    if (!fp) return false;
    std::vector<uint8_t> buffer(static_cast<size_t>(summary.file_size));
    size_t read_bytes = std::fread(buffer.data(), 1, buffer.size(), fp);
    std::fclose(fp);
    if (read_bytes != buffer.size()) return false;

    uint32_t target_base = preferred_base_va != 0 ? preferred_base_va : summary.image_base;
    if (!arena.IsValidRange(target_base, 4096)) {
        target_base = kGtaSaImageBase;
    }

    size_t hdr_copy = buffer.size() < 4096 ? buffer.size() : 4096;
    arena.WriteBytes(target_base, buffer.data(), hdr_copy);

    for (const auto& sec : summary.sections) {
        if (sec.raw_size > 0 && (sec.raw_offset + sec.raw_size) <= buffer.size()) {
            arena.WriteBytes(
                target_base + sec.virtual_address,
                buffer.data() + sec.raw_offset,
                sec.raw_size
            );
        }
    }

    int32_t delta = static_cast<int32_t>(target_base) - static_cast<int32_t>(summary.image_base);
    if (delta != 0) {
        ApplyBaseRelocations(arena, target_base, delta, summary.reloc_rva, summary.reloc_size);
    }

    // Patch Import Address Table (IAT) entries directly to Win32Bridge Native Thunk VAs
    for (const auto& mod : summary.imports) {
        uint32_t iat_slot_va = target_base + mod.iat_rva;
        for (const auto& fn_name : mod.functions) {
            uint32_t thunk_va = win32_bridge.ResolveProcAddress(mod.dll_name, fn_name);
            arena.WriteU32(iat_slot_va, thunk_va);
            iat_slot_va += 4;
        }
    }

    summary.image_base = target_base;
    summary.entry_point_va = target_base + summary.entry_point_rva;
    if (out_summary) {
        *out_summary = summary;
    }
    return true;
}

}  // namespace ventura
