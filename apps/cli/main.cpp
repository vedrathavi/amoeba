#include "amoeba/engine.hpp"

#include <iostream>
int main() {
    std::cout << amoeba::get_name() << "\n";
    std::cout << amoeba::get_description() << "\n";
    return 0;
}
