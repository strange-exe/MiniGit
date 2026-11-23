// Unit tests for PathUtils.h: path normalization and the checks that keep every
// path inside the working tree. Header-only code, no disk access.
#include <cstdlib>
#include <iostream>

#include "PathUtils.h"

using namespace minigit;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static void testReverseString() {
    CHECK(reverseString("") == "");
    CHECK(reverseString("a") == "a");
    CHECK(reverseString("*.exe") == "exe.*");
    CHECK(reverseString(reverseString("build/out.o")) == "build/out.o");
}

static void testNormalizeSeparators() {
    // Every spelling of the same file gives one result.
    CHECK(normalizePath("src/a.txt") == "src/a.txt");
    CHECK(normalizePath("./src/a.txt") == "src/a.txt");
    CHECK(normalizePath(".\\src\\a.txt") == "src/a.txt");
    CHECK(normalizePath("src\\sub\\a.txt") == "src/sub/a.txt");
    CHECK(normalizePath("src//a.txt") == "src/a.txt");
    CHECK(normalizePath("src/./a.txt") == "src/a.txt");
}

static void testNormalizeEdges() {
    CHECK(normalizePath("") == "");
    CHECK(normalizePath(".") == ".");
    CHECK(normalizePath("./") == ".");
    CHECK(normalizePath("a/b/../c.txt") == "a/c.txt");
    CHECK(normalizePath("a/../b") == "b");
}

static void testNormalizeKeepsTrailingSlash() {
    // A trailing '/' marks a directory rule and must survive.
    CHECK(normalizePath("build/") == "build/");
    CHECK(normalizePath("build\\") == "build/");
    CHECK(normalizePath("./build/") == "build/");
    CHECK(normalizePath("build") == "build");
}

static void testIsOutsideRepo() {
    CHECK(isOutsideRepo(""));
    CHECK(isOutsideRepo(".."));
    CHECK(isOutsideRepo("../x"));
    CHECK(isOutsideRepo("../../x"));
    CHECK(isOutsideRepo("/etc/passwd"));
    CHECK(isOutsideRepo("C:/Windows"));
    CHECK(isOutsideRepo("c:x"));

    CHECK(!isOutsideRepo("src/a.txt"));
    CHECK(!isOutsideRepo("."));
    CHECK(!isOutsideRepo("..hidden"));    // starts with dots but is an ordinary name
    CHECK(!isOutsideRepo("a..b/c"));
    CHECK(!isOutsideRepo("x"));           // a single character is not a drive letter

    // After normalization, a path that climbs out is caught.
    CHECK(isOutsideRepo(normalizePath("a/../../x")));
    CHECK(isOutsideRepo(normalizePath("..\\x")));
    CHECK(!isOutsideRepo(normalizePath("a/../x")));
}

static void testIsSafeRepoPath() {
    CHECK(isSafeRepoPath("a.txt"));
    CHECK(isSafeRepoPath("src/a.txt"));
    CHECK(isSafeRepoPath("a/b/../c.txt"));
    CHECK(isSafeRepoPath(".gitkeep"));
    CHECK(isSafeRepoPath(".minigitignore"));  // the ignore file itself is an ordinary file

    CHECK(!isSafeRepoPath(""));
    CHECK(!isSafeRepoPath("."));
    CHECK(!isSafeRepoPath(".."));
    CHECK(!isSafeRepoPath("../x"));
    CHECK(!isSafeRepoPath("a/../../x"));
    CHECK(!isSafeRepoPath("/abs/path"));
    CHECK(!isSafeRepoPath("C:\\x"));

    // Repository internals can never be a tracked path.
    CHECK(!isSafeRepoPath(".minigit"));
    CHECK(!isSafeRepoPath(".minigit/HEAD"));
    CHECK(!isSafeRepoPath(".minigit\\objects\\x"));
    CHECK(!isSafeRepoPath("./.minigit/index"));
    CHECK(!isSafeRepoPath("a/../.minigit/HEAD"));
}

int main() {
    testReverseString();
    testNormalizeSeparators();
    testNormalizeEdges();
    testNormalizeKeepsTrailingSlash();
    testIsOutsideRepo();
    testIsSafeRepoPath();
    std::cout << "test_pathutils: all tests passed\n";
    return 0;
}
