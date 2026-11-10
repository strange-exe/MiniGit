// Repository storage validation and effective-tree reconstruction tests.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "RepoStore.h"

namespace fs = std::filesystem;
using namespace minigit::repo;

#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl; \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

static void writeFile(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path.string(), std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    CHECK(static_cast<bool>(out));
}

static std::string meta(const std::string& id, const std::string& parent,
                        const std::string& message, const std::string& files) {
    return "commit " + id + "\nparent " + (parent.empty() ? "none" : parent) +
           "\ntimestamp 100\nauthor test\nmessage " + message + "\nfiles " +
           std::to_string(static_cast<unsigned long long>(std::count(files.begin(), files.end(), '\n'))) +
           "\n" + files;
}

int main() {
    fs::path root = fs::temp_directory_path() / "minigit_test_repo_store";
    fs::remove_all(root);
    fs::create_directories(root / ".minigit" / "objects");
    const std::string R = root.string();

    const std::string c1 = "1111111111111111111111111111111111111111";
    const std::string c2 = "2222222222222222222222222222222222222222";
    fs::path o1 = root / ".minigit" / "objects" / c1;
    fs::path o2 = root / ".minigit" / "objects" / c2;

    writeFile(o1 / "metadata", meta(c1, "", "first", "a.txt\nb.txt\n.minigitignore\n"));
    writeFile(o1 / "snapshot" / "a.txt", "a1");
    writeFile(o1 / "snapshot" / "b.txt", "b1");
    writeFile(o1 / "snapshot" / ".minigitignore", "*.log\n");

    writeFile(o2 / "metadata", meta(c2, c1, "second", "a.txt\n"));
    writeFile(o2 / "snapshot" / "a.txt", "a2");

    std::map<std::string, std::string> tree;
    std::string error;
    CHECK(effectiveTree(R, c2, tree, &error));
    CHECK(tree.size() == 3);
    CHECK(tree.at("a.txt") == "a2");
    CHECK(tree.at("b.txt") == "b1");
    CHECK(tree.at(".minigitignore") == "*.log\n");
    std::cout << "  [PASS] effective tree inherits unchanged files and overlays changed files\n";

    // Merge compatibility: the merge commit has two parents, but its snapshot
    // delta must include paths brought in from the second parent. effectiveTree
    // follows the first parent and overlays that merge delta.
    const std::string feature = "8888888888888888888888888888888888888888";
    const std::string merge = "9999999999999999999999999999999999999999";
    writeFile(root / ".minigit" / "objects" / feature / "metadata",
              meta(feature, c1, "feature", "feature.txt\n"));
    writeFile(root / ".minigit" / "objects" / feature / "snapshot" / "feature.txt", "from feature");
    writeFile(root / ".minigit" / "objects" / merge / "metadata",
              "commit " + merge + "\nparent " + c2 + "\nparent2 " + feature +
              "\ntimestamp 102\nauthor test\nmessage merge\nfiles 1\nfeature.txt\n");
    writeFile(root / ".minigit" / "objects" / merge / "snapshot" / "feature.txt", "merged feature");
    tree.clear();
    CHECK(effectiveTree(R, merge, tree, &error));
    CHECK(tree.at("a.txt") == "a2" && tree.at("b.txt") == "b1");
    CHECK(tree.at("feature.txt") == "merged feature");
    std::cout << "  [PASS] merge delta on first-parent tree preserves second-parent content\n";

    // A merge target is not valid if its second-parent history is missing or cyclic,
    // even though the first-parent chain alone could reconstruct its file contents.
    const std::string brokenMerge = "dddddddddddddddddddddddddddddddddddddddd";
    const std::string missingParent = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    writeFile(root / ".minigit" / "objects" / brokenMerge / "metadata",
              "commit " + brokenMerge + "\nparent " + c2 + "\nparent2 " + missingParent +
              "\ntimestamp 103\nauthor test\nmessage broken merge\nfiles 0\n");
    tree.clear();
    CHECK(!effectiveTree(R, brokenMerge, tree, &error));
    CHECK(tree.empty() && error.find("parent commit") != std::string::npos);

    const std::string cyclicMerge = "ffffffffffffffffffffffffffffffffffffffff";
    writeFile(root / ".minigit" / "objects" / cyclicMerge / "metadata",
              "commit " + cyclicMerge + "\nparent " + c2 + "\nparent2 " + cyclicMerge +
              "\ntimestamp 104\nauthor test\nmessage cyclic merge\nfiles 0\n");
    tree.clear();
    CHECK(!effectiveTree(R, cyclicMerge, tree, &error));
    CHECK(tree.empty() && error.find("cycle") != std::string::npos);
    std::cout << "  [PASS] merge parent history is validated for missing objects and cycles\n";

    // The parser must reject a truncated declared file list rather than silently
    // returning a commit with fewer files than its metadata promises.
    const std::string truncated = "3333333333333333333333333333333333333333";
    writeFile(root / ".minigit" / "objects" / truncated / "metadata",
              "commit " + truncated + "\nparent " + c2 +
              "\ntimestamp 101\nauthor test\nmessage broken\nfiles 2\nonly.txt\n");
    CommitMeta parsed;
    CHECK(!readCommitMeta(R, truncated, parsed));
    tree.clear();
    CHECK(!effectiveTree(R, truncated, tree, &error));
    CHECK(tree.empty() && !error.empty());
    std::cout << "  [PASS] truncated metadata is rejected and output is cleared\n";

    const std::string unsafe = "4444444444444444444444444444444444444444";
    writeFile(root / ".minigit" / "objects" / unsafe / "metadata",
              "commit " + unsafe + "\nparent " + c2 +
              "\ntimestamp 101\nauthor test\nmessage unsafe\nfiles 1\n../outside.txt\n");
    CHECK(!readCommitMeta(R, unsafe, parsed));
    std::cout << "  [PASS] unsafe repository-relative paths are rejected\n";

    // The same path can become a file and a directory descendant across commits
    // only if a deletion is represented. This format has no deletion marker, so
    // reject the ambiguous effective tree instead of letting restore fail midway.
    const std::string fileBase = "5555555555555555555555555555555555555555";
    const std::string fileChild = "6666666666666666666666666666666666666666";
    writeFile(root / ".minigit" / "objects" / fileBase / "metadata",
              meta(fileBase, c2, "file path", "node\n"));
    writeFile(root / ".minigit" / "objects" / fileBase / "snapshot" / "node", "file");
    writeFile(root / ".minigit" / "objects" / fileChild / "metadata",
              meta(fileChild, fileBase, "child path", "node/child.txt\n"));
    writeFile(root / ".minigit" / "objects" / fileChild / "snapshot" / "node" / "child.txt", "child");
    tree.clear();
    CHECK(!effectiveTree(R, fileChild, tree, &error));
    CHECK(tree.empty());
    std::cout << "  [PASS] conflicting file/directory paths are rejected\n";

    // A malformed parent reference must not be used as an object-store path.
    const std::string badParent = "7777777777777777777777777777777777777777";
    writeFile(root / ".minigit" / "objects" / badParent / "metadata",
              "commit " + badParent + "\nparent ../../outside\n"
              "timestamp 101\nauthor test\nmessage bad parent\nfiles 0\n");
    CHECK(!readCommitMeta(R, badParent, parsed));

    const std::string badTimestamp = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    writeFile(root / ".minigit" / "objects" / badTimestamp / "metadata",
              "commit " + badTimestamp + "\nparent none\ntimestamp yesterday\nauthor test\nmessage bad time\nfiles 0\n");
    CHECK(!readCommitMeta(R, badTimestamp, parsed));

    const std::string extraPath = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    writeFile(root / ".minigit" / "objects" / extraPath / "metadata",
              "commit " + extraPath + "\nparent none\ntimestamp 101\nauthor test\nmessage extra path\nfiles 0\nundeclared.txt\n");
    CHECK(!readCommitMeta(R, extraPath, parsed));

    const std::string duplicateParents = "cccccccccccccccccccccccccccccccccccccccc";
    writeFile(root / ".minigit" / "objects" / duplicateParents / "metadata",
              "commit " + duplicateParents + "\nparent " + c1 + "\nparent2 " + c1 +
              "\ntimestamp 101\nauthor test\nmessage duplicate parents\nfiles 0\n");
    CHECK(!readCommitMeta(R, duplicateParents, parsed));
    std::cout << "  [PASS] malformed parent ids, timestamp, extra file lines and duplicate parents are rejected\n";

    fs::remove_all(root);
    std::cout << "test_repo_store: all tests passed\n";
    return 0;
}
