#pragma once
// branch / switch (Snehil's Phase 3 module).
//   branch              list branches (current one marked)
//   branch <name>       create a branch at the current commit
//   switch <name>       move HEAD to a branch and restore its files
//   switch -c <name>    create a branch at the current commit and switch to it
//
// A branch is one file, .minigit/refs/heads/<name>, holding a commit id.
// HEAD is "ref: refs/heads/<name>" while on a branch, so `commit` (which already
// follows HEAD) advances the right branch with no change to the commit module.
// Returns plain results; the CLI prints.
#include <string>
#include <vector>

namespace minigit {

struct BranchResult {
    enum Status {
        CREATED,
        LISTED,
        SWITCHED,
        ALREADY_ON,
        NOT_A_REPO,
        INVALID_NAME,
        ALREADY_EXISTS,
        NOT_FOUND,
        NO_COMMITS,
        UNSAVED_CHANGES,
        ERROR
    };

    Status status = ERROR;
    std::string name;      // branch created or switched to
    std::string commitId;  // commit the branch points to
    std::string current;   // LISTED: current branch, "" when HEAD is detached
    std::string headCommitId;
    std::vector<std::string> branches;       // LISTED: sorted names
    std::vector<std::string> blockingFiles;  // UNSAVED_CHANGES
    int filesRestored = 0;
    int filesRemoved = 0;
    std::string warning;  // e.g. leaving a detached commit that no branch reaches
    std::string errorMessage;
};

BranchResult showBranches(const std::string& root = ".");
BranchResult createBranch(const std::string& name, const std::string& root = ".");
BranchResult switchBranch(const std::string& name, const std::string& root = ".", bool create = false);

}  // namespace minigit
