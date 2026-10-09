#include <cstdlib>
#include <iostream>

#include "PathUtils.h"
#include "Trie.h"

using namespace minigit;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

int main() {
    Trie t;
    Trie::Match m;

    // insert / search
    CHECK(t.insert("secret.txt", "secret.txt", 1, false));
    CHECK(!t.insert("secret.txt", "secret.txt", 1, false));  // duplicate
    CHECK(!t.insert("", "", 1, false));                      // empty key
    CHECK(t.search("secret.txt"));
    CHECK(!t.search("secret"));      // prefix of a key is not a key
    CHECK(!t.search("secret.txt2"));
    CHECK(t.size() == 1);

    // directory (prefix) rule
    CHECK(t.insert("build/", "build/", 2, true));
    CHECK(t.findMatch("build/x/y.o", m) && m.pattern == "build/" && m.line == 2);
    CHECK(t.findMatch("build/", m));
    CHECK(!t.findMatch("buildings/a.cpp", m));  // classic bug avoided
    CHECK(!t.findMatch("build", m));

    // exact file rule must match the whole path only
    CHECK(t.findMatch("secret.txt", m) && m.line == 1);
    CHECK(!t.findMatch("secret.txt.bak", m));
    CHECK(!t.findMatch("dir/secret.txt", m));

    // reversed extension rule
    Trie ext;
    CHECK(ext.insert(reverseString(".exe"), "*.exe", 3, true));
    CHECK(ext.findMatch(reverseString("a.exe"), m));
    CHECK(ext.findMatch(reverseString("dir/b.exe"), m));
    CHECK(!ext.findMatch(reverseString("exe.txt"), m));
    CHECK(!ext.findMatch(reverseString("myexe"), m));

    // remove: sibling rules survive, nodes are cleaned
    CHECK(t.insert("build2/", "build2/", 4, true));
    CHECK(t.remove("build/"));
    CHECK(!t.remove("build/"));
    CHECK(!t.findMatch("build/x.o", m));
    CHECK(t.findMatch("build2/x.o", m));
    CHECK(t.search("secret.txt"));
    CHECK(t.remove("secret.txt"));
    CHECK(t.remove("build2/"));
    CHECK(t.empty());
    CHECK(!t.findMatch("anything", m));

    // path normalization
    CHECK(normalizePath(".\\src\\a.txt") == "src/a.txt");
    CHECK(normalizePath("./src/a.txt") == "src/a.txt");
    CHECK(normalizePath("build/") == "build/");
    CHECK(normalizePath("a/../b.txt") == "b.txt");
    CHECK(isOutsideRepo(normalizePath("../x.txt")));

    std::cout << "test_trie: all tests passed\n";
    return 0;
}
