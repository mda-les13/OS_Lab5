#include <windows.h>
#include <iostream>
#include <fstream>
#include <map>
#include <thread>
#include <atomic>
#include <tchar.h>

struct employee {
    int num;
    char name[10];
    double hours;
};

// ��������� ��� ���������� ����������� ������
struct RecordLock {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE readerCV;
    CONDITION_VARIABLE writerCV;
    int readers;
    int writers_waiting;
    bool writer;
};

// ����� ���������� ��� ������ ������
std::map<int, RecordLock*> recordLocks;
CRITICAL_SECTION g_cs;

// ���������� ���������� ��� ������������ ��������
int activeClients = 0;
CRITICAL_SECTION clientCountCS;
bool allClientsDone = false;

// ���� ���������� ������ �������
std::atomic<bool> exitRequested(false);

// ������ ������� ��� ������� exit
HANDLE hExitEvent = nullptr;

// ������������� ���������� ��� ���������� ������
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

// ���������� �� ������
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

void ReleaseReadLock(int num) {
    RecordLock* lock = recordLocks[num];
    EnterCriticalSection(&lock->cs);
    lock->readers--;
    if (lock->readers == 0 && lock->writers_waiting > 0) {
        WakeConditionVariable(&lock->writerCV);
    }
    LeaveCriticalSection(&lock->cs);
}

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

DWORD WINAPI ClientHandler(LPVOID lpParam) {
    HANDLE hPipe = (HANDLE)lpParam;
    DWORD dwRead;
    int op;

    while (ReadFile(hPipe, &op, sizeof(op), &dwRead, NULL)) {
        if (op == 2) break; // ������� ������

        int num;
        ReadFile(hPipe, &num, sizeof(num), &dwRead, NULL);

        if (op == 0) { // ������ ������
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
            ReleaseReadLock(num);
        }
        else if (op == 1) { // ������ ������
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
            ReleaseWriteLock(num);
        }
    }

    // ��������� ������� �������� ��������
    EnterCriticalSection(&clientCountCS);
    activeClients--;
    if (activeClients == 0) {
        allClientsDone = true;
    }
    LeaveCriticalSection(&clientCountCS);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

// ����� �������� ������� exit
void ExitInputThread() {
    std::string command;
    while (std::cin >> command) {
        if (command == "stop") {
            SetEvent(hExitEvent); // �������� �������
            exitRequested = true;
            break;
        }
    }
}

int main() {
    // ������������� ����������� ������
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&clientCountCS);

    // �������� ������� ��� ������� exit
    hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hExitEvent) {
        std::cerr << "Failed to create exit event.\n";
        return 1;
    }

    // �������� �����
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

    // ������ ��������
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

        // ����������� ������� �������� ��������
        EnterCriticalSection(&clientCountCS);
        activeClients++;
        LeaveCriticalSection(&clientCountCS);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // ������ ������ �������� ������� exit
    std::thread exitThread(ExitInputThread);

    // �������� ������
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    std::cout << "Server is waiting for client connections...\n";

    // ���� �������� ��������
    while (!allClientsDone && !exitRequested) {
        hPipe = CreateNamedPipe(
            TEXT("\\\\.\\pipe\\EmployeePipe"),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            0, 0, INFINITE, NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create named pipe. Error: " << GetLastError() << std::endl;
            break;
        }

        OVERLAPPED overlapped = {};
        overlapped.hEvent = hExitEvent;

        if (!ConnectNamedPipe(hPipe, &overlapped)) {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED && error != ERROR_IO_PENDING) {
                std::cerr << "Failed to connect client. Error: " << error << std::endl;
                CloseHandle(hPipe);
                continue;
            }

            HANDLE waitHandles[] = { overlapped.hEvent, hExitEvent };
            DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

            if (result == WAIT_OBJECT_0 + 1) {
                CancelIo(hPipe);
                DisconnectNamedPipe(hPipe);
                CloseHandle(hPipe);
                break;
            }
        }

        // ������ ����������� �������
        DWORD threadId;
        CreateThread(NULL, 0, ClientHandler, hPipe, 0, &threadId);
    }

    exitThread.join();

    // ����� ������������ �����
    std::cout << "\nModified employee records:\n";
    std::ifstream fin("employees.dat", std::ios::binary);
    employee emp;
    while (fin.read((char*)&emp, sizeof(emp))) {
        std::cout << emp.num << " " << emp.name << " " << emp.hours << std::endl;
    }
    fin.close();

    // �������
    DeleteCriticalSection(&g_cs);
    DeleteCriticalSection(&clientCountCS);
    CloseHandle(hExitEvent);
    std::cout << "\nServer exiting. Press Enter to close...";
    std::cin.get(); std::cin.get(); // �������� ����������
    return 0;
}