// Unit tests for StagingArea: the ordered sets behind .minigit/index, staged
// deletions, and the index file. Needs only an empty .minigit directory.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

static const std::string kRoot = "test_staging_tmp";

static void freshRoot(bool withGitDir = true) {
    std::error_code ec;
    fs::remove_all(kRoot, ec);
    fs::create_directories(withGitDir ? fs::path(kRoot) / ".minigit" : fs::path(kRoot), ec);
}

static fs::path indexFile() { return fs::path(kRoot) / ".minigit" / "index"; }

static std::string slurp(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void writeIndex(const std::string& text) { std::ofstream(indexFile().string(), std::ios::binary) << text; }

using Paths = std::vector<std::string>;

static void testStageAndUnstage() {
    freshRoot();
    StagingArea sa(kRoot);
    CHECK(sa.size() == 0);

    CHECK(sa.stage("a.txt"));
    CHECK(!sa.stage("a.txt"));  // duplicate staging is prevented
    CHECK(sa.isStaged("a.txt"));
    CHECK(sa.size() == 1);

    // Any spelling of the same file is the same entry.
    CHECK(sa.isStaged("./a.txt"));
    CHECK(sa.isStaged(".\\a.txt"));
    CHECK(!sa.stage("./a.txt"));

    CHECK(!sa.stage(""));  // nothing to stage
    CHECK(!sa.isStaged("b.txt"));

    CHECK(sa.unstage("a.txt"));
    CHECK(!sa.isStaged("a.txt"));
    CHECK(!sa.unstage("a.txt"));  // already gone
    CHECK(sa.size() == 0);
}

static void testSortedOutput() {
    freshRoot();
    StagingArea sa(kRoot);
    sa.stage("b.txt");
    sa.stage("c/d.txt");
    sa.stage("a.txt");
    CHECK((sa.getStagedFiles() == Paths{"a.txt", "b.txt", "c/d.txt"}));  // ordered set: stable order
}

static void testStagedDeletions() {
    freshRoot();
    StagingArea sa(kRoot);

    CHECK(!sa.stageDeletion(""));
    CHECK(sa.stageDeletion("gone.txt"));
    CHECK(!sa.stageDeletion("gone.txt"));
    CHECK(sa.isStagedForDeletion("gone.txt") && !sa.isStaged("gone.txt"));
    CHECK(sa.size() == 1);
    CHECK(sa.getStagedFiles().empty());
    CHECK((sa.getStagedDeletions() == Paths{"gone.txt"}));

    // Staging the file again cancels the deletion (and counts as a change).
    CHECK(sa.stage("gone.txt"));
    CHECK(sa.isStaged("gone.txt") && !sa.isStagedForDeletion("gone.txt"));

    // Staging a deletion replaces a staged file.
    CHECK(sa.stageDeletion("gone.txt"));
    CHECK(!sa.isStaged("gone.txt") && sa.isStagedForDeletion("gone.txt"));

    CHECK(sa.unstage("gone.txt"));  // unstage also drops a staged deletion
    CHECK(sa.size() == 0);
}

static void testEntriesListFilesThenDeletions() {
    freshRoot();
    StagingArea sa(kRoot);
    sa.stage("z.txt");
    sa.stageDeletion("a-old.txt");
    sa.stage("m.txt");
    CHECK((sa.getStagedEntries() == Paths{"m.txt", "z.txt", "deleted:a-old.txt"}));
    CHECK(sa.size() == 3);
}

static void testSaveAndReload() {
    freshRoot();
    {
        StagingArea sa(kRoot);
        sa.stage("b.txt");
        sa.stage("a.txt");
        sa.stageDeletion("old.txt");
        CHECK(sa.save());
    }
    CHECK(slurp(indexFile()) == "a.txt\nb.txt\ndeleted:old.txt\n");

    StagingArea again(kRoot);
    CHECK((again.getStagedFiles() == Paths{"a.txt", "b.txt"}));
    CHECK((again.getStagedDeletions() == Paths{"old.txt"}));
}

static void testStageOnlyChangesMemory() {
    freshRoot();
    StagingArea sa(kRoot);
    sa.stage("a.txt");
    CHECK(!fs::exists(indexFile()));  // stage() alone writes nothing; save() does
    CHECK(StagingArea(kRoot).size() == 0);
}

static void testSaveNeedsRepository() {
    freshRoot(false);
    StagingArea sa(kRoot);
    sa.stage("a.txt");
    CHECK(!sa.save());
    CHECK(!fs::exists(fs::path(kRoot) / ".minigit"));
}

static void testLoadDropsUnsafeAndBlankLines() {
    freshRoot();
    writeIndex("ok.txt\n\n../evil\n/abs/path\n.minigit/HEAD\ndeleted:../x\ndeleted:gone.txt\nwin.txt\r\n");
    StagingArea sa(kRoot);
    CHECK((sa.getStagedFiles() == Paths{"ok.txt", "win.txt"}));  // CRLF line handled
    CHECK((sa.getStagedDeletions() == Paths{"gone.txt"}));
}

static void testSetStagedFilesAndClearSave() {
    freshRoot();
    StagingArea sa(kRoot);
    sa.stage("old.txt");

    // Used by undo/redo: takes getStagedEntries() output, replaces everything and saves.
    sa.setStagedFiles({"x.txt", "./y.txt", "deleted:z.txt", ""});
    CHECK((sa.getStagedFiles() == Paths{"x.txt", "y.txt"}));
    CHECK((sa.getStagedDeletions() == Paths{"z.txt"}));
    CHECK(slurp(indexFile()) == "x.txt\ny.txt\ndeleted:z.txt\n");

    sa.clear();
    CHECK(sa.size() == 0);
    CHECK(slurp(indexFile()).empty());
    CHECK(StagingArea(kRoot).size() == 0);
}

int main() {
    testStageAndUnstage();
    testSortedOutput();
    testStagedDeletions();
    testEntriesListFilesThenDeletions();
    testSaveAndReload();
    testStageOnlyChangesMemory();
    testSaveNeedsRepository();
    testLoadDropsUnsafeAndBlankLines();
    testSetStagedFilesAndClearSave();

    std::error_code ec;
    fs::remove_all(kRoot, ec);
    std::cout << "test_staging: all tests passed\n";
    return 0;
}
