#pragma once
// Staging area = .minigit/index (one normalized relative path per line).
// Krish's commit reads getStagedFiles(); undo/redo uses setStagedFiles().
//
// A tracked file that was deleted from the working tree can be staged for
// deletion. In the index file (and in undo/redo snapshots) such an entry is
// written as "deleted:<path>"; ':' cannot appear in a Windows file name, so the
// marker can never collide with a real path there.
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace minigit {

constexpr const char* kStagedDeletePrefix = "deleted:";

class StagingArea {
public:
    explicit StagingArea(const std::string& rootDir = ".");

    bool load();        // read .minigit/index (called by the constructor)
    bool save() const;  // write .minigit/index (atomically)

    // stage/unstage only change memory; call save() afterwards.
    bool stage(const std::string& path);           // false if already staged (also cancels a staged deletion)
    bool stageDeletion(const std::string& path);   // false if the deletion is already staged
    bool unstage(const std::string& path);         // false if neither staged nor staged for deletion
    bool isStaged(const std::string& path) const;
    bool isStagedForDeletion(const std::string& path) const;

    // For commit / undo / redo. These save automatically.
    std::vector<std::string> getStagedFiles() const;      // files to snapshot, sorted
    std::vector<std::string> getStagedDeletions() const;  // tracked files to remove, sorted
    std::vector<std::string> getStagedEntries() const;    // files, then "deleted:<path>" entries
    void setStagedFiles(const std::vector<std::string>& snapshot);  // accepts getStagedEntries() output
    void clear();

    std::size_t size() const { return files_.size() + deletions_.size(); }

private:
    std::string indexPath() const;

    std::string root_;
    std::set<std::string> files_;
    std::set<std::string> deletions_;
};

}  // namespace minigit
