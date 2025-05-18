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