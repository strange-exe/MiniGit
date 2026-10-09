#include "CLI.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#include "PathUtils.h"

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

namespace minigit {

#if !(defined(_WIN32) || defined(__WIN32__) || defined(WIN32))
// Location of the running binary. current_path() is the user's working
// directory, which is usually NOT where the executable lives.
static std::filesystem::path selfExePath() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}
#endif

// Where an ignore rule comes from: a .minigitignore line, or the built-in
// .minigit/ rule, which has no line in the file (line number -1).
static std::string ruleSource(int line) {
    return line > 0 ? "line " + std::to_string(line) : "built-in rule";
}

int CLI::printInit(const InitResult& r) {
    if (r.status == InitResult::ERROR) {
        std::cout << "fatal: " << r.message << "\n";
        return 1;
    }
    std::cout << r.message << "\n";
    return 0;
}

int CLI::printAdd(const AddResult& r, const std::string& p) {
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
            std::cout << "'" << p << "' is ignored by '" << r.message << "' (" << ruleSource(r.line) << ")\n";
            return 0;
        case AddResult::NOT_FOUND:
            std::cout << "'" << p << "' does not exist\n";
            return 1;
        case AddResult::UNSTAGED:
            if (r.stagedCount > 1) std::cout << "Unstaged " << r.stagedCount << " files\n";
            else std::cout << "Unstaged '" << p << "'\n";
            return 0;
        case AddResult::NOT_STAGED:
            std::cout << "'" << p << "' is not staged\n";
            return 0;
        case AddResult::IS_STAGED:
            std::cout << "'" << p << "' is staged\n";
            return 0;
        default:
            std::cout << "error: " << r.message << "\n";
            return 1;
    }
}

int CLI::printIgnore(const IgnoreResult& r, const std::string& p) {
    switch (r.status) {
        case IgnoreResult::ADDED:
            std::cout << "Ignored '" << r.matchedPattern << "' (line " << r.lineNumber << ")\n";
            return 0;
        case IgnoreResult::ALREADY_PRESENT:
            std::cout << "'" << p << "' already ignored at line " << r.lineNumber << "\n";
            return 0;
        case IgnoreResult::REMOVED:
            std::cout << "Removed '" << r.matchedPattern << "' (was line " << r.lineNumber << ")\n";
            return 0;
        case IgnoreResult::NOT_PRESENT:
            std::cout << "'" << p << "' is not present in .minigitignore";
            if (!r.message.empty()) std::cout << " (" << r.message << ")";
            std::cout << "\n";
            return 1;
        case IgnoreResult::IGNORED:
            std::cout << "'" << p << "' is ignored by '" << r.matchedPattern << "' (" << ruleSource(r.lineNumber) << ")\n";
            return 0;
        case IgnoreResult::NOT_IGNORED:
            std::cout << "'" << p << "' is not ignored\n";
            return 0;
        default:
            std::cout << "error: " << r.message << "\n";
            return 1;
    }
}

int CLI::printCommit(const CommitResult& r) {
    if (r.status != CommitResult::SUCCESS) {
        if (!r.errorMessage.empty()) {
            std::cout << "fatal: " << r.errorMessage << "\n";
        } else {
            std::cout << "fatal: commit failed\n";
        }
        return 1;
    }
    if (!r.commitId.empty()) {
        std::string shortId = r.commitId.size() >= 7 ? r.commitId.substr(0, 7) : r.commitId;
        std::cout << "[main " << shortId << "] " << r.message << "\n";
        if (r.filesCommitted > 0) {
            std::cout << r.filesCommitted << " file(s) committed\n";
        }
    } else {
        std::cout << r.message << "\n";
    }
    return 0;
}

int CLI::printLog(const LogResult& r) {
    switch (r.status) {
        case LogResult::OK:
            for (const LogEntry& e : r.entries) {
                std::string shortId = e.commitId.size() >= 7 ? e.commitId.substr(0, 7) : e.commitId;
                std::cout << "commit " << shortId << "\n";
                if (!e.parentId.empty()) {
                    std::string shortParent = e.parentId.size() >= 7 ? e.parentId.substr(0, 7) : e.parentId;
                    std::cout << "parent  " << shortParent << "\n";
                }
                std::cout << "date    " << formatTimestamp(e.timestamp) << "\n"
                          << "\n    " << e.message << "\n"
                          << "    (" << e.fileCount << " file(s))\n\n";
            }
            return 0;
        case LogResult::EMPTY:
            std::cout << "No commits yet\n";
            return 0;
        case LogResult::NOT_A_REPO:
            std::cout << "fatal: not a minigit repository (run 'minigit init')\n";
            return 1;
        default:
            std::cout << "fatal: " << (r.errorMessage.empty() ? "could not read commit history" : r.errorMessage)
                      << "\n";
            return 1;
    }
}

// ---------------------------------------------------------------------------
// Command table. Each row holds a command's name, aliases, usage forms and
// handler, so `minigit help`, per-command usage and dispatch are generated from
// one place and cannot drift apart. A handler returns an exit code, or kUsage
// when its arguments are wrong (the dispatcher then prints that command's usage).
// ---------------------------------------------------------------------------
namespace {

using Args = std::vector<std::string>;
const int kUsage = -1;

struct Form {
    const char* syntax;  // e.g. "add -v <file>"
    const char* description;
};

struct Command {
    const char* name;
    std::vector<std::string> aliases;
    std::vector<Form> forms;
    int (*run)(const Args& args, const std::string& root);
};

bool isFlag(const std::string& s, const char* shortFlag, const char* longFlag) {
    return s == shortFlag || s == longFlag;
}

int runInit(const Args& a, const std::string& root) {
    if (!a.empty()) return kUsage;
    return CLI::printInit(initRepo(root));
}

int runAdd(const Args& a, const std::string& root) {
    if (a.size() == 1) return CLI::printAdd(a[0] == "." ? addAll(root) : addFile(a[0], root), a[0]);
    if (a.size() == 2 && isFlag(a[0], "-v", "--verify")) return CLI::printAdd(addVerify(a[1], root), a[1]);
    return kUsage;
}

int runRemove(const Args& a, const std::string& root) {
    if (a.size() != 1) return kUsage;
    return CLI::printAdd(removeFile(a[0], root), a[0]);
}

int runIgnore(const Args& a, const std::string& root) {
    if (a.empty()) {  // list the rules with their line numbers
        IgnoreManager im(root);
        const auto& lines = im.lines();
        if (lines.empty()) std::cout << "No patterns in .minigitignore\n";
        for (std::size_t i = 0; i < lines.size(); ++i) std::cout << (i + 1) << ": " << lines[i] << "\n";
        return 0;
    }
    bool remove = isFlag(a[0], "-r", "--remove");
    bool verify = isFlag(a[0], "-v", "--verify");
    if ((remove || verify) && a.size() == 1) {
        std::cout << "minigit ignore: option '" << a[0] << "' requires a pattern argument\n";
        return 1;
    }
    if (a.size() != ((remove || verify) ? 2u : 1u)) return kUsage;

    const std::string& pattern = a.back();
    std::string err;
    if (!CLI::validateIgnoreInput(pattern, err)) {
        std::cout << "minigit ignore: " << err << "\n";
        return 1;
    }
    IgnoreResult r = remove ? ignoreRemove(pattern, root) : verify ? ignoreVerify(pattern, root) : ignoreAdd(pattern, root);
    return CLI::printIgnore(r, pattern);
}

int runCommit(const Args& a, const std::string& root) {
    if (a.size() == 1 && a[0] == "undo") return CLI::printCommit(undoCommit(root));
    if (a.size() == 1 && a[0] == "redo") return CLI::printCommit(redoCommit(root));
    if (!a.empty() && isFlag(a[0], "-m", "--message")) {
        if (a.size() == 1) {
            std::cout << "fatal: option '" << a[0] << "' requires a message argument\n";
            return 1;
        }
        return a.size() == 2 ? CLI::printCommit(commitRepo(a[1], root)) : kUsage;
    }
    return a.size() == 1 ? CLI::printCommit(commitRepo(a[0], root)) : kUsage;
}

int runUndo(const Args& a, const std::string& root) {
    return a.empty() ? CLI::printCommit(undoCommit(root)) : kUsage;
}

int runRedo(const Args& a, const std::string& root) {
    return a.empty() ? CLI::printCommit(redoCommit(root)) : kUsage;
}

int runLog(const Args& a, const std::string& root) {
    if (a.empty()) return CLI::printLog(showLog(root, false));
    if (a.size() == 1 && a[0] == "--reverse") return CLI::printLog(showLog(root, true));
    return kUsage;
}

int runInstall(const Args& a, const std::string&) { return a.empty() ? CLI::install() : kUsage; }
int runUninstall(const Args& a, const std::string&) { return a.empty() ? CLI::uninstall() : kUsage; }
int runHelp(const Args& a, const std::string& root);

const std::vector<Command>& commands() {
    static const std::vector<Command> table = {
        {"init", {}, {{"init", "Create an empty MiniGit repository or reinitialize an existing one"}}, runInit},
        {"add", {}, {{"add <file|dir>", "Stage a file, or every non-ignored file in a directory"},
                     {"add .", "Stage every non-ignored file in the repository"},
                     {"add -v <file>", "Show whether a file is staged"}}, runAdd},
        {"remove", {"rm"}, {{"remove <file|dir|.>", "Unstage files (files on disk are never touched)"}}, runRemove},
        {"ignore", {}, {{"ignore", "List the rules in .minigitignore with line numbers"},
                        {"ignore <pattern>", "Add a rule to .minigitignore"},
                        {"ignore -r <pattern>", "Remove a rule from .minigitignore"},
                        {"ignore -v <path>", "Show whether a path is ignored, and by which line"}}, runIgnore},
        {"commit", {}, {{"commit \"<message>\"", "Record the staged files as a new commit"},
                        {"commit -m \"<message>\"", "Same, with an explicit -m flag"},
                        {"commit undo", "Undo the last commit and restore its staged files"},
                        {"commit redo", "Redo the last undone commit"}}, runCommit},
        {"undo", {}, {{"undo", "Shortcut for 'commit undo'"}}, runUndo},
        {"redo", {}, {{"redo", "Shortcut for 'commit redo'"}}, runRedo},
        {"log", {}, {{"log [--reverse]", "Show commit history, newest first (oldest first with --reverse)"}}, runLog},
        {"install", {"--install"}, {{"install", "Copy MiniGit into a folder on your PATH"}}, runInstall},
        {"uninstall", {"--uninstall"}, {{"uninstall", "Remove the installed copy from your PATH"}}, runUninstall},
        {"help", {"--help", "-h"}, {{"help [<command>]", "Show this list, or the usage of one command"}}, runHelp},
    };
    return table;
}

const Command* findCommand(const std::string& name) {
    for (const Command& c : commands()) {
        if (name == c.name) return &c;
        for (const std::string& alias : c.aliases)
            if (name == alias) return &c;
    }
    return nullptr;
}

void printCommandUsage(const Command& c) {
    std::cout << "usage:";
    for (std::size_t i = 0; i < c.forms.size(); ++i) std::cout << (i ? " | " : " ") << "minigit " << c.forms[i].syntax;
    std::cout << "\n";
}

void printForms(const Command& c, std::size_t width) {
    for (const Form& f : c.forms) {
        std::string syntax = f.syntax;
        std::cout << "   " << syntax << std::string(width + 2 - syntax.size(), ' ') << f.description << "\n";
    }
}

int runHelp(const Args& a, const std::string&) {
    if (a.empty()) {
        CLI::printUsage();
        return 0;
    }
    if (a.size() != 1) return kUsage;
    const Command* c = findCommand(a[0]);
    if (!c) {
        std::cout << "minigit help: '" << a[0] << "' is not a minigit command\n";
        return 1;
    }
    printCommandUsage(*c);
    std::cout << "\n";
    std::size_t width = 0;
    for (const Form& f : c->forms) width = std::max(width, std::strlen(f.syntax));
    printForms(*c, width);
    return 0;
}

// Levenshtein edit distance with two rolling rows: O(|a|*|b|) time, O(|b|) space.
std::size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t substitute = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, substitute});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Closest command for a typo ("comit" -> "commit"); "" when nothing is within 2 edits.
std::string suggestCommand(const std::string& typo) {
    std::string best;
    std::size_t bestDistance = 3;
    for (const Command& c : commands()) {
        std::size_t d = editDistance(typo, c.name);
        if (d < bestDistance) {
            bestDistance = d;
            best = c.name;
        }
    }
    return best;
}

}  // namespace

void CLI::printUsage() {
    std::size_t width = 0;
    for (const Command& c : commands())
        for (const Form& f : c.forms) width = std::max(width, std::strlen(f.syntax));

    std::cout << "usage: minigit <command> [<args>]\n\n"
              << "These are common MiniGit commands:\n";
    for (const Command& c : commands()) printForms(c, width);
    std::cout << "\nSee 'minigit help <command>' for the usage of one command.\n";
}

bool CLI::validateIgnoreInput(const std::string& pattern, std::string& err) {
    if (pattern.empty()) {
        err = "empty pattern is not allowed";
        return false;
    }
    std::size_t first = pattern.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        err = "pattern cannot be only whitespace";
        return false;
    }
    // Same rule the add/ignore modules use: no "..", no absolute paths, no drive letters.
    if (isOutsideRepo(normalizePath(pattern))) {
        err = "pattern '" + pattern + "' escapes the repository";
        return false;
    }
    return true;
}

int CLI::install() {
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    char selfPath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    if (len == 0) {
        std::cout << "minigit install: failed to get current executable path\n";
        return 1;
    }
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData) {
        namespace fs = std::filesystem;
        fs::path destDir = fs::path(localAppData) / "Microsoft" / "WindowsApps";
        std::error_code ec;
        fs::create_directories(destDir, ec);
        fs::path destFile = destDir / "minigit.exe";

        if (CopyFileA(selfPath, destFile.string().c_str(), FALSE)) {
            std::cout << "Successfully installed MiniGit!\n"
                      << "  Installed binary to: " << destFile.string() << "\n\n"
                      << "You can now run 'minigit' directly from any folder in PowerShell or Command Prompt!\n";
            return 0;
        }
    }
    std::cout << "minigit install: could not install to WindowsApps directory\n";
    return 1;
#else
    const char* home = std::getenv("HOME");
    if (!home) {
        std::cout << "minigit install: HOME environment variable not found\n";
        return 1;
    }
    namespace fs = std::filesystem;
    fs::path destDir = fs::path(home) / ".local" / "bin";
    std::error_code ec;
    fs::create_directories(destDir, ec);
    fs::path destFile = destDir / "minigit";
    fs::path selfP = selfExePath();
    if (selfP.empty()) {
        std::cout << "minigit install: failed to get current executable path\n";
        return 1;
    }
    fs::copy_file(selfP, destFile, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cout << "minigit install: " << ec.message() << "\n";
        return 1;
    }
    std::cout << "Successfully installed MiniGit to " << destFile.string() << "\n"
              << "You can now run 'minigit' directly from your terminal!\n";
    return 0;
#endif
}

int CLI::uninstall() {
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData) {
        namespace fs = std::filesystem;
        fs::path destFile = fs::path(localAppData) / "Microsoft" / "WindowsApps" / "minigit.exe";
        std::error_code ec;
        if (fs::exists(destFile, ec)) {
            if (DeleteFileA(destFile.string().c_str())) {
                std::cout << "Successfully uninstalled MiniGit from:\n  " << destFile.string() << "\n";
                return 0;
            } else {
                std::cout << "minigit uninstall: failed to delete " << destFile.string() << "\n";
                return 1;
            }
        }
    }
    std::cout << "MiniGit is not currently installed in WindowsApps\n";
    return 0;
#else
    const char* home = std::getenv("HOME");
    if (!home) return 1;
    namespace fs = std::filesystem;
    fs::path destFile = fs::path(home) / ".local" / "bin" / "minigit";
    std::error_code ec;
    if (fs::remove(destFile, ec)) {
        std::cout << "Successfully uninstalled MiniGit\n";
        return 0;
    }
    std::cout << "MiniGit was not installed in ~/.local/bin\n";
    return 0;
#endif
}

// Decides whether an already-installed copy at `installed` should be replaced
// by the running binary `self`. Both paths exist when this is called.
static bool installedCopyIsStale(const std::filesystem::path& self, const std::filesystem::path& installed) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // A rebuilt binary is newer than the installed copy. CopyFileA keeps the source
    // timestamp and copy_file stamps the copy "now", so after one refresh this is false.
    auto selfTime = fs::last_write_time(self, ec);
    if (ec) return false;  // can't tell -> never copy before a normal command
    auto installedTime = fs::last_write_time(installed, ec);
    if (ec) return false;
    return selfTime > installedTime;
}

void CLI::autoInstall() {
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    char selfPath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    if (len == 0) return;

    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (!localAppData) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path selfP = fs::weakly_canonical(fs::path(selfPath), ec);
    fs::path destDir = fs::path(localAppData) / "Microsoft" / "WindowsApps";
    fs::path destP = fs::weakly_canonical(destDir / "minigit.exe", ec);

    std::string s1 = selfP.string();
    std::string s2 = destP.string();
    bool same = (s1.size() == s2.size());
    for (std::size_t i = 0; same && i < s1.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s1[i])) != std::tolower(static_cast<unsigned char>(s2[i]))) {
            same = false;
        }
    }
    if (same) return; // Already running from the installed location

    if (!fs::exists(destP, ec) || installedCopyIsStale(selfP, destP)) {
        fs::create_directories(destDir, ec);
        if (CopyFileA(selfPath, destP.string().c_str(), FALSE)) {
            std::cout << "[MiniGit] Auto-installed to your system PATH: " << destP.string() << "\n"
                      << "          You can now type 'minigit' directly without '.\\'.\n\n";
        }
    }
#else
    const char* home = std::getenv("HOME");
    if (!home) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path selfP = selfExePath();
    if (selfP.empty()) return;
    fs::path destDir = fs::path(home) / ".local" / "bin";
    fs::path destP = destDir / "minigit";

    if (fs::equivalent(selfP, destP, ec)) return;

    if (!fs::exists(destP, ec) || installedCopyIsStale(selfP, destP)) {
        fs::create_directories(destDir, ec);
        fs::copy_file(selfP, destP, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            std::cout << "[MiniGit] Auto-installed to ~/.local/bin/minigit.\n"
                      << "          You can now type 'minigit' directly without './'.\n\n";
        }
    }
#endif
}

int CLI::dispatch(int argc, char** argv, const std::string& root) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string name = argv[1];
    Args args(argv + 2, argv + argc);

    const Command* c = findCommand(name);
    if (!c) {
        std::cout << "minigit: '" << name << "' is not a minigit command. See 'minigit --help'.\n";
        std::string guess = suggestCommand(name);
        if (!guess.empty()) std::cout << "\nThe most similar command is\n    " << guess << "\n";
        return 1;
    }
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {  // minigit <command> --help
        printCommandUsage(*c);
        return 0;
    }
    int rc = c->run(args, root);
    if (rc == kUsage) {
        printCommandUsage(*c);
        return 1;
    }
    return rc;
}

int dispatch(int argc, char** argv, const std::string& root) {
    return CLI::dispatch(argc, argv, root);
}

}  // namespace minigit
