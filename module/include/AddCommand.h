#pragma once
// add / add . / add -v / remove  (Sparsh's commands).
// Functions return a result and never print; the CLI prints.
#include <string>

#include "IgnoreManager.h"
#include "StagingArea.h"

namespace minigit {

struct AddResult {
    enum Status { STAGED, ALREADY_STAGED, IGNORED, NOT_FOUND, UNSTAGED, NOT_STAGED, IS_STAGED, ERROR };
    Status status = ERROR;
    std::string message;      // matched ignore pattern, reason, or summary
    int line = -1;            // line of the ignore rule (when IGNORED)
    int stagedCount = 0;      // newly staged (add . / add <dir>) or unstaged (remove .)
    int alreadyStagedCount = 0;
    int skippedCount = 0;     // ignored items skipped
};

AddResult addFile(const std::string& path, const std::string& root = ".");     // add <file|dir>
AddResult addAll(const std::string& root = ".");                               // add .
AddResult addVerify(const std::string& path, const std::string& root = ".");   // add -v <file>
AddResult removeFile(const std::string& path, const std::string& root = ".");  // remove <file|.>

}  // namespace minigit
