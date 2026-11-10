// TEMPORARY demo so you can try `log` before it's wired into Abhinesh's CLI.
// Usage: minigit_log_demo log [--reverse] [path-to-repo]
#include <iostream>

#include "LogCommand.h"

using namespace minigit;

static void printEntry(const LogEntry& e) {
    std::string shortId = e.commitId.size() >= 7 ? e.commitId.substr(0, 7) : e.commitId;
    std::cout << "commit " << shortId << "\n";
    if (!e.parentId.empty()) {
        std::string shortParent = e.parentId.size() >= 7 ? e.parentId.substr(0, 7) : e.parentId;
        std::cout << "parent  " << shortParent << "\n";
    }
    std::cout << "date    " << formatTimestamp(e.timestamp) << "\n";
    std::cout << "\n    " << e.message << "\n";
    std::cout << "    (" << e.fileCount << " file(s))\n\n";
}

int main(int argc, char** argv) {
    bool reverse = false;
    std::string root = ".";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--reverse") reverse = true;
        else if (a != "log") root = a;
    }
    LogResult r = showLog(root, reverse);
    switch (r.status) {
        case LogResult::NOT_A_REPO:
            std::cout << "fatal: " << r.errorMessage << "\n";
            return 1;
        case LogResult::EMPTY:
            std::cout << r.errorMessage << "\n";
            return 0;
        case LogResult::ERROR:
            std::cout << "fatal: " << r.errorMessage << "\n";
            return 1;
        case LogResult::OK:
            for (const auto& e : r.entries) printEntry(e);
            return 0;
    }
    return 1;
}
