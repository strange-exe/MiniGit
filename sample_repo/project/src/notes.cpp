#include "notes.h"

int Notebook::add(const std::string& text) {
    notes_.push_back({nextId_, text});
    return nextId_++;
}
