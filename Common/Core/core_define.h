#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <process.h>

#pragma comment(lib, "Ws2_32.lib")


#define DEFAULT_PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 4096
#define ACCEPT_EX_BUFFERS 16


enum IO_OPERATION {
	IO_ACCEPT,
	IO_READ,
	IO_WRITE
};


struct PER_IO_CONTEXT {
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[BUFFER_SIZE];
	int bytesTransferred;
	IO_OPERATION op;
	SOCKET acceptSocket; // used for accept contexts
};


struct PER_CLIENT {
	SOCKET socket;
	// You can add user data here
};