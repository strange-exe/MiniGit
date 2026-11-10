CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Iinclude
LDFLAGS  := -static
LIB_SRC  := src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp src/CommitHistory.cpp src/CommitCommand.cpp src/CommitLog.cpp src/RepoStore.cpp src/CommitGraph.cpp src/LogCommand.cpp src/RestoreCommand.cpp src/BranchCommand.cpp src/CLI.cpp

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

test_checkout: tests/test_checkout.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_branch: tests/test_branch.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_graph: tests/test_graph.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_repo_store: tests/test_repo_store.cpp src/RepoStore.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

ALL_TESTS := test_trie test_add test_init test_cli test_commit test_commitlog test_log test_checkout test_branch test_graph test_repo_store

# Select each test independently from the source/header files it actually needs.
# This lets a partial Phase 3 contribution run any self-contained tests available
# without making the aggregate target fail on absent Phase 2 files.
test_trie_DEPS := tests/test_trie.cpp src/Trie.cpp include/Trie.h
test_add_DEPS := tests/test_add.cpp src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp src/InitCommand.cpp include/Trie.h include/IgnoreManager.h include/StagingArea.h include/AddCommand.h include/InitCommand.h include/PathUtils.h include/filesystem
test_init_DEPS := tests/test_init.cpp src/InitCommand.cpp include/InitCommand.h include/PathUtils.h include/filesystem
test_cli_DEPS := tests/test_cli.cpp $(LIB_SRC) include/PathUtils.h include/filesystem
test_commit_DEPS := tests/test_commit.cpp $(LIB_SRC) include/PathUtils.h include/filesystem
test_commitlog_DEPS := tests/test_commitlog.cpp src/CommitLog.cpp include/CommitLog.h include/CommitNode.h include/filesystem
test_log_DEPS := tests/test_log.cpp $(LIB_SRC) include/PathUtils.h include/filesystem
test_checkout_DEPS := tests/test_checkout.cpp $(LIB_SRC) include/RepoStore.h include/RestoreCommand.h include/PathUtils.h include/filesystem include/IgnoreManager.h include/StagingArea.h
test_branch_DEPS := tests/test_branch.cpp $(LIB_SRC) include/BranchCommand.h include/CommitGraph.h include/RepoStore.h include/RestoreCommand.h include/PathUtils.h include/filesystem include/IgnoreManager.h include/StagingArea.h
test_graph_DEPS := tests/test_graph.cpp $(LIB_SRC) include/CommitGraph.h include/RepoStore.h include/PathUtils.h include/filesystem
test_repo_store_DEPS := tests/test_repo_store.cpp src/RepoStore.cpp include/RepoStore.h include/PathUtils.h include/filesystem

AVAILABLE_TESTS := $(foreach t,$(ALL_TESTS),$(if $(filter-out $(wildcard $($(t)_DEPS)),$($(t)_DEPS)),,$(t)))
TESTS := $(AVAILABLE_TESTS)
SKIPPED_TESTS := $(filter-out $(TESTS),$(ALL_TESTS))

test: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done
	@if [ -n "$(SKIPPED_TESTS)" ]; then echo "Skipped tests (missing source/header dependencies): $(SKIPPED_TESTS)"; fi

# Recipes run under sh (Git Bash / MSYS on Windows), like the ./test_* lines above.
clean:
	rm -f minigit minigit_demo minigit_log_demo $(ALL_TESTS) $(addsuffix .exe,minigit minigit_demo minigit_log_demo $(ALL_TESTS))
