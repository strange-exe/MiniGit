#include "StagingArea.h"

#include <filesystem>
#include <fstream>

#include "FileUtils.h"
#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

StagingArea::StagingArea(const std::string& rootDir) : root_(rootDir) { load(); }

std::string StagingArea::indexPath() const {
    return (fs::path(root_) / ".minigit" / "index").string();
}

bool StagingArea::load() {
    files_.clear();
    deletions_.clear();
    std::ifstream in(indexPath());
    if (!in) return true;  // no index yet = nothing staged
    const std::string prefix = kStagedDeletePrefix;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        bool isDeletion = line.compare(0, prefix.size(), prefix) == 0;
        std::string path = isDeletion ? line.substr(prefix.size()) : line;
        // An index edited by hand (or damaged) must not point outside the working tree.
        if (!isSafeRepoPath(path)) continue;
        (isDeletion ? deletions_ : files_).insert(path);
    }
    return true;
}

bool StagingArea::save() const {
    std::error_code ec;
    if (!fs::is_directory(fs::path(root_) / ".minigit", ec)) return false;
    std::string text;
    for (const auto& f : files_) text += f + '\n';
    for (const auto& d : deletions_) text += std::string(kStagedDeletePrefix) + d + '\n';
    return writeFileAtomic(indexPath(), text);
}

bool StagingArea::stage(const std::string& path) {
    std::string n = normalizePath(path, root_);
    if (n.empty()) return false;
    bool cancelledDeletion = deletions_.erase(n) > 0;
    return files_.insert(n).second || cancelledDeletion;
}

bool StagingArea::stageDeletion(const std::string& path) {
    std::string n = normalizePath(path, root_);
    if (n.empty()) return false;
    files_.erase(n);
    return deletions_.insert(n).second;
}

bool StagingArea::unstage(const std::string& path) {
    std::string n = normalizePath(path, root_);
    return files_.erase(n) + deletions_.erase(n) > 0;
}

bool StagingArea::isStaged(const std::string& path) const {
    return files_.count(normalizePath(path, root_)) > 0;
}

bool StagingArea::isStagedForDeletion(const std::string& path) const {
    return deletions_.count(normalizePath(path, root_)) > 0;
}

std::vector<std::string> StagingArea::getStagedFiles() const {
    return std::vector<std::string>(files_.begin(), files_.end());
}

std::vector<std::string> StagingArea::getStagedDeletions() const {
    return std::vector<std::string>(deletions_.begin(), deletions_.end());
}

std::vector<std::string> StagingArea::getStagedEntries() const {
    std::vector<std::string> entries(files_.begin(), files_.end());
    for (const auto& d : deletions_) entries.push_back(std::string(kStagedDeletePrefix) + d);
    return entries;
}

void StagingArea::setStagedFiles(const std::vector<std::string>& snapshot) {
    files_.clear();
    deletions_.clear();
    const std::string prefix = kStagedDeletePrefix;
    for (const auto& f : snapshot) {
        bool isDeletion = f.compare(0, prefix.size(), prefix) == 0;
        std::string n = normalizePath(isDeletion ? f.substr(prefix.size()) : f, root_);
        if (n.empty()) continue;
        (isDeletion ? deletions_ : files_).insert(n);
    }
    save();
}

void StagingArea::clear() {
    files_.clear();
    deletions_.clear();
    save();
}

}  // namespace minigit
