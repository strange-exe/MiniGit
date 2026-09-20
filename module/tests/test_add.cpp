#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "AddCommand.h"
#include "IgnoreManager.h"
#include "StagingArea.h"

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
    std::ofstream(p, std::ios::binary) << s;
}

static bool has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

int main() {
    fs::path rootP = fs::temp_directory_path() / "minigit_test_repo";
    fs::remove_all(rootP);
    fs::create_directories(rootP / ".minigit");
    const std::string R = rootP.string();

    for (const char* f : {"main.cpp", "app.exe", "notes.txt", "exe.txt", "secret.txt", "build/x.o",
                          "build/sub/y.o", "buildings/a.cpp", "logs/debug.txt", "src/a.cpp"})
        writeFile(rootP / f, "data");

    // ---- add <file> ----
    CHECK(addFile("ghost.cpp", R).status == AddResult::NOT_FOUND);
    CHECK(addFile("main.cpp", R).status == AddResult::STAGED);
    CHECK(addFile("./main.cpp", R).status == AddResult::ALREADY_STAGED);  // duplicate, normalized
    CHECK(addFile("..\\evil.txt", R).status == AddResult::ERROR);

    // ---- ignore add ----
    IgnoreResult r = ignoreAdd("*.exe", R);
    CHECK(r.status == IgnoreResult::ADDED && r.lineNumber == 1);
    CHECK(ignoreAdd("*.exe", R).status == IgnoreResult::ALREADY_PRESENT);
    r = ignoreAdd("build", R);  // existing dir -> "build/"
    CHECK(r.status == IgnoreResult::ADDED && r.matchedPattern == "build/" && r.lineNumber == 2);
    CHECK(ignoreAdd("secret.txt", R).lineNumber == 3);
    CHECK(ignoreAdd("logs\\debug.txt", R).matchedPattern == "logs/debug.txt");

    // ---- ignore verify ----
    r = ignoreVerify("build/sub/y.o", R);
    CHECK(r.status == IgnoreResult::IGNORED && r.matchedPattern == "build/" && r.lineNumber == 2);
    CHECK(ignoreVerify("buildings/a.cpp", R).status == IgnoreResult::NOT_IGNORED);
    CHECK(ignoreVerify("exe.txt", R).status == IgnoreResult::NOT_IGNORED);
    CHECK(ignoreVerify("dir/b.exe", R).lineNumber == 1);
    CHECK(ignoreVerify(".minigit/index", R).status == IgnoreResult::IGNORED);

    // ---- add respects ignore ----
    AddResult a = addFile("app.exe", R);
    CHECK(a.status == AddResult::IGNORED && a.line == 1 && a.message == "*.exe");
    CHECK(addFile("build/x.o", R).status == AddResult::IGNORED);
    CHECK(addFile("secret.txt", R).status == AddResult::IGNORED);
    CHECK(addFile("exe.txt", R).status == AddResult::STAGED);
    CHECK(addFile("buildings/a.cpp", R).status == AddResult::STAGED);

    // ---- add -v ----
    CHECK(addVerify("main.cpp", R).status == AddResult::IS_STAGED);
    a = addVerify("app.exe", R);
    CHECK(a.status == AddResult::IGNORED && a.line == 1);
    CHECK(addVerify("notes.txt", R).status == AddResult::NOT_STAGED);
    CHECK(addVerify("ghost.cpp", R).status == AddResult::NOT_FOUND);

    // ---- add . ----
    a = addAll(R);
    CHECK(a.status == AddResult::STAGED);
    StagingArea sa(R);  // reloads from disk => persistence works
    auto staged = sa.getStagedFiles();
    CHECK(has(staged, "main.cpp") && has(staged, "notes.txt") && has(staged, "src/a.cpp"));
    CHECK(!has(staged, "app.exe") && !has(staged, "build/x.o") && !has(staged, "build/sub/y.o"));
    CHECK(!has(staged, "secret.txt") && !has(staged, "logs/debug.txt"));
    for (const auto& s : staged) CHECK(s.rfind(".minigit/", 0) != 0);
    CHECK(addAll(R).status == AddResult::ALREADY_STAGED);  // second run adds nothing
    CHECK(std::count(staged.begin(), staged.end(), "main.cpp") == 1);

    // ---- remove (unstage only) ----
    CHECK(removeFile("main.cpp", R).status == AddResult::UNSTAGED);
    CHECK(fs::exists(rootP / "main.cpp"));  // disk untouched
    CHECK(removeFile("main.cpp", R).status == AddResult::NOT_STAGED);
    CHECK(addFile("main.cpp", R).status == AddResult::STAGED);

    // ---- ignore remove ----
    r = ignoreRemove("build/x.o", R);
    CHECK(r.status == IgnoreResult::NOT_PRESENT && r.matchedPattern == "build/" && !r.message.empty());
    r = ignoreRemove("*.exe", R);
    CHECK(r.status == IgnoreResult::REMOVED && r.lineNumber == 1);
    CHECK(ignoreRemove("*.exe", R).status == IgnoreResult::NOT_PRESENT);
    CHECK(ignoreVerify("build/x.o", R).lineNumber == 1);  // lines shifted up
    CHECK(addFile("app.exe", R).status == AddResult::STAGED);  // addable again
    CHECK(ignoreRemove("build", R).status == IgnoreResult::REMOVED);
    CHECK(ignoreVerify("build/x.o", R).status == IgnoreResult::NOT_IGNORED);

    // ---- CRLF, comments, blank lines count for line numbers ----
    writeFile(rootP / ".minigitignore", "# comment\r\n\r\n*.log\r\n");
    r = ignoreVerify("run.log", R);
    CHECK(r.status == IgnoreResult::IGNORED && r.lineNumber == 3);

    // ---- snapshots for undo/redo ----
    StagingArea sb(R);
    auto snap = sb.getStagedFiles();
    sb.clear();
    CHECK(StagingArea(R).size() == 0);
    sb.setStagedFiles(snap);
    CHECK(StagingArea(R).getStagedFiles() == snap);
    CHECK(removeFile(".", R).status == AddResult::UNSTAGED);
    CHECK(StagingArea(R).size() == 0);

    // ---- not a repo ----
    fs::path other = fs::temp_directory_path() / "minigit_not_repo";
    fs::create_directories(other);
    CHECK(addFile("x", other.string()).status == AddResult::ERROR);

    fs::remove_all(rootP);
    fs::remove_all(other);
    std::cout << "test_add: all tests passed\n";
    return 0;
}
