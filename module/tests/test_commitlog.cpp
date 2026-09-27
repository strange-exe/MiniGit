// Pure data-structure tests for the CommitLog doubly linked list.
// Uses insert() directly, no disk access, no dependency on Krish's commitRepo.
#include <cstdlib>
#include <iostream>

#include "CommitLog.h"

using namespace minigit;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static CommitNode makeNode(const std::string& id, const std::string& parent, const std::string& msg) {
    CommitNode n;
    n.id = id;
    n.parentId = parent;
    n.message = msg;
    n.timestamp = "1000";
    n.files = {"a.txt"};
    return n;
}

int main() {
    CommitLog log;
    CHECK(log.empty());
    CHECK(log.head() == nullptr && log.tail() == nullptr);
    CHECK(log.next() == nullptr && log.prev() == nullptr);  // no crash on empty list

    // insert() appends as the newest node
    log.insert(makeNode("c1", "", "first"));
    CHECK(log.size() == 1);
    CHECK(log.head() == log.tail());
    CHECK(log.head()->id == "c1");

    log.insert(makeNode("c2", "c1", "second"));
    log.insert(makeNode("c3", "c2", "third"));
    CHECK(log.size() == 3);
    CHECK(log.head()->id == "c1");  // oldest
    CHECK(log.tail()->id == "c3");  // newest

    // doubly linked: walk forward from head, then backward from tail
    CommitNode* n = log.head();
    CHECK(n->id == "c1");
    CHECK(n->next->id == "c2");
    CHECK(n->next->next->id == "c3");
    CHECK(n->next->next->next == nullptr);
    CommitNode* t = log.tail();
    CHECK(t->prev->id == "c2");
    CHECK(t->prev->prev->id == "c1");
    CHECK(t->prev->prev->prev == nullptr);

    // find()
    CHECK(log.find("c2") != nullptr && log.find("c2")->message == "second");
    CHECK(log.find("missing") == nullptr);

    // cursor traversal: prev() from the tail walks toward older commits
    log.resetToTail();
    CHECK(log.current()->id == "c3");
    CHECK(log.prev()->id == "c2");
    CHECK(log.prev()->id == "c1");
    CHECK(log.prev() == nullptr);        // already at head, cursor stays put
    CHECK(log.current()->id == "c1");    // unmoved after a failed prev()

    // cursor traversal: next() from the head walks toward newer commits
    log.resetToHead();
    CHECK(log.current()->id == "c1");
    CHECK(log.next()->id == "c2");
    CHECK(log.next()->id == "c3");
    CHECK(log.next() == nullptr);
    CHECK(log.current()->id == "c3");

    std::cout << "test_commitlog: all tests passed\n";
    return 0;
}
