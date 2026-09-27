#pragma once
// A single commit, as a node in Snehil's doubly linked list.
// Populated from Krish's on-disk format: .minigit/objects/<id>/metadata
#include <string>
#include <vector>

namespace minigit {

struct CommitNode {
    std::string id;
    std::string parentId;   // "" if this is the first commit in the repo
    std::string message;
    std::string timestamp;  // unix time, as a string (matches CommitCommand's format)
    std::vector<std::string> files;

    CommitNode* prev = nullptr;  // older commit
    CommitNode* next = nullptr;  // newer commit
};

}  // namespace minigit
