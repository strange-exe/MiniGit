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
    CHECK(fs::is_directory(tempRoot / ".minigit" / "refs" / "heads"));
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

    // 2. Re-initialization keeps existing data
    {
        std::ofstream((tempRoot / ".minigit" / "index").string()) << "kept.txt\n";
        std::ofstream((tempRoot / ".minigitignore").string()) << "*.log\n";
    }
    InitResult r2 = initRepo(R);
    CHECK(r2.status == InitResult::REINITIALIZED);
    {
        std::ifstream idx((tempRoot / ".minigit" / "index").string());
        std::string line;
        std::getline(idx, line);
        CHECK(line == "kept.txt");
        std::ifstream ig((tempRoot / ".minigitignore").string());
        std::getline(ig, line);
        CHECK(line == "*.log");
    }

    // 3. A file where the repository folder should be is reported, not ignored
    fs::path blocked = tempRoot / "blocked";
    fs::create_directories(blocked);
    std::ofstream((blocked / ".minigit").string()) << "not a directory";
    InitResult r3 = initRepo(blocked.string());
    CHECK(r3.status == InitResult::ERROR);
    CHECK(r3.message.find("cannot create directory") != std::string::npos);

    // Clean up
    fs::remove_all(tempRoot);
    std::cout << "test_init: all tests passed\n";
    return 0;
}
