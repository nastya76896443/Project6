#include "rolling_average.h"

#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
    try {
        std::cout << "Enter decimal numbers. Send EOF to finish.\n";
        return runRollingAverage(std::cin, std::cout, std::cerr);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
