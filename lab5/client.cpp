/**
 * Client process: connects to the server's named pipe, requests read/modify
 * operations on employee records by ID.
 *
 * Compile: cl /EHsc /Fe:client.exe client.cpp
 */
#include "common.h"
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>

struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

bool SendRequest(HANDLE hPipe, const Request& req, Response& resp) {
    DWORD bytesWritten;
    if (!WriteFile(hPipe, &req, sizeof(req), &bytesWritten, nullptr) || bytesWritten != sizeof(req)) {
        std::cerr << "Failed to send request.\n";
        return false;
    }
    DWORD bytesRead;
    if (!ReadFile(hPipe, &resp, sizeof(resp), &bytesRead, nullptr) || bytesRead != sizeof(resp)) {
        std::cerr << "Failed to receive response.\n";
        return false;
    }
    return true;
}

int main() {
    try {
        // Connect to pipe
        UniqueHandle hPipe(CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,              // no sharing
            nullptr,        // default security
            OPEN_EXISTING,
            0,              // default attributes
            nullptr         // no template
        ));
        if (!hPipe) {
            DWORD err = GetLastError();
            throw std::runtime_error("Cannot connect to pipe. Error: " + std::to_string(err));
        }
        std::cout << "Connected to server.\n";

        bool running = true;
        while (running) {
            std::cout << "\nOperation (1 - read, 2 - modify, 3 - exit): ";
            int opCode;
            std::cin >> opCode;
            std::cin.ignore();
            if (opCode == 3) {
                Request exitReq{ Operation::EXIT, 0 };
                Response dummy;
                SendRequest(hPipe.get(), exitReq, dummy);
                running = false;
                break;
            }

            Request req;
            Response resp;
            req.op = static_cast<Operation>(opCode);
            std::cout << "Enter employee ID: ";
            std::cin >> req.key;
            std::cin.ignore();

            if (opCode == 1) { // read
                if (!SendRequest(hPipe.get(), req, resp)) break;
                if (resp.success) {
                    std::cout << "Employee data:\n";
                    std::cout << "ID: " << resp.data.num << "\nName: " << resp.data.name
                        << "\nHours: " << resp.data.hours << "\n";
                }
                else {
                    std::cout << "Error: " << resp.message << "\n";
                }
            }
            else if (opCode == 2) { // modify
                // Send initial request
                if (!SendRequest(hPipe.get(), req, resp)) break;
                if (!resp.success) {
                    std::cout << "Error: " << resp.message << "\n";
                    continue;
                }
                // Display old data
                std::cout << "Current data: ID " << resp.data.num << ", Name "
                    << resp.data.name << ", Hours " << resp.data.hours << "\n";
                // Input new data
                Employee newData;
                newData.num = resp.data.num; // ID cannot be changed
                std::cout << "Enter new name (max 9 chars): ";
                std::cin.getline(newData.name, 10);
                std::cout << "Enter new hours: ";
                std::cin >> newData.hours;
                std::cin.ignore();
                // Send modified record as a new request of type MODIFY
                Request modReq;
                modReq.op = Operation::MODIFY;
                modReq.key = req.key;
                modReq.data = newData;
                Response finalResp;
                if (!SendRequest(hPipe.get(), modReq, finalResp)) break;
                if (finalResp.success) {
                    std::cout << "Record updated.\n";
                }
                else {
                    std::cout << "Error: " << finalResp.message << "\n";
                }
            }
            else {
                std::cout << "Unknown operation.\n";
            }
        }
        std::cout << "Client exiting.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}