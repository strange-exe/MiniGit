#pragma once
// Shared read/write helpers for HEAD, branch references and commit objects
// (Snehil's Phase 3 modules: checkout, branch/switch, commit graph).
//
// This file is also the single "seam" to storage. Checkout and switch rebuild
// complete snapshots through effectiveTree(), which reads commit metadata and
// individual snapshots through the helpers below. When tree objects / packfile
// storage is enabled, the object-reading helpers can be adapted without
// changing the restore or branch modules.
//
// Metadata format read here (Krish's format, plus one optional line):
//   parent <id|none>      first parent
//   parent2 <id>          second parent of a merge commit (written by merge)
#include <map>
#include <string>
#include <vector>

namespace minigit {
namespace repo {

// ---- HEAD and branch references -------------------------------------------
// HEAD holds either "ref: refs/heads/<name>" (on a branch) or a raw commit id
// (detached).
std::string readHeadRaw(const std::string& root);                       // first line, trimmed
bool writeHeadRaw(const std::string& root, const std::string& raw);     // writes raw + "\n"
bool isDetached(const std::string& root);
std::string currentBranch(const std::string& root);                     // "" when detached
std::string headCommitId(const std::string& root);                      // "" when no commit yet

bool isValidBranchName(const std::string& name, std::string& err);
bool branchFileExists(const std::string& root, const std::string& name);
std::string readBranch(const std::string& root, const std::string& name);  // "" if missing/empty
bool writeBranch(const std::string& root, const std::string& name, const std::string& commitId);
std::vector<std::string> listBranchNames(const std::string& root);         // sorted

// ---- Commit objects (read side) ----------------------------------------------
struct CommitMeta {
    std::string id;
    std::vector<std::string> parents;  // 0, 1 or 2 ids; parents[0] is the first parent
    std::string message;
    std::string timestamp;
    std::string author;
    std::vector<std::string> files;  // paths in this commit's snapshot
};

// Validates the object id, required metadata fields, parent ids, file count,
// relative file paths and duplicate paths before returning a commit record.
bool readCommitMeta(const std::string& root, const std::string& id, CommitMeta& out);
// Reads one file of a commit's snapshot (binary-safe). False if it cannot be read.
bool readSnapshotFile(const std::string& root, const std::string& id, const std::string& relPath,
                      std::string& content);
std::vector<std::string> listCommitIds(const std::string& root);

// Reconstructs the complete file tree visible at commitId. Commits store only
// their changed/staged files, so this overlays each commit's snapshots along
// the first-parent chain, oldest to newest. A merge commit must therefore record
// every merged result path that differs from its first parent. On failure, `tree`
// is cleared and `errorMessage` explains the missing/corrupt metadata or snapshot.
bool effectiveTree(const std::string& root, const std::string& commitId,
                   std::map<std::string, std::string>& tree,
                   std::string* errorMessage = nullptr);

enum class IdLookup { FOUND, NOT_FOUND, AMBIGUOUS, TOO_SHORT };
// Accepts a full 40-char id or a unique prefix of at least 4 characters.
IdLookup resolveCommitId(const std::string& root, const std::string& text, std::string& fullId);

std::string shortId(const std::string& id);  // first 7 characters

}  // namespace repo
}  // namespace minigit
