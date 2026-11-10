CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Iinclude
LDFLAGS  := -static
LIB_SRC  := src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp src/CommitHistory.cpp src/CommitCommand.cpp src/CommitLog.cpp src/LogCommand.cpp src/BranchCommand.cpp src/RollbackCommand.cpp src/RestoreCommand.cpp src/CommitGraph.cpp src/CLI.cpp

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

test_rollback: tests/test_rollback.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_branch: tests/test_branch.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_checkout: tests/test_checkout.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_dag: tests/test_dag.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_regression: tests/test_regression.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_integration: tests/test_integration.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

TESTS := test_trie test_add test_init test_cli test_commit test_commitlog test_log test_rollback test_branch test_checkout test_dag test_regression test_integration

test: $(TESTS)
	./test_trie
	./test_add
	./test_init
	./test_cli
	./test_commit
	./test_commitlog
	./test_log
	./test_rollback
	./test_branch
	./test_checkout
	./test_dag
	./test_regression
	./test_integration

# `clean` has three cases:
#  * make found a real sh on PATH (MSYS / Git Bash): $(SHELL) is then a full path such as
#    C:/Program Files/Git/usr/bin/sh.exe, and recipes use plain rm. Calling `cmd /c` from there
#    fails: MSYS rewrites "/c" into a drive path, cmd opens interactively and nothing is deleted.
#  * no sh on PATH (plain PowerShell with MinGW): $(SHELL) stays the bare name "sh.exe", make
#    runs recipes through cmd.exe, and `del` is used.
#  * Linux/macOS: rm.
clean:
ifeq ($(OS),Windows_NT)
ifneq (,$(findstring /,$(SHELL)))
	rm -f minigit minigit_demo minigit_log_demo $(TESTS) $(addsuffix .exe,minigit minigit_demo minigit_log_demo $(TESTS))
else
	-cmd /c "del /q /f minigit minigit_demo minigit_log_demo $(TESTS) $(addsuffix .exe,minigit minigit_demo minigit_log_demo $(TESTS)) 2>nul" || exit 0
endif
else
	rm -f minigit minigit_demo minigit_log_demo $(TESTS) $(addsuffix .exe,minigit minigit_demo minigit_log_demo $(TESTS))
endif
