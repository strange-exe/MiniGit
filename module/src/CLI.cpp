#include "CLI.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

namespace minigit {

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
            std::cout << "'" << p << "' is ignored by '" << r.message << "' (line " << r.line << ")\n";
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
            std::cout << "'" << p << "' is ignored by '" << r.matchedPattern << "' (line " << r.lineNumber << ")\n";
            return 0;
        case IgnoreResult::NOT_IGNORED:
            std::cout << "'" << p << "' is not ignored\n";
            return 0;
        default:
            std::cout << "error: " << r.message << "\n";
            return 1;
    }
}

void CLI::printUsage() {
    std::cout << "usage: minigit <command> [<args>]\n\n"
              << "These are common MiniGit commands:\n"
              << "   init                 Create an empty MiniGit repository or reinitialize an existing one\n"
              << "   add <file|dir>       Add file contents to the staging area index\n"
              << "   add .                Add all tracked/unignored repository files to index\n"
              << "   add -v <file>        Verify if file is staged in index\n"
              << "   remove <file|dir|.>  Remove files from the staging area index\n"
              << "   ignore <pattern>     Add a pattern to .minigitignore\n"
              << "   ignore -r <pattern>  Remove a pattern from .minigitignore\n"
              << "   ignore -v <path>     Verify whether a path is ignored and show line number\n"
              << "   install              Install MiniGit globally into system PATH\n"
              << "   uninstall            Uninstall MiniGit from system PATH\n";
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
    std::string s = pattern;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    if (s == ".." || s.rfind("../", 0) == 0 || (s.size() > 1 && s[1] == ':')) {
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
    fs::copy_file(fs::current_path() / "minigit", destFile, fs::copy_options::overwrite_existing, ec);
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

    if (!fs::exists(destP, ec)) {
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
    fs::path selfP = fs::current_path() / "minigit";
    fs::path destDir = fs::path(home) / ".local" / "bin";
    fs::path destP = destDir / "minigit";

    if (fs::equivalent(selfP, destP, ec)) return;

    if (!fs::exists(destP, ec)) {
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
        autoInstall();
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd != "uninstall" && cmd != "--uninstall") {
        autoInstall();
    }

    if (cmd == "init") {
        InitResult r = initRepo(root);
        return printInit(r);
    }

    if (cmd == "install" || cmd == "--install") {
        return install();
    }

    if (cmd == "uninstall" || cmd == "--uninstall") {
        return uninstall();
    }

    if (cmd == "add") {
        if (argc < 3) {
            std::cout << "usage: minigit add <file|dir> | minigit add . | minigit add -v <file>\n";
            return 1;
        }
        if (argc == 4 && (std::string(argv[2]) == "-v" || std::string(argv[2]) == "--verify")) {
            std::string path = argv[3];
            AddResult r = addVerify(path, root);
            return printAdd(r, path);
        }
        if (argc == 3) {
            std::string path = argv[2];
            if (path == ".") {
                AddResult r = addAll(root);
                return printAdd(r, path);
            } else {
                AddResult r = addFile(path, root);
                return printAdd(r, path);
            }
        }
    } else if (cmd == "remove" || cmd == "rm") {
        if (argc < 3) {
            std::cout << "usage: minigit remove <file|dir|.>\n";
            return 1;
        }
        std::string path = argv[2];
        AddResult r = removeFile(path, root);
        return printAdd(r, path);
    } else if (cmd == "ignore") {
        if (argc == 2) {
            IgnoreManager im(root);
            const auto& lines = im.lines();
            if (lines.empty()) {
                std::cout << "No patterns in .minigitignore\n";
            } else {
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    std::cout << (i + 1) << ": " << lines[i] << "\n";
                }
            }
            return 0;
        }
        if (argc == 4 && (std::string(argv[2]) == "-r" || std::string(argv[2]) == "--remove")) {
            std::string pattern = argv[3];
            std::string err;
            if (!validateIgnoreInput(pattern, err)) {
                std::cout << "minigit ignore: " << err << "\n";
                return 1;
            }
            IgnoreResult r = ignoreRemove(pattern, root);
            return printIgnore(r, pattern);
        }
        if (argc == 4 && (std::string(argv[2]) == "-v" || std::string(argv[2]) == "--verify")) {
            std::string pattern = argv[3];
            std::string err;
            if (!validateIgnoreInput(pattern, err)) {
                std::cout << "minigit ignore: " << err << "\n";
                return 1;
            }
            IgnoreResult r = ignoreVerify(pattern, root);
            return printIgnore(r, pattern);
        }
        if (argc == 3) {
            std::string pattern = argv[2];
            if (pattern == "-r" || pattern == "-v") {
                std::cout << "minigit ignore: option '" << pattern << "' requires a pattern argument\n";
                return 1;
            }
            std::string err;
            if (!validateIgnoreInput(pattern, err)) {
                std::cout << "minigit ignore: " << err << "\n";
                return 1;
            }
            IgnoreResult r = ignoreAdd(pattern, root);
            return printIgnore(r, pattern);
        }
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }

    std::cout << "minigit: '" << cmd << "' is not a minigit command. See 'minigit --help'.\n";
    return 1;
}

int dispatch(int argc, char** argv, const std::string& root) {
    return CLI::dispatch(argc, argv, root);
}

}  // namespace minigit
