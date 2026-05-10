/**
 * Sender process: synchronizes with Receiver via events and sends messages
 * to the shared binary file (ring buffer).
 *
 * Compile: cl /EHsc /Fe:sender.exe sender.cpp
 */
#include "common.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>

struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: sender.exe <filename> <readyEventName>\n";
            return 1;
        }
        std::string filename = argv[1];
        std::string readyEventName = argv[2];

        // Открываем синхронизирующие объекты (созданы Receiver)
        UniqueHandle hMutex(OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, Config::MUTEX_NAME.c_str()));
        if (!hMutex) throw std::runtime_error("OpenMutex failed");

        UniqueHandle hNotEmpty(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, Config::NOT_EMPTY_EVENT.c_str()));
        if (!hNotEmpty) throw std::runtime_error("OpenEvent NotEmpty failed");

        UniqueHandle hNotFull(OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, Config::NOT_FULL_EVENT.c_str()));
        if (!hNotFull) throw std::runtime_error("OpenEvent NotFull failed");

        UniqueHandle hReadyEvent(OpenEventA(EVENT_MODIFY_STATE, FALSE, readyEventName.c_str()));
        if (!hReadyEvent) throw std::runtime_error("OpenEvent readyEvent failed");

        // Сигнализируем готовность
        SetEvent(hReadyEvent.get());

        std::cout << "Sender ready.\n";
        bool running = true;
        while (running) {
            std::cout << "\nCommand (s - send message, q - quit): ";
            char cmd;
            std::cin >> cmd;
            std::cin.ignore();
            if (cmd == 'q') {
                running = false;
            }
            else if (cmd == 's') {
                std::cout << "Enter message (max " << Config::MSG_SIZE - 1 << " chars): ";
                std::string msgStr;
                std::getline(std::cin, msgStr);
                if (msgStr.size() >= Config::MSG_SIZE) {
                    msgStr.resize(Config::MSG_SIZE - 1);
                }
                // Дожидаемся, пока очередь не переполнится
                WaitForSingleObject(hNotFull.get(), INFINITE);
                WaitForSingleObject(hMutex.get(), INFINITE);

                std::fstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
                if (!file) {
                    ReleaseMutex(hMutex.get());
                    throw std::runtime_error("Cannot open file for writing");
                }
                QueueHeader hdr;
                file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
                if (hdr.currentCount >= hdr.totalMessages) {
                    // Очередь заполнена – подождали NotFull, значит, так не должно быть, но проверяем
                    ReleaseMutex(hMutex.get());
                    continue;
                }
                // Записываем в позицию tail
                Message msg;
                memset(&msg, 0, sizeof(msg));
                strncpy_s(msg.text, msgStr.c_str(), Config::MSG_SIZE - 1);
                msg.occupied = true;
                file.seekp(sizeof(QueueHeader) + hdr.tail * sizeof(Message));
                file.write(reinterpret_cast<const char*>(&msg), sizeof(msg));
                // Обновляем заголовок
                hdr.tail = (hdr.tail + 1) % hdr.totalMessages;
                hdr.currentCount++;
                file.seekp(0);
                file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
                file.flush();
                file.close();

                // Сигнализируем, что очередь не пуста
                SetEvent(hNotEmpty.get());
                // Если очередь заполнена, сбрасываем NotFull
                if (hdr.currentCount == hdr.totalMessages) {
                    ResetEvent(hNotFull.get());
                }
                ReleaseMutex(hMutex.get());
                std::cout << "Message sent.\n";
            }
            else {
                std::cout << "Unknown command.\n";
            }
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}