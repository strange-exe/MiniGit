#pragma once
// CLI Interface, Argument Parser, Dispatcher, and Output Mapper (Abhinesh's module).
#include <string>

#include "AddCommand.h"
#include "CommitCommand.h"
#include "IgnoreManager.h"
#include "InitCommand.h"
#include "LogCommand.h"

namespace minigit {

class CLI {
public:
    // Looks argv[1] up in the command table (src/CLI.cpp) and runs its handler:
    //   init -> initRepo            add -> addFile / addAll / addVerify
    //   remove -> removeFile        ignore -> ignoreAdd / ignoreRemove / ignoreVerify
    //   commit, undo, redo -> commitRepo / undoCommit / redoCommit
    //   log -> showLog              install, uninstall, help
    // Wrong arguments print that command's usage and return 1.
    static int dispatch(int argc, char** argv, const std::string& root = ".");

    // Result and error mapping / output formatters
    static int printInit(const InitResult& r);
    static int printAdd(const AddResult& r, const std::string& p);
    static int printIgnore(const IgnoreResult& r, const std::string& p);
    static int printCommit(const CommitResult& r);
    static int printLog(const LogResult& r);

    // Helpers
    static void printUsage();
    static bool validateIgnoreInput(const std::string& pattern, std::string& err);
    static void autoInstall();
    static int install();
    static int uninstall();
};

// Free function matching dispatcher specification
int dispatch(int argc, char** argv, const std::string& root = ".");

}  // namespace minigit
