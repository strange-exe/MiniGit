#pragma once
// Shared helpers for test_regression.cpp and test_integration.cpp.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "CLI.h"

namespace tc {

namespace fs = std::filesystem;

inline int& checkCount() {
    static int n = 0;
    return n;
}

}  // namespace tc

// Counts every check; the first failure prints file:line and ends the program with exit code 1.
#define CHECK(c)                                                                              \
    do {                                                                                      \
        ++tc::checkCount();                                                                   \
        if (!(c)) {                                                                           \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #c << std::endl;      \
            std::exit(1);                                                                     \
        }                                                                                     \
    } while (0)

#define PASS(name) (std::cout << "  [PASS] " << name << "\n")

namespace tc {

inline void writeFile(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p.string(), std::ios::binary) << s;
}

inline std::string readFile(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Empty directory `name` below the current directory (the tests run from module/).
inline std::string freshDir(const std::string& name) {
    std::error_code ec;
    fs::remove_all(name, ec);
    fs::create_directories(name, ec);
    return name;
}

inline void removeDir(const std::string& name) {
    std::error_code ec;
    fs::remove_all(name, ec);
}

// Runs the real command dispatcher and captures what it prints on stdout.
inline std::string runCli(const std::vector<std::string>& args, const std::string& root, int& exitCode) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    std::stringstream buffer;
    std::streambuf* oldCout = std::cout.rdbuf(buffer.rdbuf());
    exitCode = minigit::CLI::dispatch(static_cast<int>(argv.size()), argv.data(), root);
    std::cout.rdbuf(oldCout);
    return buffer.str();
}

inline bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace tc
