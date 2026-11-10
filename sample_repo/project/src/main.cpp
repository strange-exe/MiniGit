#include <iostream>

#include "notes.h"

int main() {
    Notebook book;
    book.add("Buy milk");
    book.add("Finish the PBL report");
    for (const Note& n : book.all()) std::cout << n.id << ". " << n.text << "\n";
    return 0;
}
