#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <string>

namespace Config {
    const int MSG_SIZE = 20;              // максимальная длина сообщения (без нуль-терминатора)
    const int MAX_SENDERS = 10;           // максимальное количество процессов Sender
    const std::string MUTEX_NAME = "FileMutex";
    const std::string NOT_EMPTY_EVENT = "NotEmptyEvt";
    const std::string NOT_FULL_EVENT = "NotFullEvt";
    const std::string SENDER_READY_PREFIX = "SenderReadyEvt_"; // + номер
}

// Структура сообщения в файле (20 символов + признак занятости)
#pragma pack(push, 1)
struct Message {
    char text[Config::MSG_SIZE]; // сообщение
    bool occupied;               // true, если ячейка занята
};
#pragma pack(pop)

// Заголовок файла
struct QueueHeader {
    int totalMessages;     // общее количество записей (ёмкость)
    int head;              // индекс для чтения
    int tail;              // индекс для записи
    int currentCount;      // текущее количество занятых ячеек
};

#endif // COMMON_H