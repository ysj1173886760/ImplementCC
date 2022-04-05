#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <thread>

#include <unistd.h>
#include <cstdio>
#include <sys/socket.h>
#include <cstdlib>
#include <netinet/in.h>
#include <cstring>
#include <queue>
#include <condition_variable>

const int workers = 2;

class Queue {
public:
    std::queue<int> _q;
    std::condition_variable _consumer;
    std::mutex _mu;

    Queue() {}

    void push(int fd) {
        std::unique_lock<std::mutex> lock_guard(_mu);
        _q.push(fd);
        _consumer.notify_one();
    }

    int pop() {
        std::unique_lock<std::mutex> lock_guard(_mu);
        while (_q.empty()) {
            _consumer.wait(lock_guard, [&]() { return !_q.empty(); });
        }
        int fd = _q.front();
        _q.pop();
        return fd;
    }
} AQueue;

void work(int fd) {
	char recv_buffer[1024] = {0};
    char send_buffer[1024] = {0};

    int n = 1;
    while (n > 0) {
        n = read(fd ,recv_buffer, sizeof(recv_buffer));
        send(fd, recv_buffer, n, 0);
    }
}

void worker_thread() {
    while (1) {
        int fd = AQueue.pop();
        work(fd);
        close(fd);
    }
}

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    int port = 8080;

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        std::cout << "socket failed";
        exit(EXIT_FAILURE);
    }
       
    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                                                  &opt, sizeof(opt)))
    {
        std::cout << "setsockopt";
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
       
    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cout << "bind failed";
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        std::cout << "listen";
        exit(EXIT_FAILURE);
    }

    // spawn worker thread
    std::vector<std::thread> list;
    for (int i = 0; i < workers; i++) {
        list.emplace_back(std::thread(worker_thread));
    }

    std::cout << "server listening" << std::endl;
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            continue;
        }
        AQueue.push(new_socket);
    }

    return 0;
}