#pragma once
// Shared path helpers. Everyone in the team should use normalizePath()
// so that ".\src\a.txt", "./src/a.txt" and "src/a.txt" all become "src/a.txt".
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace minigit {

inline std::string reverseString(std::string s) {
    std::reverse(s.begin(), s.end());
    return s;
}

// Returns a repo-relative path using '/' separators. A trailing '/' is kept
// (it marks a directory rule). Returns "" for empty input.
inline std::string normalizePath(const std::string& input, const std::string& root = "") {
    namespace fs = std::filesystem;
    std::string s = input;
    for (char& c : s)
        if (c == '\\') c = '/';
    if (s.empty()) return "";

    bool trailingSlash = (s.back() == '/');
    fs::path p(s);

    if (p.is_absolute() && !root.empty()) {
        std::error_code ec;
        fs::path absRoot = fs::weakly_canonical(fs::path(root), ec);
        fs::path absP = fs::weakly_canonical(p, ec);
        fs::path rel = absP.lexically_relative(absRoot);
        if (!rel.empty()) p = rel;
    }

    std::string out = p.lexically_normal().generic_string();
    if (out == "./") out = ".";
    if (trailingSlash && out != "." && !out.empty() && out.back() != '/') out += '/';
    return out;
}

// True if the normalized path escapes the repository (or is absolute).
inline bool isOutsideRepo(const std::string& norm) {
    if (norm.empty()) return true;
    if (norm == ".." || norm.rfind("../", 0) == 0) return true;
    if (norm[0] == '/') return true;
    if (norm.size() > 1 && norm[1] == ':') return true;  // Windows drive letter
    return false;
}

}  // namespace minigit
