CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Iinclude
LDFLAGS  := -static

ifeq ($(OS),Windows_NT)
    RM := del /f /q
else
    RM := rm -f
endif

LIB_SRC  := src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp src/CommitHistory.cpp src/CommitCommand.cpp src/CLI.cpp

all: minigit

minigit: $(LIB_SRC) src/main.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

minigit_demo: $(LIB_SRC) src/main_demo.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test_trie: tests/test_trie.cpp src/Trie.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_add: tests/test_add.cpp src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_init: tests/test_init.cpp src/InitCommand.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_cli: tests/test_cli.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_commit: tests/test_commit.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: test_trie test_add test_init test_cli test_commit
	./test_trie
	./test_add
	./test_init
	./test_cli
	./test_commit

clean:
	$(RM) minigit.exe minigit_demo.exe test_trie.exe test_add.exe test_init.exe test_cli.exe test_commit.exe minigit minigit_demo test_trie test_add test_init test_cli test_commit 2>NUL || true
