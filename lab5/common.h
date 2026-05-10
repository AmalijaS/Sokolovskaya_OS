#ifndef COMMON_H
#define COMMON_H

#include <windows.h>

struct Employee {
    int num;           // идентификационный номер
    char name[10];     // имя (9 символов + '\0')
    double hours;      // отработанные часы
};

// Типы операций
enum class Operation : int {
    READ = 1,
    MODIFY = 2,
    EXIT = 3
};

// Тип блокировки, используемый только на сервере
enum class LockType : int {
    NONE = 0,
    READ = 1,
    WRITE = 2
};

// Запрос от клиента к серверу
struct Request {
    Operation op;       // операция
    int key;            // ID сотрудника
    Employee data;      // данные (при модификации)
};

// Ответ от сервера клиенту
struct Response {
    bool success;       // успешность выполнения
    char message[64];   // сообщение об ошибке или статус
    Employee data;      // запись сотрудника
};

constexpr DWORD PIPE_TIMEOUT = 5000;      // таймаут ожидания клиента
constexpr char PIPE_NAME[] = "\\\\.\\pipe\\EmployeePipe";

#endif // COMMON_H