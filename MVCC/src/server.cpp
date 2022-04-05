#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <thread>
#include <map>
#include <atomic>

#include <unistd.h>
#include <cstdio>
#include <sys/socket.h>
#include <cstdlib>
#include <netinet/in.h>
#include <cstring>
#include <queue>
#include <condition_variable>

using namespace std;

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

class TSOManager {
private:
    atomic<int> tso;
public:
    TSOManager() {
        tso.store(0);
    }

    int get_tso() {
        return tso.load();
    }

    int inc_tso() {
        return tso.fetch_add(1);
    }
} TSO;

struct Key {
    int a;
    int seq;
    Key(int _a, int _seq): a(_a), seq(_seq) {}

    bool operator<(const Key &rhs) const {
        if (this->a == rhs.a) {
            return this->seq > rhs.seq;
        }
        return this->a < rhs.a;
    }
};

struct Value {
    int b, c;
    int xmin, xmax;

    Value(): b(0), c(0), xmin(0), xmax(0) {}
    Value(int _b, int _c, int _xmin, int _xmax): b(_b), c(_c), xmin(_xmin), xmax(_xmax) {}
};

map<Key, Value> db;

string select(char *recv_buffer, int ptr, int size) {
    bool scan_all = false;
    int primary_key = 0;
    string tmp;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        tmp = tmp + recv_buffer[ptr];
        ptr++;
    }
    if (tmp == "where") {
        scan_all = false;
    } else {
        scan_all = true;
    }

    string result;
    if (!scan_all) {
        ptr += 3;
        while (ptr < size && recv_buffer[ptr] != ' ') {
            primary_key = primary_key * 10 + recv_buffer[ptr] - '0';
            ptr++;
        }
        auto it = db.lower_bound(Key{primary_key, TSO.get_tso()});
        if (it == db.end()) {
            return "";
        }
        if (it->first.a != primary_key) {
            return "";
        }

        return to_string(it->first.a) + ", " + to_string(it->second.b) + ", " + to_string(it->second.c) + "\n";
    } else {
        int lst = -1;
        for (const auto &[key, value] : db) {
            if (key.a == lst) {
                continue;
            }
            result += to_string(key.a) + ", " + to_string(value.b) + ", " + to_string(value.c) + "\n";
            lst = key.a;
        }
        return result;
    }
}

string insert(char *recv_buffer, int ptr, int size) {
    int a = 0, b = 0, c = 0;
    while (ptr < size && recv_buffer[ptr] != ',') {
        a = a * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    ptr++;
    while (ptr < size && recv_buffer[ptr] != ',') {
        b = b * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    ptr++;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        c = c * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    int tso = TSO.inc_tso();
    db[Key(a, tso)] = Value(b, c, 0, 0);
    return "success\n";
}

string remove(char *recv_buffer, int ptr, int size) {
    string tmp;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        tmp = tmp + recv_buffer[ptr];
        ptr++;
    }
    if (tmp != "where") {
        return "failed";
    }

    int primary_key = 0;
    ptr += 3;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        primary_key = primary_key * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }

    // TODO: we should exploit mvcc to implement delete
    return "";
}

string update(char *recv_buffer, int ptr, int size) {
    int a = 0, b = 0, c = 0;
    while (ptr < size && recv_buffer[ptr] != ',') {
        a = a * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    ptr++;
    while (ptr < size && recv_buffer[ptr] != ',') {
        b = b * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    ptr++;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        c = c * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }

    string tmp;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        tmp = tmp + recv_buffer[ptr];
        ptr++;
    }
    if (tmp != "where") {
        return "failed";
    }

    int primary_key = 0;
    ptr += 3;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        primary_key = primary_key * 10 + recv_buffer[ptr] - '0';
        ptr++;
    }
    return "";
}

// table A(int, primary), B(int), C(int)
// select or select where A=a(int)
// insert a(int),b(int),c(int)
// update a(int),b(int),c(int) where A=a(int)
// delete where A=a(int)
std::string process(char *recv_buffer, int size) {
    std::string op;
    int ptr = 0;
    while (ptr < size && recv_buffer[ptr] != ' ') {
        op += recv_buffer[ptr];
        ptr++;
    }
    ptr++;
    if (op == "select") {
        return select(recv_buffer, ptr, size);
    } else if (op == "insert") {
        return insert(recv_buffer, ptr, size);
    } else if (op == "update") {
        return update(recv_buffer, ptr, size);
    } else if (op == "delete") {
        return remove(recv_buffer, ptr, size);
    }
    return "";
}

void work(int fd) {
	char recv_buffer[1024] = {0};

    int n = 1;
    while (n > 0) {
        n = read(fd ,recv_buffer, sizeof(recv_buffer));
        string send_buffer = process(recv_buffer, n);
        send(fd, send_buffer.c_str(), send_buffer.size(), 0);
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