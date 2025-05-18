#include <gtest/gtest.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct employee {
    int num;
    char name[10];
    double hours;
};

std::atomic<bool> serverStarted(false);
std::atomic<bool> clientFinished(false);

std::mutex mtx;
std::condition_variable cv;
bool serverReady = false;

void ServerThread() {
    DeleteFile(TEXT("\\\\.\\pipe\\test_pipe")); // Очистка

    HANDLE hPipe = CreateNamedPipe(
        TEXT("\\\\.\\pipe\\test_pipe"),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        0, 0,
        INFINITE,
        NULL);

    ASSERT_NE(hPipe, INVALID_HANDLE_VALUE) << "Failed to create named pipe.";

    std::cout << "[SERVER] Server started and waiting for client..." << std::endl;

    {
        std::lock_guard<std::mutex> lock(mtx);
        serverReady = true;
    }
    cv.notify_one();

    EXPECT_TRUE(ConnectNamedPipe(hPipe, NULL))
        << "ConnectNamedPipe failed. Error: " << GetLastError();

    employee received;
    DWORD dwRead;
    EXPECT_TRUE(ReadFile(hPipe, &received, sizeof(received), &dwRead, NULL))
        << "Failed to read from pipe. Error: " << GetLastError();

    EXPECT_EQ(received.num, 1);
    EXPECT_STREQ(received.name, "Alice");
    EXPECT_DOUBLE_EQ(received.hours, 150.5);

    CloseHandle(hPipe);
    serverStarted = true;
}

void ClientThread() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return serverReady; });
    lock.unlock();

    std::cout << "[CLIENT] Attempting to connect to pipe..." << std::endl;

    HANDLE hPipe = CreateFile(
        TEXT("\\\\.\\pipe\\test_pipe"),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_NE(hPipe, INVALID_HANDLE_VALUE)
        << "Failed to connect to server. Error: " << GetLastError();

    employee send = { 1, "Alice", 150.5 };
    DWORD dwWritten;
    EXPECT_TRUE(WriteFile(hPipe, &send, sizeof(send), &dwWritten, NULL))
        << "Failed to write to pipe. Error: " << GetLastError();

    CloseHandle(hPipe);
    clientFinished = true;
}

TEST(ServerTests, FullClientServerDataExchange) {
    std::thread server(ServerThread);
    std::thread client(ClientThread);

    if (client.joinable()) {
        client.join(); // Ждём клиента
    }

    if (server.joinable()) {
        server.join(); // Ждём сервера
    }

    EXPECT_TRUE(clientFinished.load());
    EXPECT_TRUE(serverStarted.load());
}

// Тест: Клиент не может подключиться, если сервер не запущен
TEST(ServerTests, ClientConnectsToNonExistentPipe) {
    // Клиент пытается подключиться к несуществующему каналу
    HANDLE hPipe = CreateFile(
        TEXT("\\\\.\\pipe\\test_pipe_nonexistent"),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_EQ(hPipe, INVALID_HANDLE_VALUE)
        << "Client should not connect to non-existent pipe";
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
}

// Тест: Асинхронная запись данных через канал
TEST(ServerTests, AsyncWriteToPipe) {
    DeleteFile(TEXT("\\\\.\\pipe\\test_async_pipe"));

    // Создаем асинхронный канал
    HANDLE hPipe = CreateNamedPipe(
        TEXT("\\\\.\\pipe\\test_async_pipe"),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 0, 0, INFINITE, NULL);

    ASSERT_NE(hPipe, INVALID_HANDLE_VALUE)
        << "Failed to create async pipe. Error: " << GetLastError();

    std::thread serverThread([hPipe]() {
        // Ждем подключения клиента
        EXPECT_TRUE(ConnectNamedPipe(hPipe, NULL))
            << "Failed to connect client. Error: " << GetLastError();

        // Читаем данные асинхронно
        employee received;
        DWORD dwRead;
        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        EXPECT_TRUE(ReadFile(hPipe, &received, sizeof(received), &dwRead, &overlapped))
            << "Async read failed. Error: " << GetLastError();

        EXPECT_EQ(WaitForSingleObject(overlapped.hEvent, 5000), WAIT_OBJECT_0);

        EXPECT_EQ(received.num, 2);
        EXPECT_STREQ(received.name, "Bob");
        EXPECT_DOUBLE_EQ(received.hours, 40.0);

        CloseHandle(overlapped.hEvent);
        CloseHandle(hPipe);
        });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Клиент подключается и отправляет данные
    HANDLE hClientPipe = CreateFile(
        TEXT("\\\\.\\pipe\\test_async_pipe"),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    ASSERT_NE(hClientPipe, INVALID_HANDLE_VALUE);

    employee send = { 2, "Bob", 40.0 };
    DWORD dwWritten;

    OVERLAPPED clientOverlapped = {};
    clientOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    EXPECT_TRUE(WriteFile(hClientPipe, &send, sizeof(send), &dwWritten, &clientOverlapped))
        << "Async write failed. Error: " << GetLastError();

    EXPECT_EQ(WaitForSingleObject(clientOverlapped.hEvent, 5000), WAIT_OBJECT_0);

    CloseHandle(clientOverlapped.hEvent);
    CloseHandle(hClientPipe);

    serverThread.join();
}

// Тест: Сервер завершает работу по команде "exit"
TEST(ServerTests, ServerExitsOnExitCommand) {
    DeleteFile(TEXT("\\\\.\\pipe\\test_exit_pipe"));

    std::atomic<bool> serverStarted(false);
    std::atomic<bool> serverExited(false);

    // Сервер
    std::thread serverThread([&]() {
        HANDLE hPipe = CreateNamedPipe(
            TEXT("\\\\.\\pipe\\test_exit_pipe"),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 0, 0, INFINITE, NULL);

        ASSERT_NE(hPipe, INVALID_HANDLE_VALUE);

        serverStarted = true;

        // Ждем команду exit
        if (ConnectNamedPipe(hPipe, NULL)) {
            int op;
            DWORD dwRead;
            if (ReadFile(hPipe, &op, sizeof(op), &dwRead, NULL)) {
                if (op == 2) {
                    serverExited = true;
                }
            }
        }

        CloseHandle(hPipe);
        });

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Клиент отправляет команду exit
    HANDLE hPipe = CreateFile(
        TEXT("\\\\.\\pipe\\test_exit_pipe"),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    ASSERT_NE(hPipe, INVALID_HANDLE_VALUE);

    int op = 2; // Команда выхода
    DWORD dwWritten;
    WriteFile(hPipe, &op, sizeof(op), &dwWritten, NULL);

    CloseHandle(hPipe);

    // Ждем завершения сервера
    for (int i = 0; i < 10 && !serverExited.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    EXPECT_TRUE(serverExited.load());

    if (serverThread.joinable()) {
        serverThread.join();
    }
}