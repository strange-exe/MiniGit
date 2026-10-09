#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "CLI.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p.string(), std::ios::binary) << s;
}

// Helper to capture stdout from dispatch calls
static std::string runDispatch(const std::vector<std::string>& args, const std::string& root, int& exitCode) {
    std::vector<char*> argv;
    for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }

    std::stringstream buffer;
    std::streambuf* oldCout = std::cout.rdbuf(buffer.rdbuf());

    exitCode = CLI::dispatch(static_cast<int>(argv.size()), argv.data(), root);

    std::cout.rdbuf(oldCout);
    return buffer.str();
}

int main() {
    fs::path tempRoot = fs::temp_directory_path() / "minigit_cli_test";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);
    std::string R = tempRoot.string();

    int code = 0;
    std::string out;

    // 1. minigit init
    out = runDispatch({"minigit", "init"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Initialized empty MiniGit repository") != std::string::npos);

    // Reinit
    out = runDispatch({"minigit", "init"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Reinitialized existing MiniGit repository") != std::string::npos);

    // 2. Prepare test files
    writeFile(tempRoot / "main.cpp", "int main() {}");
    writeFile(tempRoot / "secret.env", "KEY=123");
    writeFile(tempRoot / "logs" / "app.log", "debug");
    writeFile(tempRoot / "src" / "util.cpp", "void foo() {}");

    // 3. minigit ignore <pattern>
    out = runDispatch({"minigit", "ignore", "secret.env"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Ignored 'secret.env' (line 1)") != std::string::npos);

    out = runDispatch({"minigit", "ignore", "*.log"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Ignored '*.log' (line 2)") != std::string::npos);

    // Duplicate ignore
    out = runDispatch({"minigit", "ignore", "secret.env"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("already ignored at line 1") != std::string::npos);

    // 4. minigit ignore -v <path>
    out = runDispatch({"minigit", "ignore", "-v", "secret.env"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("ignored by 'secret.env' (line 1)") != std::string::npos);

    out = runDispatch({"minigit", "ignore", "-v", "logs/app.log"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("ignored by '*.log' (line 2)") != std::string::npos);

    out = runDispatch({"minigit", "ignore", "-v", "main.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("is not ignored") != std::string::npos);

    // 5. minigit ignore -r <pattern>
    out = runDispatch({"minigit", "ignore", "-r", "secret.env"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Removed 'secret.env' (was line 1)") != std::string::npos);

    // Removing when not present -> reports "not present"
    out = runDispatch({"minigit", "ignore", "-r", "nonexistent.file"}, R, code);
    CHECK(code == 1);
    CHECK(out.find("is not present in .minigitignore") != std::string::npos);

    // Re-ignore secret.env for staging tests
    runDispatch({"minigit", "ignore", "secret.env"}, R, code);

    // 6. minigit add <file>
    out = runDispatch({"minigit", "add", "main.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Staged 'main.cpp'") != std::string::npos);

    // Add ignored file -> blocked
    out = runDispatch({"minigit", "add", "secret.env"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("is ignored by 'secret.env'") != std::string::npos);

    // 7. minigit add -v <file> -> tests IS_STAGED print case!
    out = runDispatch({"minigit", "add", "-v", "main.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("'main.cpp' is staged") != std::string::npos);

    out = runDispatch({"minigit", "add", "-v", "src/util.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("'src/util.cpp' is not staged") != std::string::npos);

    // 8. minigit add .
    out = runDispatch({"minigit", "add", "."}, R, code);
    CHECK(code == 0);
    CHECK(out.find("staged") != std::string::npos);

    // Verify src/util.cpp is now staged
    out = runDispatch({"minigit", "add", "-v", "src/util.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("'src/util.cpp' is staged") != std::string::npos);

    // 9. minigit remove <file>
    out = runDispatch({"minigit", "remove", "main.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("Unstaged 'main.cpp'") != std::string::npos);

    // Verify main.cpp is not staged anymore
    out = runDispatch({"minigit", "add", "-v", "main.cpp"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("'main.cpp' is not staged") != std::string::npos);

    // 10. Malformed argument lists print the command's own usage, not "not a minigit command"
    out = runDispatch({"minigit", "add", "a.txt", "b.txt"}, R, code);
    CHECK(code == 1);
    CHECK(out.find("usage: minigit add") != std::string::npos);
    CHECK(out.find("is not a minigit command") == std::string::npos);

    out = runDispatch({"minigit", "ignore", "-x", "foo"}, R, code);
    CHECK(code == 1);
    CHECK(out.find("usage: minigit ignore") != std::string::npos);

    // 11. Patterns escaping the repository are rejected (absolute, "..", drive letter)
    for (const char* bad : {"/etc/passwd", "../x", "a/../../x", "C:/x"}) {
        out = runDispatch({"minigit", "ignore", bad}, R, code);
        CHECK(code == 1);
        CHECK(out.find("escapes the repository") != std::string::npos);
    }

    // 12. Help is generated from the command table: every command is listed
    out = runDispatch({"minigit", "help"}, R, code);
    CHECK(code == 0);
    for (const char* form : {"init", "add -v <file>", "remove <file|dir|.>", "ignore -r <pattern>",
                             "commit -m \"<message>\"", "undo", "redo", "log [--reverse]", "uninstall"})
        CHECK(out.find(std::string("   ") + form) != std::string::npos);
    CHECK(runDispatch({"minigit", "--help"}, R, code) == out && code == 0);

    // 13. Per-command help, without running the command
    out = runDispatch({"minigit", "help", "commit"}, R, code);
    CHECK(code == 0);
    CHECK(out.find("usage: minigit commit \"<message>\" | minigit commit -m \"<message>\"") != std::string::npos);
    out = runDispatch({"minigit", "commit", "--help"}, R, code);
    CHECK(code == 0 && out.find("usage: minigit commit") != std::string::npos);
    CHECK(out.find("committed") == std::string::npos);
    out = runDispatch({"minigit", "help", "nope"}, R, code);
    CHECK(code == 1 && out.find("is not a minigit command") != std::string::npos);

    // 14. Typos suggest the closest command (edit distance <= 2)
    out = runDispatch({"minigit", "comit"}, R, code);
    CHECK(code == 1);
    CHECK(out.find("The most similar command is\n    commit") != std::string::npos);
    out = runDispatch({"minigit", "xyzzy"}, R, code);
    CHECK(out.find("most similar") == std::string::npos);

    // 15. Extra arguments are rejected with the command's usage instead of ignored
    out = runDispatch({"minigit", "remove", "a.txt", "b.txt"}, R, code);
    CHECK(code == 1 && out.find("usage: minigit remove <file|dir|.>") != std::string::npos);
    out = runDispatch({"minigit", "init", "extra"}, R, code);
    CHECK(code == 1 && out.find("usage: minigit init") != std::string::npos);
    out = runDispatch({"minigit", "commit", "msg", "extra"}, R, code);
    CHECK(code == 1 && out.find("usage: minigit commit") != std::string::npos);

    // 16. Long flags without a value are errors, not patterns
    out = runDispatch({"minigit", "ignore", "--remove"}, R, code);
    CHECK(code == 1 && out.find("requires a pattern argument") != std::string::npos);
    out = runDispatch({"minigit", "ignore"}, R, code);
    CHECK(out.find("--remove") == std::string::npos);

    // 17. The built-in .minigit/ rule has no line in .minigitignore: say so instead of "(line -1)"
    out = runDispatch({"minigit", "ignore", "-v", ".minigit"}, R, code);
    CHECK(code == 0 && out.find("is ignored by '.minigit/' (built-in rule)") != std::string::npos);
    out = runDispatch({"minigit", "add", ".minigit"}, R, code);
    CHECK(out.find("(built-in rule)") != std::string::npos && out.find("line -1") == std::string::npos);

    // 18. Clean up
    fs::remove_all(tempRoot);
    std::cout << "test_cli: all tests passed\n";
    return 0;
}
