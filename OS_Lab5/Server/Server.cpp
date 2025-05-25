#include <windows.h>
#include <iostream>
#include <fstream>
#include <map>
#include <tchar.h>

struct employee {
    int num;
    char name[10];
    double hours;
};

// Структура для управления блокировкой записи
struct RecordLock {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE readerCV;
    CONDITION_VARIABLE writerCV;
    int readers;
    int writers_waiting;
    bool writer;
};

// Карта блокировок для каждой записи
std::map<int, RecordLock*> recordLocks;
CRITICAL_SECTION g_cs;

// Инициализация блокировки для конкретной записи
void InitializeRecordLock(int num) {
    EnterCriticalSection(&g_cs);
    if (recordLocks.find(num) == recordLocks.end()) {
        RecordLock* lock = new RecordLock();
        InitializeCriticalSection(&lock->cs);
        InitializeConditionVariable(&lock->readerCV);
        InitializeConditionVariable(&lock->writerCV);
        lock->readers = 0;
        lock->writers_waiting = 0;
        lock->writer = false;
        recordLocks[num] = lock;
    }
    LeaveCriticalSection(&g_cs);
}

// Блокировка на чтение
void AcquireReadLock(int num) {
    InitializeRecordLock(num);
    RecordLock* lock = recordLocks[num];
    EnterCriticalSection(&lock->cs);
    while (lock->writer || lock->writers_waiting > 0) {
        SleepConditionVariableCS(&lock->readerCV, &lock->cs, INFINITE);
    }
    lock->readers++;
    LeaveCriticalSection(&lock->cs);
}

// Освобождение блокировки на чтение
void ReleaseReadLock(int num) {
    RecordLock* lock = recordLocks[num];
    EnterCriticalSection(&lock->cs);
    lock->readers--;
    if (lock->readers == 0 && lock->writers_waiting > 0) {
        WakeConditionVariable(&lock->writerCV);
    }
    LeaveCriticalSection(&lock->cs);
}

// Блокировка на запись
void AcquireWriteLock(int num) {
    InitializeRecordLock(num);
    RecordLock* lock = recordLocks[num];
    EnterCriticalSection(&lock->cs);
    lock->writers_waiting++;
    while (lock->readers > 0 || lock->writer) {
        SleepConditionVariableCS(&lock->writerCV, &lock->cs, INFINITE);
    }
    lock->writers_waiting--;
    lock->writer = true;
    LeaveCriticalSection(&lock->cs);
}

// Освобождение блокировки на запись
void ReleaseWriteLock(int num) {
    RecordLock* lock = recordLocks[num];
    EnterCriticalSection(&lock->cs);
    lock->writer = false;
    if (lock->writers_waiting > 0) {
        WakeConditionVariable(&lock->writerCV);
    }
    else {
        WakeAllConditionVariable(&lock->readerCV);
    }
    LeaveCriticalSection(&lock->cs);
}

// Обработчик клиента
DWORD WINAPI ClientHandler(LPVOID lpParam) {
    HANDLE hPipe = (HANDLE)lpParam;
    DWORD dwRead;
    int op;

    while (ReadFile(hPipe, &op, sizeof(op), &dwRead, NULL)) {
        if (op == 2) break; // EXIT

        int num;
        ReadFile(hPipe, &num, sizeof(num), &dwRead, NULL);

        if (op == 0) { // READ
            AcquireReadLock(num);
            employee e;
            bool found = false;
            std::ifstream file("employees.dat", std::ios::binary);
            while (file.read((char*)&e, sizeof(employee))) {
                if (e.num == num) {
                    found = true;
                    break;
                }
            }
            file.close();
            WriteFile(hPipe, found ? &e : new employee(), sizeof(employee), &dwRead, NULL);

            // Ожидаем подтверждение от клиента (Enter)
            int confirm = 0;
            ReadFile(hPipe, &confirm, sizeof(confirm), &dwRead, NULL);

            ReleaseReadLock(num);
        }
        else if (op == 1) { // WRITE
            AcquireWriteLock(num);
            employee current;
            bool found = false;
            std::ifstream file("employees.dat", std::ios::binary);
            while (file.read((char*)&current, sizeof(employee))) {
                if (current.num == num) {
                    found = true;
                    break;
                }
            }
            file.close();
            WriteFile(hPipe, &current, sizeof(current), &dwRead, NULL);

            employee newData;
            ReadFile(hPipe, &newData, sizeof(employee), &dwRead, NULL);
            std::fstream file2("employees.dat", std::ios::binary | std::ios::in | std::ios::out);
            employee e;
            while (file2.read((char*)&e, sizeof(employee))) {
                if (e.num == newData.num) {
                    file2.seekp(-static_cast<long>(sizeof(employee)), std::ios::cur);
                    file2.write((char*)&newData, sizeof(employee));
                    break;
                }
            }
            file2.close();

            // Ожидаем подтверждение от клиента (Enter)
            int confirm = 0;
            ReadFile(hPipe, &confirm, sizeof(confirm), &dwRead, NULL);

            ReleaseWriteLock(num);
        }
    }

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

int main() {
    // Инициализация критических секций
    InitializeCriticalSection(&g_cs);

    // Создание файла
    std::ofstream file("employees.dat", std::ios::binary);
    int numRecords;
    std::cout << "Enter the number of employee records: ";
    std::cin >> numRecords;
    for (int i = 0; i < numRecords; ++i) {
        employee emp;
        std::cout << "Enter employee data (num name hours): ";
        std::cin >> emp.num >> emp.name >> emp.hours;
        file.write((char*)&emp, sizeof(emp));
    }
    file.close();

    // Запуск клиентов
    int clientCount;
    std::cout << "Enter the number of client processes to start: ";
    std::cin >> clientCount;

    for (int i = 0; i < clientCount; ++i) {
        STARTUPINFO si = { sizeof(STARTUPINFO) };
        PROCESS_INFORMATION pi;

        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;

        TCHAR clientPath[] = TEXT("Client.exe");

        if (!CreateProcess(
            clientPath,
            NULL,
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_CONSOLE,
            NULL,
            NULL,
            &si,
            &pi)) {
            std::cerr << "Failed to start client process. Error: " << GetLastError() << std::endl;
            continue;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Создание канала
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    std::cout << "Server is waiting for client connections...\n";

    while (true) {
        hPipe = CreateNamedPipe(
            TEXT("\\\\.\\pipe\\EmployeePipe"),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            0, 0, INFINITE, NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create named pipe. Error: " << GetLastError() << std::endl;
            break;
        }

        if (!ConnectNamedPipe(hPipe, NULL)) {
            std::cerr << "Failed to connect client. Error: " << GetLastError() << std::endl;
            CloseHandle(hPipe);
            continue;
        }

        DWORD threadId;
        CreateThread(NULL, 0, ClientHandler, hPipe, 0, &threadId);
    }

    // Вывод обновленного файла
    std::cout << "\nModified employee records:\n";
    std::ifstream fin("employees.dat", std::ios::binary);
    employee emp;
    while (fin.read((char*)&emp, sizeof(emp))) {
        std::cout << emp.num << " " << emp.name << " " << emp.hours << std::endl;
    }
    fin.close();

    DeleteCriticalSection(&g_cs);
    std::cout << "\nServer exiting. Press Enter to close...";
    std::cin.get(); std::cin.get();
    return 0;
}