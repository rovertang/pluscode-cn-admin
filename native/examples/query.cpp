#include "pluscode_admin/index.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4) { std::cerr << "Usage: pcad-query DATABASE [CODE | LAT LNG]\nOmit coordinates for one full code per stdin line.\n"; return 2; }
        pluscode_admin::Index index(argv[1]);
        if (argc == 4) std::cout << index.lookup_latlng(std::stod(argv[2]), std::stod(argv[3])) << '\n';
        else if (argc == 3) std::cout << index.lookup(argv[2]) << '\n';
        else { std::string line; while (std::getline(std::cin, line)) std::cout << index.lookup(line) << '\n'; }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
