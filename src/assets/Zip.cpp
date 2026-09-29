#include "yk/assets/Zip.hpp"
#include "yk/core/FileIO.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <set>

namespace yk {
namespace {
constexpr std::uint32_t localSignature = 0x04034b50;
constexpr std::uint32_t centralSignature = 0x02014b50;
constexpr std::uint32_t endSignature = 0x06054b50;
constexpr std::uint16_t utf8Flag = 0x0800;
// 1980-01-01 00:00, the earliest time a ZIP can hold: archives do not change from run to run.
constexpr std::uint16_t dosTime = 0;
constexpr std::uint16_t dosDate = (0 << 9) | (1 << 5) | 1;

const std::array<std::uint32_t, 256> &crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> made{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1) : value >> 1;
            made[i] = value;
        }
        return made;
    }();
    return table;
}

void put16(std::string &out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8) & 0xFFU));
}
void put32(std::string &out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xFFU));
}
std::uint16_t get16(const std::string &bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) |
                                      (static_cast<unsigned char>(bytes[at + 1]) << 8));
}
std::uint32_t get32(const std::string &bytes, std::size_t at) {
    return static_cast<std::uint32_t>(get16(bytes, at)) |
           (static_cast<std::uint32_t>(get16(bytes, at + 2)) << 16);
}

Result<std::string> readBinary(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return Error{"Cannot read '" + path.string() + "'"};
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
        return Error{"Cannot read '" + path.string() + "'"};
    return bytes;
}
} // namespace

std::uint32_t crc32(std::string_view bytes) {
    const auto &table = crcTable();
    std::uint32_t value = 0xFFFFFFFFU;
    for (const char c : bytes)
        value = table[(value ^ static_cast<unsigned char>(c)) & 0xFFU] ^ (value >> 8);
    return value ^ 0xFFFFFFFFU;
}

Status writeZip(const std::filesystem::path &archive, const std::vector<ZipEntry> &entries) {
    if (entries.size() > 65535)
        return Error{"A ZIP archive holds at most 65535 files"};
    std::string body, directory;
    std::set<std::string> seen;
    for (const ZipEntry &entry : entries) {
        if (entry.name.empty() || entry.name.front() == '/' ||
            entry.name.find('\\') != std::string::npos ||
            entry.name.find("..") != std::string::npos)
            return Error{"Not a usable archive path: '" + entry.name + "'"};
        if (!seen.insert(entry.name).second)
            return Error{"'" + entry.name + "' appears twice in the archive"};
        auto bytes = readBinary(entry.source);
        if (!bytes)
            return Error{bytes.error()};
        if (bytes.value().size() >= 0xFFFFFFFFULL || body.size() >= 0xF0000000ULL)
            return Error{"The archive would be larger than 4 GB, which classic ZIP cannot hold"};
        const auto size = static_cast<std::uint32_t>(bytes.value().size());
        const std::uint32_t crc = crc32(bytes.value());
        const auto offset = static_cast<std::uint32_t>(body.size());

        put32(body, localSignature);
        put16(body, 20); // Version needed: 2.0.
        put16(body, utf8Flag);
        put16(body, 0); // Method: stored.
        put16(body, dosTime);
        put16(body, dosDate);
        put32(body, crc);
        put32(body, size);
        put32(body, size);
        put16(body, static_cast<std::uint16_t>(entry.name.size()));
        put16(body, 0); // No extra field.
        body += entry.name;
        body += bytes.value();

        put32(directory, centralSignature);
        put16(directory, (3 << 8) | 20); // Made by Unix, version 2.0: the mode below is meaningful.
        put16(directory, 20);
        put16(directory, utf8Flag);
        put16(directory, 0);
        put16(directory, dosTime);
        put16(directory, dosDate);
        put32(directory, crc);
        put32(directory, size);
        put32(directory, size);
        put16(directory, static_cast<std::uint16_t>(entry.name.size()));
        put16(directory, 0); // Extra field length.
        put16(directory, 0); // Comment length.
        put16(directory, 0); // Disk number.
        put16(directory, 0); // Internal attributes.
        put32(directory, (0100000U | (entry.executable ? 0755U : 0644U)) << 16);
        put32(directory, offset);
        directory += entry.name;
    }
    std::string end;
    put32(end, endSignature);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<std::uint16_t>(entries.size()));
    put16(end, static_cast<std::uint16_t>(entries.size()));
    put32(end, static_cast<std::uint32_t>(directory.size()));
    put32(end, static_cast<std::uint32_t>(body.size()));
    put16(end, 0);
    // writeTextFileAtomic writes bytes as given, so it serves for binary content too.
    return writeTextFileAtomic(archive, body + directory + end);
}

Result<std::vector<ZipInfo>> readZip(const std::filesystem::path &archive) {
    auto file = readBinary(archive);
    if (!file)
        return Error{file.error()};
    const std::string &bytes = file.value();
    if (bytes.size() < 22)
        return Error{"'" + archive.string() + "' is not a ZIP archive"};
    std::size_t end = std::string::npos;
    for (std::size_t at = bytes.size() - 22;; --at) {
        if (get32(bytes, at) == endSignature) {
            end = at;
            break;
        }
        if (at == 0)
            break;
    }
    if (end == std::string::npos)
        return Error{"'" + archive.string() + "' has no ZIP directory"};
    const std::size_t count = get16(bytes, end + 10);
    std::size_t at = get32(bytes, end + 16);
    std::vector<ZipInfo> infos;
    for (std::size_t i = 0; i < count; ++i) {
        if (at + 46 > bytes.size() || get32(bytes, at) != centralSignature)
            return Error{"The ZIP directory is damaged"};
        ZipInfo info;
        const std::uint16_t method = get16(bytes, at + 10);
        info.crc32 = get32(bytes, at + 16);
        info.size = get32(bytes, at + 24);
        const std::size_t nameLength = get16(bytes, at + 28);
        const std::size_t extraLength = get16(bytes, at + 30) + get16(bytes, at + 32);
        info.unixMode = get32(bytes, at + 38) >> 16;
        const std::size_t local = get32(bytes, at + 42);
        if (at + 46 + nameLength > bytes.size())
            return Error{"The ZIP directory is damaged"};
        info.name = bytes.substr(at + 46, nameLength);
        if (method != 0)
            return Error{"'" + info.name + "' is compressed; only stored archives are read here"};
        if (local + 30 > bytes.size() || get32(bytes, local) != localSignature)
            return Error{"The entry '" + info.name + "' is damaged"};
        const std::size_t data = local + 30 + get16(bytes, local + 26) + get16(bytes, local + 28);
        if (data + info.size > bytes.size())
            return Error{"The entry '" + info.name + "' is cut off"};
        info.contents = bytes.substr(data, info.size);
        if (crc32(info.contents) != info.crc32)
            return Error{"The entry '" + info.name + "' fails its checksum"};
        infos.push_back(std::move(info));
        at += 46 + nameLength + extraLength;
    }
    return infos;
}
} // namespace yk
