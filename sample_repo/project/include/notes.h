#pragma once
#include <string>
#include <vector>

struct Note {
    int id;
    std::string text;
};

class Notebook {
public:
    int add(const std::string& text);
    const std::vector<Note>& all() const { return notes_; }

private:
    std::vector<Note> notes_;
    int nextId_ = 1;
};
