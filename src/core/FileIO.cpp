#include "yk/core/FileIO.hpp"
#include <fstream>
#include <sstream>

namespace yk {
Result<std::string> readTextFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error{"Cannot open '" + path.string() + "' for reading"};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad())
        return Error{"Cannot read '" + path.string() + "'"};
    return buffer.str();
}
Status writeTextFileAtomic(const std::filesystem::path &path, std::string_view contents) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return Error{"Cannot create directory '" + path.parent_path().string() +
                         "': " + error.message()};
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return Error{"Cannot open '" + temporary.string() + "' for writing"};
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(temporary, error);
            return Error{"Cannot write '" + temporary.string() + "'"};
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) { // Some platforms refuse to rename over an existing file.
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary, error);
        return Error{"Cannot replace '" + path.string() + "': " + error.message()};
    }
    return success();
}
std::string toPortablePath(const std::filesystem::path &path) {
    auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
} // namespace yk
