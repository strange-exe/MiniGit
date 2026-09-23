#pragma once
// Repository initialization (Abhinesh's module).
// Creates .minigit directory structure and initializes HEAD, index, and .minigitignore.
#include <string>

namespace minigit {

struct InitResult {
    enum Status { INITIALIZED, REINITIALIZED, ERROR };
    Status status = ERROR;
    std::string path;
    std::string message;
};

InitResult initRepo(const std::string& root = ".");

}  // namespace minigit
