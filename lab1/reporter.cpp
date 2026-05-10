/**
 * Reporter utility: generates a text report from a binary employee file.
 * Command line: reporter.exe <input_binary_file> <report_file> <hourly_rate>
 * The report lists employees sorted by ID with calculated salary.
 */

#include "employee.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <string>
#include <iomanip>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: reporter.exe <input_binary_file> <report_file> <hourly_rate>\n";
            return 1;
        }

        const std::string inputFile = argv[1];
        const std::string reportFile = argv[2];
        const double hourlyRate = std::stod(argv[3]);
        if (hourlyRate < 0) {
            std::cerr << "Error: hourly rate must be non-negative.\n";
            return 1;
        }

        // Read binary file
        std::ifstream inFile(inputFile, std::ios::binary);
        if (!inFile) {
            std::cerr << "Error: cannot open input file '" << inputFile << "'.\n";
            return 1;
        }

        std::vector<Employee> employees;
        Employee temp{};
        while (inFile.read(reinterpret_cast<char*>(&temp), sizeof(temp))) {
            employees.push_back(temp);
        }
        if (!inFile.eof()) {
            std::cerr << "Error: reading input file failed before end of file.\n";
            return 1;
        }
        inFile.close();

        // Sort by employee number
        std::sort(employees.begin(), employees.end(),
            [](const Employee& a, const Employee& b) { return a.num < b.num; });

        // Write report
        std::ofstream outFile(reportFile);
        if (!outFile) {
            std::cerr << "Error: cannot create report file '" << reportFile << "'.\n";
            return 1;
        }

        outFile << "Report on file \"" << inputFile << "\"\n";
        outFile << "ID  Name       Hours  Salary\n";
        outFile << std::fixed << std::setprecision(2);

        for (const auto& emp : employees) {
            double salary = emp.hours * hourlyRate;
            outFile << emp.num << " "
                << emp.name << " "
                << emp.hours << " "
                << salary << "\n";
            if (!outFile) {
                std::cerr << "Error: failed to write to report file.\n";
                return 1;
            }
        }

        outFile.close();
        std::cout << "Report created successfully.\n";
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}