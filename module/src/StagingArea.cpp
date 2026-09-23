#include "StagingArea.h"

#include <filesystem>
#include <fstream>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

StagingArea::StagingArea(const std::string& rootDir) : root_(rootDir) { load(); }

std::string StagingArea::indexPath() const {
    return (fs::path(root_) / ".minigit" / "index").string();
}

bool StagingArea::load() {
    files_.clear();
    std::ifstream in(indexPath());
    if (!in) return true;  // no index yet = nothing staged
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) files_.insert(line);
    }
    return true;
}

bool StagingArea::save() const {
    std::error_code ec;
    if (!fs::is_directory(fs::path(root_) / ".minigit", ec)) return false;
    std::ofstream out(indexPath(), std::ios::trunc);
    if (!out) return false;
    for (const auto& f : files_) out << f << '\n';
    return static_cast<bool>(out);
}

bool StagingArea::stage(const std::string& path) {
    std::string n = normalizePath(path, root_);
    if (n.empty()) return false;
    return files_.insert(n).second;
}

bool StagingArea::unstage(const std::string& path) {
    return files_.erase(normalizePath(path, root_)) > 0;
}

bool StagingArea::isStaged(const std::string& path) const {
    return files_.count(normalizePath(path, root_)) > 0;
}

std::vector<std::string> StagingArea::getStagedFiles() const {
    return std::vector<std::string>(files_.begin(), files_.end());
}

void StagingArea::setStagedFiles(const std::vector<std::string>& snapshot) {
    files_.clear();
    for (const auto& f : snapshot) {
        std::string n = normalizePath(f, root_);
        if (!n.empty()) files_.insert(n);
    }
    save();
}

void StagingArea::clear() {
    files_.clear();
    save();
}

}  // namespace minigit
