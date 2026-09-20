// TEMPORARY demo CLI so you can try your module before Abhinesh's CLI is ready.
// Abhinesh's real dispatcher should call the same functions.
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AddCommand.h"
#include "IgnoreManager.h"

using namespace minigit;

static int printAdd(const AddResult& r, const std::string& p) {
    switch (r.status) {
        case AddResult::STAGED:
            if (!r.message.empty()) std::cout << r.message << "\n";
            else std::cout << "Staged '" << p << "'\n";
            return 0;
        case AddResult::ALREADY_STAGED:
            if (!r.message.empty()) std::cout << r.message << "\n";
            else std::cout << "'" << p << "' is already staged\n";
            return 0;
        case AddResult::IGNORED:
            std::cout << "'" << p << "' is ignored by '" << r.message << "' (line " << r.line << ")\n";
            return 0;
        case AddResult::NOT_FOUND: std::cout << "'" << p << "' does not exist\n"; return 1;
        case AddResult::UNSTAGED:
            if (r.stagedCount > 1) std::cout << "Unstaged " << r.stagedCount << " files\n";
            else std::cout << "Unstaged '" << p << "'\n";
            return 0;
        case AddResult::NOT_STAGED: std::cout << "'" << p << "' is not staged\n"; return 0;
        case AddResult::IS_STAGED: std::cout << "'" << p << "' is staged\n"; return 0;
        default: std::cout << "error: " << r.message << "\n"; return 1;
    }
}

static int printIgnore(const IgnoreResult& r, const std::string& p) {
    switch (r.status) {
        case IgnoreResult::ADDED: std::cout << "Ignored '" << r.matchedPattern << "' (line " << r.lineNumber << ")\n"; return 0;
        case IgnoreResult::ALREADY_PRESENT: std::cout << "'" << p << "' already ignored at line " << r.lineNumber << "\n"; return 0;
        case IgnoreResult::REMOVED: std::cout << "Removed '" << r.matchedPattern << "' (was line " << r.lineNumber << ")\n"; return 0;
        case IgnoreResult::NOT_PRESENT:
            std::cout << "'" << p << "' is not present in .minigitignore";
            if (!r.message.empty()) std::cout << " (" << r.message << ")";
            std::cout << "\n";
            return 1;
        case IgnoreResult::IGNORED: std::cout << "'" << p << "' is ignored by '" << r.matchedPattern << "' (line " << r.lineNumber << ")\n"; return 0;
        case IgnoreResult::NOT_IGNORED: std::cout << "'" << p << "' is not ignored\n"; return 0;
        default: std::cout << "error: " << r.message << "\n"; return 1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cout << "usage: minigit_demo init | add [-v] <f|.> | remove <f|.> | ignore [-r|-v] <path>\n"; return 1; }
    std::string cmd = argv[1];
    if (cmd == "init") {
        std::filesystem::create_directories(".minigit");
        std::ofstream(".minigit/index", std::ios::app);
        std::ofstream(".minigitignore", std::ios::app);
        std::cout << "Initialized empty minigit repository\n";
        return 0;
    }
    if (cmd == "add") {
        if (argc == 4 && std::string(argv[2]) == "-v") return printAdd(addVerify(argv[3]), argv[3]);
        if (argc == 3) return printAdd(std::string(argv[2]) == "." ? addAll() : addFile(argv[2]), argv[2]);
    } else if (cmd == "remove" && argc == 3) {
        return printAdd(removeFile(argv[2]), argv[2]);
    } else if (cmd == "ignore") {
        if (argc == 4 && std::string(argv[2]) == "-r") return printIgnore(ignoreRemove(argv[3]), argv[3]);
        if (argc == 4 && std::string(argv[2]) == "-v") return printIgnore(ignoreVerify(argv[3]), argv[3]);
        if (argc == 3) return printIgnore(ignoreAdd(argv[2]), argv[2]);
    }
    std::cout << "invalid command\n";
    return 1;
}
