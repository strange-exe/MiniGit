#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "InitCommand.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

int main() {
    fs::path tempRoot = fs::temp_directory_path() / "minigit_init_test";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);
    std::string R = tempRoot.string();

    // 1. Initial init
    InitResult r1 = initRepo(R);
    CHECK(r1.status == InitResult::INITIALIZED);
    CHECK(fs::is_directory(tempRoot / ".minigit"));
    CHECK(fs::is_directory(tempRoot / ".minigit" / "objects"));
    CHECK(fs::exists(tempRoot / ".minigit" / "HEAD"));
    CHECK(fs::exists(tempRoot / ".minigit" / "index"));
    CHECK(fs::exists(tempRoot / ".minigitignore"));

    // Verify HEAD content
    {
        std::ifstream headIn((tempRoot / ".minigit" / "HEAD").string());
        std::string headLine;
        std::getline(headIn, headLine);
        CHECK(headLine == "ref: refs/heads/main");
    }

    // 2. Re-initialization
    InitResult r2 = initRepo(R);
    CHECK(r2.status == InitResult::REINITIALIZED);

    // Clean up
    fs::remove_all(tempRoot);
    std::cout << "test_init: all tests passed\n";
    return 0;
}
