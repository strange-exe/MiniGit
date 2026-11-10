#pragma once
// Small file helpers shared by every module that writes repository state.
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace minigit {

// Replaces `target` with `content` without ever leaving a half-written file:
// the text goes to a hidden sibling file first and is renamed over the target
// only after it was written completely. If anything fails the old file is
// untouched and false is returned. (Branch names cannot start with '.', so a
// leftover temp file in refs/heads can never be mistaken for a branch.)
inline bool writeFileAtomic(const std::string& target, const std::string& content) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dest(target);
    fs::path tmp = dest.parent_path() / ("." + dest.filename().string() + ".tmp");
    {
        std::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, dest, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

}  // namespace minigit
