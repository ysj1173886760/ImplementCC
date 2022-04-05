#include <cstdio>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <cstdio>
#define PORT 8080

int main() {
	int sock = 0;
	struct sockaddr_in serv_addr;
	char recv_buffer[1024] = {0};
    char send_buffer[1024] = {0};

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("\n Socket creation error \n");
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	
	// Convert IPv4 and IPv6 addresses from text to binary form
	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0)
	{
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}

	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		printf("\nConnection Failed \n");
		return -1;
	}

    for (std::string line; std::getline(std::cin, line); ) {
        memset(recv_buffer, 0, sizeof(recv_buffer));
        send(sock, line.c_str(), line.size(), 0);
        read(sock ,recv_buffer, sizeof(recv_buffer));
        printf("%.*s", sizeof(recv_buffer), recv_buffer);
    }
    
    close(sock);
	return 0;
}
