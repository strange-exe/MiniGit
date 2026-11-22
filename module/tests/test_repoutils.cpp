// Unit tests for the shared helpers in RepoUtils.h: HEAD handling, commit metadata
// parsing, commit id resolution and the parent-chain walk. Repositories are built
// by hand in a throw-away folder, so no command code is involved.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "RepoUtils.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static const std::string kRoot = "test_repoutils_tmp";

static const std::string A(40, 'a');
static const std::string B(40, 'b');
static const std::string C(40, 'c');
static const std::string D(40, 'd');
static const std::string MISSING(40, 'f');

static void put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p.string(), std::ios::binary) << text;
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void freshRepo() {
    std::error_code ec;
    fs::remove_all(kRoot, ec);
    fs::create_directories(fs::path(kRoot) / ".minigit" / "objects", ec);
    fs::create_directories(fs::path(kRoot) / ".minigit" / "refs" / "heads", ec);
}

static fs::path gitFile(const std::string& rel) { return fs::path(kRoot) / ".minigit" / rel; }

// A complete commit: metadata plus one snapshot file per entry of `files`.
static void makeCommit(const std::string& id, const std::string& parent, const std::vector<std::string>& files,
                       const std::vector<std::string>& deleted = {}) {
    std::string meta = "commit " + id + "\nparent " + (parent.empty() ? "none" : parent) +
                       "\ntimestamp 100\nauthor tester\nmessage m-" + id.substr(0, 1) + "\nfiles " +
                       std::to_string(files.size()) + "\n";
    for (const auto& f : files) {
        meta += f + "\n";
        put(gitFile("objects/" + id + "/snapshot/" + f), "content of " + f + " in " + id.substr(0, 1));
    }
    if (!deleted.empty()) {
        meta += "deleted " + std::to_string(deleted.size()) + "\n";
        for (const auto& d : deleted) meta += d + "\n";
    }
    put(gitFile("objects/" + id + "/metadata"), meta);
}

static void testTrim() {
    CHECK(trim("") == "");
    CHECK(trim("   ") == "");
    CHECK(trim("abc") == "abc");
    CHECK(trim("  a b \t\r\n") == "a b");
    CHECK(trim("\tx") == "x");
}

static void testIdAndRefValidation() {
    CHECK(isFullCommitId(A));
    CHECK(isFullCommitId(std::string(40, '7')));
    CHECK(isFullCommitId("0123456789abcdef0123456789abcdef01234567"));
    CHECK(!isFullCommitId(""));
    CHECK(!isFullCommitId(std::string(39, 'a')));
    CHECK(!isFullCommitId(std::string(41, 'a')));
    CHECK(!isFullCommitId(std::string(40, 'A')));  // lowercase only
    CHECK(!isFullCommitId(std::string(40, 'g')));

    CHECK(isSafeHeadRef("refs/heads/main"));
    CHECK(isSafeHeadRef("refs/heads/feature/login"));
    CHECK(!isSafeHeadRef("refs/heads/"));
    CHECK(!isSafeHeadRef("refs/heads/a/"));
    CHECK(!isSafeHeadRef("refs/heads/../x"));
    CHECK(!isSafeHeadRef("refs/heads/a\\b"));
    CHECK(!isSafeHeadRef("refs/tags/v1"));
    CHECK(!isSafeHeadRef("heads/main"));
    CHECK(!isSafeHeadRef("/refs/heads/main"));
    CHECK(!isSafeHeadRef(""));
}

static void testReadHeadState() {
    freshRepo();

    HeadState h = readHeadState(kRoot);
    CHECK(!h.valid);
    CHECK(h.error.find("missing") != std::string::npos);

    put(gitFile("HEAD"), "ref: refs/heads/main\n");
    h = readHeadState(kRoot);
    CHECK(h.valid && !h.detached);
    CHECK(h.branch == "main");
    CHECK(h.commitId.empty());  // a branch with no commits yet

    put(gitFile("refs/heads/main"), A + "\n");
    h = readHeadState(kRoot);
    CHECK(h.valid && h.commitId == A);

    put(gitFile("HEAD"), "ref: refs/heads/main\r\n");  // CRLF is tolerated
    h = readHeadState(kRoot);
    CHECK(h.valid && h.branch == "main" && h.commitId == A);

    put(gitFile("HEAD"), "ref: refs/heads/feature/x\n");
    h = readHeadState(kRoot);
    CHECK(h.valid && h.branch == "feature/x");

    // Detached HEAD: only valid while the commit exists.
    put(gitFile("HEAD"), B + "\n");
    h = readHeadState(kRoot);
    CHECK(!h.valid);
    CHECK(h.error.find("missing") != std::string::npos);
    makeCommit(B, "", {"a.txt"});
    h = readHeadState(kRoot);
    CHECK(h.valid && h.detached && h.commitId == B && h.branch.empty());

    // Anything else is damage, never followed.
    put(gitFile("HEAD"), "garbage\n");
    h = readHeadState(kRoot);
    CHECK(!h.valid && h.error.find("damaged") != std::string::npos);
    put(gitFile("HEAD"), "ref: refs/heads/../x\n");
    CHECK(!readHeadState(kRoot).valid);
    put(gitFile("HEAD"), "ref: refs/tags/v1\n");
    CHECK(!readHeadState(kRoot).valid);
}

static void testHeadAccessors() {
    freshRepo();
    CHECK(getHEADBranch(kRoot).empty());
    CHECK(getHEADCommitId(kRoot).empty());

    CHECK(setHEADBranch(kRoot, "dev"));
    CHECK(slurp(gitFile("HEAD")) == "ref: refs/heads/dev\n");
    CHECK(getHEADBranch(kRoot) == "dev");
    CHECK(getHEADCommitId(kRoot).empty());  // branch file does not exist yet

    CHECK(setHEADCommitId(kRoot, A));
    CHECK(slurp(gitFile("refs/heads/dev")) == A + "\n");
    CHECK(getHEADCommitId(kRoot) == A);

    CHECK(setHEADCommitId(kRoot, ""));  // back to a branch without commits
    CHECK(slurp(gitFile("refs/heads/dev")).empty());
    CHECK(getHEADCommitId(kRoot).empty());

    put(gitFile("HEAD"), "ref: refs/heads/feature/x\n");
    CHECK(getHEADBranch(kRoot) == "feature/x");

    // Detached: no branch to move, the raw id is reported.
    makeCommit(B, "", {"a.txt"});
    put(gitFile("HEAD"), B + "\n");
    CHECK(getHEADBranch(kRoot).empty());
    CHECK(getHEADCommitId(kRoot) == B);
    CHECK(!setHEADCommitId(kRoot, A));
    CHECK(slurp(gitFile("HEAD")) == B + "\n");  // HEAD untouched

    // A damaged reference is not followed.
    put(gitFile("HEAD"), "ref: refs/heads/../../escape\n");
    CHECK(getHEADCommitId(kRoot).empty());
}

static void testReadCommitMeta() {
    freshRepo();
    CommitMetadata m;

    CHECK(!readCommitMeta(kRoot, A, m));  // no such commit

    put(gitFile("objects/" + A + "/metadata"),
        "commit " + A + "\nparent none\ntimestamp 100\nauthor tester\nmessage first commit\nfiles 2\n"
        "a.txt\nmessage 1.txt\n");
    CHECK(readCommitMeta(kRoot, A, m));
    CHECK(m.commitId == A);
    CHECK(m.parentId.empty() && m.parents.empty());
    CHECK(m.timestamp == "100" && m.author == "tester" && m.message == "first commit");
    CHECK(m.files.size() == 2 && m.files[0] == "a.txt" && m.files[1] == "message 1.txt");  // keyword-like name is a path
    CHECK(m.deleted.empty());

    put(gitFile("objects/" + B + "/metadata"),
        "commit " + B + "\r\nparent " + A + "\r\ntimestamp 200\r\nauthor t\r\nmessage second\r\nfiles 1\r\nb.txt\r\n"
        "deleted 1\r\na.txt\r\n");
    CHECK(readCommitMeta(kRoot, B, m));  // CRLF file
    CHECK(m.parentId == A && m.parents.size() == 1);
    CHECK(m.files.size() == 1 && m.files[0] == "b.txt");
    CHECK(m.deleted.size() == 1 && m.deleted[0] == "a.txt");

    put(gitFile("objects/" + C + "/metadata"),
        "commit " + C + "\nparents " + A + " " + B + "\ntimestamp 3\nmessage merge\nfiles 0\n");
    CHECK(readCommitMeta(kRoot, C, m));
    CHECK(m.parents.size() == 2 && m.parentId == A);
    CHECK(m.files.empty());

    // Damaged files are rejected as a whole.
    put(gitFile("objects/" + D + "/metadata"),
        "commit " + D + "\nparent none\ntimestamp 1\nmessage m\nfiles 3\na.txt\nb.txt\n");  // list cut short
    CHECK(!readCommitMeta(kRoot, D, m));
    put(gitFile("objects/" + D + "/metadata"),
        "commit " + D + "\nparent none\ntimestamp 1\nmessage m\nfiles 1\n../evil\n");  // unsafe path
    CHECK(!readCommitMeta(kRoot, D, m));
    put(gitFile("objects/" + D + "/metadata"), "commit " + D + "\nparent none\nmessage m\nfiles 0\n");  // no timestamp
    CHECK(!readCommitMeta(kRoot, D, m));
    put(gitFile("objects/" + D + "/metadata"), "commit " + D + "\nparent none\ntimestamp 1\nmessage m\n");  // no files line
    CHECK(!readCommitMeta(kRoot, D, m));
    put(gitFile("objects/" + D + "/metadata"), "commit " + D + "\nparent none\ntimestamp 1\nmessage m\nfiles x\n");
    CHECK(!readCommitMeta(kRoot, D, m));  // count is not a number
}

static void testResolveCommitId() {
    freshRepo();
    CHECK(resolveCommitId("no_such_folder_xyz", "abcd").empty());  // no objects directory

    const std::string id1 = "abcd1111" + std::string(32, '0');
    const std::string id2 = "abcd2222" + std::string(32, '0');
    const std::string id3 = "ef012345" + std::string(32, '0');
    makeCommit(id1, "", {"a.txt"});
    makeCommit(id2, "", {"a.txt"});
    makeCommit(id3, "", {"a.txt"});
    fs::create_directories(gitFile("objects/ef01ffff" + std::string(32, '0')));  // no metadata: ignored

    bool amb = true;
    CHECK(resolveCommitId(kRoot, "ef01", &amb) == id3 && !amb);
    CHECK(resolveCommitId(kRoot, "abcd1", &amb) == id1 && !amb);
    CHECK(resolveCommitId(kRoot, id2, &amb) == id2 && !amb);  // full id
    CHECK(resolveCommitId(kRoot, "EF01", &amb) == id3);       // case-insensitive

    CHECK(resolveCommitId(kRoot, "abcd", &amb).empty() && amb);  // two commits share the prefix
    CHECK(resolveCommitId(kRoot, "9999", &amb).empty() && !amb); // unknown
    CHECK(resolveCommitId(kRoot, "abc", &amb).empty() && !amb);  // shorter than 4
    CHECK(resolveCommitId(kRoot, std::string(41, 'a'), &amb).empty());
    CHECK(resolveCommitId(kRoot, "zzzz", &amb).empty());         // not hex
    CHECK(resolveCommitId(kRoot, "../..", &amb).empty());        // path tricks never reach the filesystem
    CHECK(resolveCommitId(kRoot, "ab/d", &amb).empty());
}

static void testTrackedFilesAtCommit() {
    freshRepo();
    makeCommit(A, "", {"a.txt", "b.txt"});
    makeCommit(B, A, {"a.txt"}, {"b.txt"});  // rewrites a.txt, removes b.txt
    makeCommit(C, B, {"c.txt"});

    bool complete = false;
    std::string problem;
    auto t = getTrackedFilesAtCommit(kRoot, C, &complete, &problem);
    CHECK(complete && problem.empty());
    CHECK(t.size() == 2);
    CHECK(t.count("a.txt") == 1 && t.count("c.txt") == 1 && t.count("b.txt") == 0);
    CHECK(slurp(t["a.txt"]) == "content of a.txt in b");  // the newest commit wins
    CHECK(slurp(t["c.txt"]) == "content of c.txt in c");

    t = getTrackedFilesAtCommit(kRoot, A, &complete, &problem);
    CHECK(complete && t.size() == 2 && slurp(t["a.txt"]) == "content of a.txt in a");

    t = getTrackedFilesAtCommit(kRoot, "", &complete, &problem);  // no commits yet
    CHECK(complete && t.empty());

    // Missing parent commit.
    makeCommit(D, MISSING, {"d.txt"});
    problem.clear();
    t = getTrackedFilesAtCommit(kRoot, D, &complete, &problem);
    CHECK(!complete && !problem.empty());

    // Listed file without a snapshot.
    const std::string E(40, 'e');
    makeCommit(E, "", {"x.txt"});
    fs::remove(gitFile("objects/" + E + "/snapshot/x.txt"));
    problem.clear();
    getTrackedFilesAtCommit(kRoot, E, &complete, &problem);
    CHECK(!complete && problem.find("x.txt") != std::string::npos);

    // Parents that form a cycle must end the walk, not loop.
    const std::string F(40, '1'), G(40, '2');
    makeCommit(F, G, {"f.txt"});
    makeCommit(G, F, {"g.txt"});
    problem.clear();
    getTrackedFilesAtCommit(kRoot, F, &complete, &problem);
    CHECK(!complete && problem.find("cycle") != std::string::npos);
}

static void testRemoveFileAndEmptyDirs() {
    freshRepo();
    put(fs::path(kRoot) / "a" / "b" / "c.txt", "x");
    removeFileAndEmptyDirs(kRoot, "a/b/c.txt");
    CHECK(!fs::exists(fs::path(kRoot) / "a"));  // empty parents are cleaned up
    CHECK(fs::exists(kRoot));                   // the root itself never is

    put(fs::path(kRoot) / "a" / "b" / "c.txt", "x");
    put(fs::path(kRoot) / "a" / "keep.txt", "y");
    removeFileAndEmptyDirs(kRoot, "a/b/c.txt");
    CHECK(!fs::exists(fs::path(kRoot) / "a" / "b"));
    CHECK(fs::exists(fs::path(kRoot) / "a" / "keep.txt"));  // a non-empty parent stays

    // Paths that leave the working tree are ignored.
    const fs::path outside = "test_repoutils_outside.txt";
    put(outside, "must survive");
    removeFileAndEmptyDirs(kRoot, "../test_repoutils_outside.txt");
    CHECK(fs::exists(outside));
    removeFileAndEmptyDirs(kRoot, ".minigit/HEAD");
    std::error_code ec;
    fs::remove(outside, ec);
}

int main() {
    testTrim();
    testIdAndRefValidation();
    testReadHeadState();
    testHeadAccessors();
    testReadCommitMeta();
    testResolveCommitId();
    testTrackedFilesAtCommit();
    testRemoveFileAndEmptyDirs();

    std::error_code ec;
    fs::remove_all(kRoot, ec);
    std::cout << "test_repoutils: all tests passed\n";
    return 0;
}
