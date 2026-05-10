/**
 * Creator utility: creates a binary file with employee records.
 * Command line: creator.exe <binary_filename> <number_of_records>
 * Input: records from console (num, name, hours).
 */

#include "employee.h"
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

static const int MAX_NAME_LENGTH = 9;  // leave space for null terminator

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: creator.exe <binary_filename> <number_of_records>\n";
            return 1;
        }

        const std::string filename = argv[1];
        const int numRecords = std::stoi(argv[2]);
        if (numRecords <= 0) {
            std::cerr << "Error: number of records must be positive.\n";
            return 1;
        }

        // Check if file already exists
        {
            std::ifstream test(filename);
            if (test.good()) {
                std::cerr << "Error: file '" << filename << "' already exists.\n";
                return 1;
            }
        }

        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile) {
            std::cerr << "Error: failed to create file '" << filename << "'.\n";
            return 1;
        }

        std::cout << "Enter " << numRecords << " employee records.\n";
        std::cout << "Format per line: <num> <name> <hours>\n";
        std::cout << "Name length up to " << MAX_NAME_LENGTH << " characters.\n";

        for (int i = 0; i < numRecords; ++i) {
            Employee emp{};
            std::cout << "Record " << (i + 1) << ": ";

            if (!(std::cin >> emp.num)) {
                std::cerr << "Error: invalid number for employee ID.\n";
                return 1;
            }

            std::string name;
            if (!(std::cin >> name)) {
                std::cerr << "Error: failed to read name.\n";
                return 1;
            }
            if (name.length() > MAX_NAME_LENGTH) {
                std::cerr << "Warning: name truncated to " << MAX_NAME_LENGTH << " characters.\n";
                name.resize(MAX_NAME_LENGTH);
            }
            std::strncpy(emp.name, name.c_str(), sizeof(emp.name) - 1);
            emp.name[sizeof(emp.name) - 1] = '\0';

            if (!(std::cin >> emp.hours)) {
                std::cerr << "Error: invalid number for hours.\n";
                return 1;
            }
            if (emp.hours < 0) {
                std::cerr << "Error: hours cannot be negative.\n";
                return 1;
            }

            outFile.write(reinterpret_cast<const char*>(&emp), sizeof(emp));
            if (!outFile) {
                std::cerr << "Error: failed to write record to file.\n";
                return 1;
            }
        }

        outFile.close();
        if (!outFile) {
            std::cerr << "Error: failed to finalize file.\n";
            return 1;
        }

        std::cout << "Binary file created successfully.\n";
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}