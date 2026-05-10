/**
 * Main program: orchestrates Creator and Reporter utilities.
 * 1. Asks for binary file name and record count.
 * 2. Runs Creator, waits for it.
 * 3. Prints contents of the binary file.
 * 4. Asks for report file name and hourly rate.
 * 5. Runs Reporter, waits for it.
 * 6. Prints the report file.
 */

#include "employee.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

 // Custom deleter for HANDLE to use with unique_ptr
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) {
            CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

namespace {
    void PrintBinaryFile(const std::string& filename) {
        std::ifstream inFile(filename, std::ios::binary);
        if (!inFile) {
            std::cerr << "Error: cannot open file '" << filename << "' for reading.\n";
            return;
        }

        std::cout << "Contents of binary file \"" << filename << "\":\n";
        std::cout << "ID  Name       Hours\n";
        Employee emp{};
        while (inFile.read(reinterpret_cast<char*>(&emp), sizeof(emp))) {
            std::cout << emp.num << " " << emp.name << " " << emp.hours << "\n";
        }
        inFile.close();
    }

    void PrintTextFile(const std::string& filename) {
        std::ifstream inFile(filename);
        if (!inFile) {
            std::cerr << "Error: cannot open report file '" << filename << "'.\n";
            return;
        }
        std::string line;
        while (std::getline(inFile, line)) {
            std::cout << line << '\n';
        }
        inFile.close();
    }

    // Runs a process and returns its handle.
    UniqueHandle StartProcess(const std::string& commandLine) {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        // CreateProcessA requires a modifiable command line buffer
        std::vector<char> cmdBuffer(commandLine.begin(), commandLine.end());
        cmdBuffer.push_back('\0');

        if (!CreateProcessA(
            nullptr,          // application name (derived from command line)
            cmdBuffer.data(), // command line
            nullptr,          // process security attributes
            nullptr,          // thread security attributes
            FALSE,            // inherit handles
            0,                // creation flags
            nullptr,          // environment
            nullptr,          // current directory
            &si,
            &pi)) {
            DWORD error = GetLastError();
            std::cerr << "Error: CreateProcess failed with error code " << error << "\n";
            return nullptr;
        }

        CloseHandle(pi.hThread); // we don't need the thread handle
        return UniqueHandle(pi.hProcess);
    }

    bool WaitForProcess(HANDLE hProcess, DWORD& exitCode) {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD code;
        if (FALSE == GetExitCodeProcess(hProcess, &code)) {
            std::cerr << "Error: GetExitCodeProcess failed.\n";
            return false;
        }
        exitCode = code;
        return true;
    }
}

int main() {
    try {
        std::string binaryFile;
        int recordCount = 0;

        std::cout << "Enter binary file name: ";
        std::getline(std::cin, binaryFile);
        std::cout << "Enter number of records: ";
        std::cin >> recordCount;
        std::cin.ignore(); // ignore newline after number

        if (recordCount <= 0) {
            std::cerr << "Error: number of records must be positive.\n";
            return 1;
        }

        // Prepare and start Creator
        std::string creatorCmd = "creator.exe " + binaryFile + " " + std::to_string(recordCount);
        std::cout << "Starting Creator...\n";
        auto hCreator = StartProcess(creatorCmd);
        if (!hCreator) {
            return 1;
        }

        DWORD creatorExitCode = 0;
        if (!WaitForProcess(hCreator.get(), creatorExitCode)) {
            return 1;
        }
        if (creatorExitCode != 0) {
            std::cerr << "Creator exited with error code " << creatorExitCode << "\n";
            return 1;
        }
        std::cout << "Creator finished successfully.\n";

        // Display binary file contents
        PrintBinaryFile(binaryFile);

        // Ask for report parameters
        std::string reportFile;
        double hourlyRate = 0.0;

        std::cout << "\nEnter report file name: ";
        std::getline(std::cin, reportFile);
        std::cout << "Enter hourly rate: ";
        std::cin >> hourlyRate;
        std::cin.ignore();

        if (hourlyRate < 0) {
            std::cerr << "Error: hourly rate cannot be negative.\n";
            return 1;
        }

        // Prepare and start Reporter
        std::string reporterCmd = "reporter.exe " + binaryFile + " " +
            reportFile + " " + std::to_string(hourlyRate);
        std::cout << "Starting Reporter...\n";
        auto hReporter = StartProcess(reporterCmd);
        if (!hReporter) {
            return 1;
        }

        DWORD reporterExitCode = 0;
        if (!WaitForProcess(hReporter.get(), reporterExitCode)) {
            return 1;
        }
        if (reporterExitCode != 0) {
            std::cerr << "Reporter exited with error code " << reporterExitCode << "\n";
            return 1;
        }
        std::cout << "Reporter finished successfully.\n";

        // Display report
        std::cout << "\nReport contents:\n";
        PrintTextFile(reportFile);

    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in main: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}