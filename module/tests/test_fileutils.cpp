// Unit tests for FileUtils.h: writeFileAtomic replaces a file completely or not at all.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "FileUtils.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static const std::string kDir = "test_fileutils_tmp";

static std::string slurp(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void put(const fs::path& p, const std::string& text) {
    std::ofstream(p.string(), std::ios::binary) << text;
}

static std::size_t entriesIn(const fs::path& dir) {
    std::size_t n = 0;
    for (auto it = fs::directory_iterator(dir); it != fs::directory_iterator(); ++it) ++n;
    return n;
}

static void freshDir() {
    std::error_code ec;
    fs::remove_all(kDir, ec);
    fs::create_directories(kDir, ec);
}

static void testCreatesFile() {
    freshDir();
    const fs::path target = fs::path(kDir) / "ref";
    CHECK(writeFileAtomic(target.string(), "abc123\n"));
    CHECK(slurp(target) == "abc123\n");
    CHECK(entriesIn(kDir) == 1);  // the temporary sibling is gone
}

static void testReplacesWholeContent() {
    freshDir();
    const fs::path target = fs::path(kDir) / "index";
    put(target, "a very long previous content\nwith several lines\n");
    CHECK(writeFileAtomic(target.string(), "short"));
    CHECK(slurp(target) == "short");  // no bytes of the old file remain
    CHECK(entriesIn(kDir) == 1);
}

static void testBinaryAndEmptyContent() {
    freshDir();
    const fs::path target = fs::path(kDir) / "blob";
    const std::string bytes("a\0b\r\nc\xff", 7);
    CHECK(writeFileAtomic(target.string(), bytes));
    CHECK(slurp(target) == bytes);  // written as is: no newline translation, NUL kept

    CHECK(writeFileAtomic(target.string(), ""));
    CHECK(fs::exists(target) && fs::file_size(target) == 0);
}

static void testStaleTempFileDoesNotMatter() {
    freshDir();
    const fs::path target = fs::path(kDir) / "HEAD";
    put(fs::path(kDir) / ".HEAD.tmp", "left over from a crash");
    CHECK(writeFileAtomic(target.string(), "ref: refs/heads/main\n"));
    CHECK(slurp(target) == "ref: refs/heads/main\n");
    CHECK(entriesIn(kDir) == 1);
}

static void testFailureLeavesNothingBehind() {
    freshDir();

    // Folder does not exist: fails, creates nothing.
    CHECK(!writeFileAtomic((fs::path(kDir) / "missing" / "file").string(), "x"));
    CHECK(!fs::exists(fs::path(kDir) / "missing"));
    CHECK(entriesIn(kDir) == 0);

    // The target is a directory: the rename fails, the old state is kept and the temp file removed.
    const fs::path dirTarget = fs::path(kDir) / "occupied";
    fs::create_directories(dirTarget / "child");
    CHECK(!writeFileAtomic(dirTarget.string(), "x"));
    CHECK(fs::is_directory(dirTarget / "child"));
    CHECK(entriesIn(kDir) == 1);  // only "occupied"
}

int main() {
    testCreatesFile();
    testReplacesWholeContent();
    testBinaryAndEmptyContent();
    testStaleTempFileDoesNotMatter();
    testFailureLeavesNothingBehind();

    std::error_code ec;
    fs::remove_all(kDir, ec);
    std::cout << "test_fileutils: all tests passed\n";
    return 0;
}
