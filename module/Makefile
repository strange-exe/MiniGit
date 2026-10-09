CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Iinclude
LDFLAGS  := -static
LIB_SRC  := src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp src/CommitHistory.cpp src/CommitCommand.cpp src/CommitLog.cpp src/LogCommand.cpp src/CLI.cpp

all: minigit

minigit: $(LIB_SRC) src/main.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

minigit_demo: $(LIB_SRC) src/main_demo.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

minigit_log_demo: $(LIB_SRC) src/main_demo_log.cpp
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

test_commitlog: tests/test_commitlog.cpp src/CommitLog.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_log: tests/test_log.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

TESTS := test_trie test_add test_init test_cli test_commit test_commitlog test_log

test: $(TESTS)
	./test_trie
	./test_add
	./test_init
	./test_cli
	./test_commit
	./test_commitlog
	./test_log

# Recipes run under sh (Git Bash / MSYS on Windows), like the ./test_* lines above.
clean:
	rm -f minigit minigit_demo minigit_log_demo $(TESTS) $(addsuffix .exe,minigit minigit_demo minigit_log_demo $(TESTS))
