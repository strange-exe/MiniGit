CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude
LIB_SRC  := src/Trie.cpp src/IgnoreManager.cpp src/StagingArea.cpp src/AddCommand.cpp

all: minigit_demo

minigit_demo: $(LIB_SRC) src/main_demo.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_trie: tests/test_trie.cpp src/Trie.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_add: tests/test_add.cpp $(LIB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: test_trie test_add
	./test_trie
	./test_add

clean:
	rm -f minigit_demo test_trie test_add
