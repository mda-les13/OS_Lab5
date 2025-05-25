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

        if (choice == 1 || choice == 2) {
            int op = (choice == 1) ? 0 : 1;
            WriteFile(hPipe, &op, sizeof(op), NULL, NULL);
            WriteFile(hPipe, &num, sizeof(num), NULL, NULL);

            employee e;
            DWORD dwRead;
            ReadFile(hPipe, &e, sizeof(e), &dwRead, NULL);

            if (op == 0) { // READ
                std::cout << "Employee Data:\n"
                    << "ID: " << e.num << "\n"
                    << "Name: " << e.name << "\n"
                    << "Hours: " << e.hours << "\n"
                    << "Press Enter to release the file...\n";
                std::cin.get(); std::cin.get();
                int confirm = 1;
                WriteFile(hPipe, &confirm, sizeof(confirm), NULL, NULL);
            }
            else if (op == 1) { // WRITE
                std::cout << "Current record:\n"
                    << "ID: " << e.num << "\n"
                    << "Name: " << e.name << "\n"
                    << "Hours: " << e.hours << "\n"
                    << "Enter new data (ID Name Hours): ";
                employee newData;
                std::cin >> newData.num >> newData.name >> newData.hours;
                WriteFile(hPipe, &newData, sizeof(newData), NULL, NULL);
                std::cout << "Record updated successfully. Press Enter to release the file...\n";
                std::cin.get(); std::cin.get();
                int confirm = 1;
                WriteFile(hPipe, &confirm, sizeof(confirm), NULL, NULL);
            }
        }
    }

    CloseHandle(hPipe);
    std::cout << "Client exiting...\n";
    return 0;
}