#pragma once
#include "yk/core/Result.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// A small ZIP writer (and reader, for checking what was written) so an exported game can be
// shipped as one file without depending on an archiver being installed. Entries are stored, not
// compressed: that keeps the writer tiny and the output byte-for-byte reproducible (fixed
// timestamps, sorted by the caller), and game art and sound do not compress much more than that
// anyway. Unix permissions are recorded so an executable stays executable after unzipping on
// macOS and Linux.
namespace yk {
struct ZipEntry {
    std::string name;             // Path inside the archive, '/' separated, no leading slash.
    std::filesystem::path source; // File whose bytes are stored.
    bool executable{false};
};
// Writes `archive` atomically. Fails on names that would escape the archive, duplicate names,
// unreadable sources and anything beyond the classic (non-ZIP64) limits: 4 GB and 65535 entries.
Status writeZip(const std::filesystem::path &archive, const std::vector<ZipEntry> &entries);

struct ZipInfo {
    std::string name;
    std::uint32_t size{};
    std::uint32_t crc32{};
    std::uint32_t unixMode{}; // Permission bits recorded by the writer (0 when none).
    std::string contents;
};
// Reads every entry of a stored (uncompressed) archive and verifies its CRC.
Result<std::vector<ZipInfo>> readZip(const std::filesystem::path &archive);
std::uint32_t crc32(std::string_view bytes);
} // namespace yk
