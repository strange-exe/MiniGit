#pragma once
// Staging area = .minigit/index (one normalized relative path per line).
// Krish's commit reads getStagedFiles(); undo/redo uses setStagedFiles().
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace minigit {

class StagingArea {
public:
    explicit StagingArea(const std::string& rootDir = ".");

    bool load();        // read .minigit/index (called by the constructor)
    bool save() const;  // write .minigit/index

    // stage/unstage only change memory; call save() afterwards.
    bool stage(const std::string& path);    // false if already staged
    bool unstage(const std::string& path);  // false if not staged
    bool isStaged(const std::string& path) const;

    // For commit / undo / redo. These save automatically.
    std::vector<std::string> getStagedFiles() const;  // sorted
    void setStagedFiles(const std::vector<std::string>& snapshot);
    void clear();

    std::size_t size() const { return files_.size(); }

private:
    std::string indexPath() const;

    std::string root_;
    std::set<std::string> files_;
};

}  // namespace minigit
