#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ostream>

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "ERROR EXPECTED MORE FILES" << std::endl;
        return 1;
    }

    std::ifstream header(argv[1]);
    std::ifstream css(argv[2]);
    std::ifstream footer(argv[3]);

    if (!header.is_open() || !css.is_open() || !footer.is_open()) {
        std::cerr << "ERROR Couldn't open file" << std::endl;
        return 1;
    }

    std::cout << header.rdbuf();
    std::cout << "<script>";
    for (int i = 4; i < argc; i++) {
        std::ifstream script(argv[i]);
        if (!script.is_open()) {
            std::cerr << "ERROR Couldn't open file" << std::endl;
            return 1;
        }
        std::cout << script.rdbuf();
    }
    std::cout << "</script>";
    std::cout << "<style>" << css.rdbuf() << "</style>";
    std::cout << footer.rdbuf();
    std::flush(std::cout);
    return 0;
}