/**
 * Receiver process: creates a binary message queue, launches Sender processes,
 * synchronizes with them and reads messages on command.
 *
 * Compile: cl /EHsc /Fe:receiver.exe receiver.cpp
 */
#include "common.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>

 // RAII wrapper for HANDLE
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

int main() {
    try {
        // 1. Ввод параметров
        std::string filename;
        int queueSize, numSenders;
        std::cout << "Enter binary file name: ";
        std::getline(std::cin, filename);
        std::cout << "Enter number of messages (queue size): ";
        std::cin >> queueSize;
        if (queueSize <= 0) throw std::runtime_error("Queue size must be positive");
        std::cout << "Enter number of Sender processes: ";
        std::cin >> numSenders;
        if (numSenders <= 0 || numSenders > Config::MAX_SENDERS)
            throw std::runtime_error("Invalid number of senders");
        std::cin.ignore(); // убрать остаток строки

        // 2. Создание бинарного файла с заголовком и сообщениями
        {
            std::ofstream file(filename, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("Cannot create file");
            QueueHeader hdr{};
            hdr.totalMessages = queueSize;
            hdr.head = 0;
            hdr.tail = 0;
            hdr.currentCount = 0;
            file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            Message emptyMsg{};
            emptyMsg.occupied = false;
            for (int i = 0; i < queueSize; ++i) {
                file.write(reinterpret_cast<const char*>(&emptyMsg), sizeof(emptyMsg));
            }
            file.close();
        }

        // 3. Создание синхронизирующих объектов
        UniqueHandle hMutex(CreateMutexA(nullptr, FALSE, Config::MUTEX_NAME.c_str()));
        if (!hMutex) throw std::runtime_error("CreateMutex failed");

        UniqueHandle hNotEmpty(CreateEventA(nullptr, TRUE, FALSE, Config::NOT_EMPTY_EVENT.c_str()));
        if (!hNotEmpty) throw std::runtime_error("CreateEvent NotEmpty failed");

        UniqueHandle hNotFull(CreateEventA(nullptr, TRUE, TRUE, Config::NOT_FULL_EVENT.c_str()));
        if (!hNotFull) throw std::runtime_error("CreateEvent NotFull failed");
        // NotFull изначально сигнален, потому что очередь пуста

        // События готовности для каждого Sender (имена: SenderReadyEvt_1, SenderReadyEvt_2, ...)
        std::vector<UniqueHandle> senderReadyEvents;
        for (int i = 0; i < numSenders; ++i) {
            std::string evtName = Config::SENDER_READY_PREFIX + std::to_string(i + 1);
            UniqueHandle evt(CreateEventA(nullptr, TRUE, FALSE, evtName.c_str()));
            if (!evt) throw std::runtime_error("CreateEvent SenderReady failed");
            senderReadyEvents.push_back(std::move(evt));
        }

        // 4. Запуск процессов Sender
        for (int i = 0; i < numSenders; ++i) {
            std::string readyEventName = Config::SENDER_READY_PREFIX + std::to_string(i + 1);
            std::string cmdLine = "sender.exe " + filename + " " + readyEventName;
            STARTUPINFOA si{ sizeof(si) };
            PROCESS_INFORMATION pi{};
            if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()),
                nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                DWORD err = GetLastError();
                throw std::runtime_error("CreateProcess failed with code " + std::to_string(err));
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            // Дескрипторы процессов нам не нужны, мы не ждём их завершения активно
        }

        // 5. Ожидание готовности всех Sender
        std::vector<HANDLE> readyHandles;
        for (auto& evt : senderReadyEvents) readyHandles.push_back(evt.get());
        DWORD waitResult = WaitForMultipleObjects(numSenders, readyHandles.data(), TRUE, INFINITE);
        if (waitResult == WAIT_FAILED) throw std::runtime_error("Wait for sender readiness failed");
        std::cout << "All Sender processes are ready.\n";

        // 6. Основной цикл работы Receiver
        bool running = true;
        while (running) {
            std::cout << "\nCommand (r - read message, q - quit): ";
            char cmd;
            std::cin >> cmd;
            std::cin.ignore();
            if (cmd == 'q') {
                running = false;
            }
            else if (cmd == 'r') {
                // Ждём, пока очередь не станет непустой
                WaitForSingleObject(hNotEmpty.get(), INFINITE);
                // Захватываем мьютекс
                WaitForSingleObject(hMutex.get(), INFINITE);

                // Открываем файл и читаем из головы
                std::fstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
                if (!file) {
                    ReleaseMutex(hMutex.get());
                    throw std::runtime_error("Cannot open file for reading");
                }
                QueueHeader hdr;
                file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
                if (hdr.currentCount <= 0) {
                    // Этого не должно случиться, но на всякий случай
                    ReleaseMutex(hMutex.get());
                    continue;
                }
                // Перемещаемся к позиции head
                file.seekg(sizeof(QueueHeader) + hdr.head * sizeof(Message));
                Message msg;
                file.read(reinterpret_cast<char*>(&msg), sizeof(msg));
                // Выводим сообщение
                std::cout << "Received: " << msg.text << std::endl;
                // Обновляем заголовок
                msg.occupied = false;
                hdr.head = (hdr.head + 1) % hdr.totalMessages;
                hdr.currentCount--;

                // Записываем изменения
                file.seekp(0);
                file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
                file.seekp(sizeof(QueueHeader) + ((hdr.head - 1 + hdr.totalMessages) % hdr.totalMessages) * sizeof(Message));
                file.write(reinterpret_cast<const char*>(&msg), sizeof(msg));
                file.flush();
                file.close();

                // Сигнализируем, что очередь не полна (освободилось место)
                SetEvent(hNotFull.get());
                // Если очередь опустела, сбрасываем событие NotEmpty
                if (hdr.currentCount == 0) {
                    ResetEvent(hNotEmpty.get());
                }

                ReleaseMutex(hMutex.get());
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