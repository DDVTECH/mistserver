#include <cstdlib>
#include <ios>
#include <iostream>
#include <fstream>
#include <ostream>

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "ERROR EXPECTED MORE FILES" << std::endl;
        return 1;
    }

    std::ofstream output(argv[1]);

    std::ifstream header(argv[2]);
    std::ifstream css(argv[3]);
    std::ifstream footer(argv[4]);

    if (!header.is_open() || !css.is_open() || !footer.is_open()) {
        std::cerr << "ERROR Couldn't open file" << std::endl;
        return 1;
    }

    output << header.rdbuf();
    output << "<script>";
    for (int i = 5; i < argc; i++) {
        std::ifstream script(argv[i]);
        if (!script.is_open()) {
            std::cerr << "ERROR Couldn't open file" << std::endl;
            return 1;
        }
        output << script.rdbuf();
    }
    output << "</script>";
    output << "<style>" << css.rdbuf() << "</style>";
    output << footer.rdbuf();
    std::flush(output);
    return 0;
}

