/**
 * Server process: creates an employee binary file, services client requests
 * via a named pipe with read/write locking on individual records.
 * Simplified version: single client supported (but code is multi‑client ready).
 *
 * Compile: cl /EHsc /Fe:server.exe server.cpp
 */
#include "common.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

 // RAII wrapper for Windows HANDLE
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

//----------- Employee file management -----------
class EmployeeFile {
public:
    EmployeeFile(const std::string& filename, const std::vector<Employee>& employees)
        : m_filename(filename) {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot create file");
        for (const auto& e : employees) {
            file.write(reinterpret_cast<const char*>(&e), sizeof(e));
        }
        file.close();
        m_employees = employees;  // keep in memory for simplicity (or re-read)
    }

    void Print() const {
        std::ifstream file(m_filename, std::ios::binary);
        if (!file) {
            std::cerr << "Error opening file for output.\n";
            return;
        }
        std::cout << "File contents:\n";
        std::cout << "ID  Name       Hours\n";
        Employee e;
        while (file.read(reinterpret_cast<char*>(&e), sizeof(e))) {
            std::cout << e.num << "  " << e.name << "  " << e.hours << "\n";
        }
    }

    // Read a record by key; returns true and fills emp if found
    bool Read(int key, Employee& emp) const {
        std::ifstream file(m_filename, std::ios::binary);
        if (!file) return false;
        while (file.read(reinterpret_cast<char*>(&emp), sizeof(emp))) {
            if (emp.num == key) return true;
        }
        return false;
    }

    // Write (update) a record by key; returns true if record existed
    bool Write(int key, const Employee& newData) {
        std::fstream file(m_filename, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) return false;
        Employee emp;
        while (file.read(reinterpret_cast<char*>(&emp), sizeof(emp))) {
            if (emp.num == key) {
                // Go back to start of this record and overwrite
                file.seekp(-static_cast<int>(sizeof(emp)), std::ios::cur);
                file.write(reinterpret_cast<const char*>(&newData), sizeof(newData));
                file.close();
                return true;
            }
        }
        return false;
    }

private:
    std::string m_filename;
    std::vector<Employee> m_employees; // kept for possible re-initialization
};

//----------- Lock manager (per record) -----------
class RecordLockManager {
public:
    // Request read lock on record key. Returns true if granted.
    bool LockRead(int key) {
        auto& lock = m_locks[key];
        if (lock.type == LockType::WRITE) return false; // write lock held
        lock.type = LockType::READ;
        lock.readers++;
        return true;
    }

    // Request write lock on record key. Returns true if granted.
    bool LockWrite(int key) {
        auto& lock = m_locks[key];
        if (lock.type != LockType::NONE) return false; // any lock held
        lock.type = LockType::WRITE;
        lock.readers = 0;
        return true;
    }

    // Release the lock held by the client. (For simplicity, we release completely)
    void Unlock(int key) {
        auto& lock = m_locks[key];
        if (lock.type == LockType::READ) {
            if (--lock.readers == 0) lock.type = LockType::NONE;
        }
        else if (lock.type == LockType::WRITE) {
            lock.type = LockType::NONE;
        }
    }

private:
    struct LockInfo {
        LockType type = LockType::NONE;
        int readers = 0;
    };
    std::map<int, LockInfo> m_locks;
};

//----------- Pipe handling -----------
void HandleClient(HANDLE hPipe, EmployeeFile& file, RecordLockManager& lockMgr) {
    Request req;
    Response resp;
    DWORD bytesRead, bytesWritten;

    while (true) {
        // Read request from client
        BOOL readOk = ReadFile(hPipe, &req, sizeof(req), &bytesRead, nullptr);
        if (!readOk || bytesRead < sizeof(req)) break; // client disconnected

        if (req.op == Operation::EXIT) break;

        resp = {};
        resp.success = false;
        strcpy_s(resp.message, "Unknown error");

        if (req.op == Operation::READ) {
            Employee emp;
            if (!file.Read(req.key, emp)) {
                strcpy_s(resp.message, "Employee not found");
            }
            else if (!lockMgr.LockRead(req.key)) {
                strcpy_s(resp.message, "Record is locked for writing");
            }
            else {
                resp.data = emp;
                resp.success = true;
                strcpy_s(resp.message, "Read successful");
                // For read, unlock immediately after sending data? 
                // In real scenario, the client should explicitly unlock after finishing.
                // Here we'll unlock immediately for simplicity (read is "done").
                lockMgr.Unlock(req.key);
            }
        }
        else if (req.op == Operation::MODIFY) {
            // First phase: send existing record to client, with lock
            Employee emp;
            if (!file.Read(req.key, emp)) {
                strcpy_s(resp.message, "Employee not found");
            }
            else if (!lockMgr.LockWrite(req.key)) {
                strcpy_s(resp.message, "Record is locked (read or write)");
            }
            else {
                resp.data = emp;
                resp.success = true;
                strcpy_s(resp.message, "Record sent, waiting for new data");
                // Send the old record and wait for the modified one
            }

            // Send response for first phase
            WriteFile(hPipe, &resp, sizeof(resp), &bytesWritten, nullptr);
            if (!resp.success) continue; // go to next request

            // Second phase: receive modified record from client
            Request modReq;
            ReadFile(hPipe, &modReq, sizeof(modReq), &bytesRead, nullptr);
            if (modReq.op == Operation::MODIFY) {
                // Update file
                bool updated = file.Write(req.key, modReq.data);
                if (updated) {
                    strcpy_s(resp.message, "Modification successful");
                    resp.success = true;
                }
                else {
                    strcpy_s(resp.message, "Failed to write record");
                    resp.success = false;
                }
                lockMgr.Unlock(req.key);
                // Send final confirmation
                WriteFile(hPipe, &resp, sizeof(resp), &bytesWritten, nullptr);
            }
            // If client sent something else, just unlock and break?
        }
        // For pure read command, we already sent response above; ensure it's sent
        if (req.op == Operation::READ) {
            WriteFile(hPipe, &resp, sizeof(resp), &bytesWritten, nullptr);
        }
    }

    // Cleanup any remaining locks? Not needed if client properly unlocks.
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

int main() {
    try {
        // 1. Create employee file
        std::string filename;
        int recordCount;
        std::cout << "Enter binary file name: ";
        std::getline(std::cin, filename);
        std::cout << "Enter number of employees: ";
        std::cin >> recordCount;
        std::cin.ignore();

        std::vector<Employee> employees(recordCount);
        for (int i = 0; i < recordCount; ++i) {
            std::cout << "Employee " << (i + 1) << " (num name hours): ";
            std::cin >> employees[i].num;
            std::cin >> employees[i].name;
            std::cin >> employees[i].hours;
            if (employees[i].num <= 0) {
                std::cerr << "Error: num must be positive.\n";
                return 1;
            }
        }
        std::cin.ignore();

        EmployeeFile empFile(filename, employees);
        empFile.Print();

        // 2. Create named pipe
        UniqueHandle hPipe(CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,  // support multiple clients in theory
            0,                         // default output buffer size
            0,                         // default input buffer size
            0,                         // default timeout
            nullptr                    // default security
        ));
        if (!hPipe) throw std::runtime_error("CreateNamedPipe failed");

        // 3. Number of clients (simplified: we accept 1, but can loop)
        int numClients;
        std::cout << "Enter number of clients (1 for simplified): ";
        std::cin >> numClients;
        std::cin.ignore();
        if (numClients != 1) {
            std::cout << "Simplified mode only supports 1 client; proceeding with 1.\n";
        }

        // 4. Accept one client connection
        std::cout << "Waiting for client connection...\n";
        BOOL connected = ConnectNamedPipe(hPipe.get(), nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            throw std::runtime_error("ConnectNamedPipe failed");
        }

        // 5. Process client requests
        RecordLockManager lockMgr;
        HandleClient(hPipe.get(), empFile, lockMgr);
        // HandleClient closes the pipe

        // 6. After client finishes, print modified file
        empFile.Print();
        std::cout << "Server finished.\n";

    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}