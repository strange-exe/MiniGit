// Integration test: real repo, real commits (via Krish's commitRepo), then
// verify `minigit log` and `minigit log --reverse` read them back correctly.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "AddCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "InitCommand.h"
#include "LogCommand.h"

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

int main() {
    fs::path rootP = fs::temp_directory_path() / "minigit_test_log_repo";
    fs::remove_all(rootP);
    fs::create_directories(rootP);
    const std::string R = rootP.string();

    // log before init: not a repo
    LogResult lr = showLog(R);
    CHECK(lr.status == LogResult::NOT_A_REPO);

    CHECK(initRepo(R).status == InitResult::INITIALIZED);

    // log on a fresh, empty repo
    lr = showLog(R);
    CHECK(lr.status == LogResult::EMPTY);

    // three commits, in order
    writeFile(rootP / "a.txt", "1");
    CHECK(addFile("a.txt", R).status == AddResult::STAGED);
    CommitResult c1 = commitRepo("first commit", R);
    CHECK(c1.status == CommitResult::SUCCESS);

    writeFile(rootP / "b.txt", "2");
    CHECK(addFile("b.txt", R).status == AddResult::STAGED);
    CommitResult c2 = commitRepo("second commit", R);
    CHECK(c2.status == CommitResult::SUCCESS);
    CHECK(c2.parentId == c1.commitId);

    writeFile(rootP / "c.txt", "3");
    CHECK(addFile("c.txt", R).status == AddResult::STAGED);
    CommitResult c3 = commitRepo("third commit", R);
    CHECK(c3.status == CommitResult::SUCCESS);
    CHECK(c3.parentId == c2.commitId);

    // default order: newest first
    lr = showLog(R);
    CHECK(lr.status == LogResult::OK);
    CHECK(lr.entries.size() == 3);
    CHECK(lr.entries[0].commitId == c3.commitId && lr.entries[0].message == "third commit");
    CHECK(lr.entries[1].commitId == c2.commitId && lr.entries[1].message == "second commit");
    CHECK(lr.entries[2].commitId == c1.commitId && lr.entries[2].message == "first commit");
    CHECK(lr.entries[2].parentId.empty());          // first commit has no parent
    CHECK(lr.entries[0].parentId == c2.commitId);
    CHECK(lr.entries[0].fileCount == 1);

    // reverse order: oldest first
    LogResult rev = showLog(R, true);
    CHECK(rev.status == LogResult::OK);
    CHECK(rev.entries.size() == 3);
    CHECK(rev.entries[0].commitId == c1.commitId);
    CHECK(rev.entries[1].commitId == c2.commitId);
    CHECK(rev.entries[2].commitId == c3.commitId);

    // forward and reverse are exact mirrors of each other
    CHECK(lr.entries[0].commitId == rev.entries[2].commitId);
    CHECK(lr.entries[1].commitId == rev.entries[1].commitId);
    CHECK(lr.entries[2].commitId == rev.entries[0].commitId);

    // timestamp formatting: numeric string -> readable string, non-numeric passes through
    CHECK(formatTimestamp("0") == "1970-01-01 00:00:00" || formatTimestamp("0").find('-') != std::string::npos);
    CHECK(formatTimestamp("not-a-number") == "not-a-number");

    // log after undo: HEAD moves back, so log should show only the first two commits
    CommitResult u = undoCommit(R);
    CHECK(u.status == CommitResult::SUCCESS);
    lr = showLog(R);
    CHECK(lr.entries.size() == 2);
    CHECK(lr.entries[0].commitId == c2.commitId);

    // `minigit log` is reachable through the CLI dispatcher
    {
        std::stringstream out;
        std::streambuf* old = std::cout.rdbuf(out.rdbuf());
        const char* logArgs[] = {"minigit", "log"};
        int rc = CLI::dispatch(2, const_cast<char**>(logArgs), R);
        const char* revArgs[] = {"minigit", "log", "--reverse"};
        int rcRev = CLI::dispatch(3, const_cast<char**>(revArgs), R);
        const char* badArgs[] = {"minigit", "log", "--bogus"};
        int rcBad = CLI::dispatch(3, const_cast<char**>(badArgs), R);
        std::cout.rdbuf(old);

        std::string s = out.str();
        CHECK(rc == 0 && rcRev == 0 && rcBad == 1);
        CHECK(s.find("commit " + c2.commitId.substr(0, 7)) != std::string::npos);
        CHECK(s.find("second commit") != std::string::npos);
        CHECK(s.find("is not a minigit command") == std::string::npos);
        CHECK(s.find("usage: minigit log [--reverse]") != std::string::npos);
    }

    fs::remove_all(rootP);
    std::cout << "test_log: all tests passed\n";
    return 0;
}
