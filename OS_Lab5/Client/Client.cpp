#include <windows.h>
#include <iostream>
#include <tchar.h>

struct employee {
    int num;
    char name[10];
    double hours;
};

int main() {
    HANDLE hPipe = CreateFile(
        TEXT("\\\\.\\pipe\\EmployeePipe"),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "Connection failed. Ensure server is running.\n";
        return 1;
    }

    std::cout << "Connected to server.\n";

    int choice;
    while (true) {
        std::cout << "\nChoose operation:\n"
            << "1. Read employee\n"
            << "2. Modify employee\n"
            << "3. Exit\n"
            << "Enter choice: ";
        std::cin >> choice;

        if (choice == 3) {
            int op = 2; // EXIT
            WriteFile(hPipe, &op, sizeof(op), NULL, NULL);
            break;
        }

        int num;
        std::cout << "Enter employee ID: ";
        std::cin >> num;

        int status;
        if (choice == 1 || choice == 2) {
            int op = (choice == 1) ? 0 : 1;
            WriteFile(hPipe, &op, sizeof(op), NULL, NULL);
            WriteFile(hPipe, &num, sizeof(num), NULL, NULL);

            ReadFile(hPipe, &status, sizeof(status), NULL, NULL);
            if (status == 1) {
                std::cout << "File is currently in use by another client. Please try again later.\n";
                continue;
            }

            if (op == 0) { // READ
                employee e;
                DWORD dwRead;
                ReadFile(hPipe, &e, sizeof(e), &dwRead, NULL);
                std::cout << "Employee Data:\n"
                    << "ID: " << e.num << "\n"
                    << "Name: " << e.name << "\n"
                    << "Hours: " << e.hours << "\n";
            }
            else if (op == 1) { // WRITE
                employee current;
                DWORD dwRead;
                ReadFile(hPipe, &current, sizeof(current), &dwRead, NULL);
                std::cout << "Current record:\n"
                    << "ID: " << current.num << "\n"
                    << "Name: " << current.name << "\n"
                    << "Hours: " << current.hours << "\n"
                    << "Enter new data (ID Name Hours): ";
                std::cin >> current.num >> current.name >> current.hours;
                WriteFile(hPipe, &current, sizeof(current), NULL, NULL);
                std::cout << "Record updated successfully. Press Enter to release the file...\n";
                std::cin.get(); std::cin.get();
            }

            int confirm = 1;
            WriteFile(hPipe, &confirm, sizeof(confirm), NULL, NULL);
        }
    }

    CloseHandle(hPipe);
    std::cout << "Client exiting...\n";
    return 0;
}