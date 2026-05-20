#include "Thread/ThreadMgr.h"


unsigned __stdcall ThreadMgr::Work(void*)
{
}

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


HANDLE g_hIOCP = NULL;
SOCKET g_listenSocket = INVALID_SOCKET;
LPFN_ACCEPTEX lpfnAcceptEx = NULL;
LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
volatile bool g_running = true;


unsigned __stdcall WorkerThread(void* pv) {
	DWORD bytesTransferred;
	ULONG_PTR completionKey;
	LPOVERLAPPED pOverlapped;


	while (g_running) {
		BOOL ok = GetQueuedCompletionStatus(
			g_hIOCP,
			&bytesTransferred,
			&completionKey,
			&pOverlapped,
			INFINITE);


		if (!g_running) break;


		if (!ok) {
			if (pOverlapped == NULL) {
				// fatal error
				fprintf(stderr, "GetQueuedCompletionStatus failed: %u\n", GetLastError());
				break;
			}
			// Else, an error occured for this IO
		}


		PER_IO_CONTEXT* pIo = (PER_IO_CONTEXT*)pOverlapped;
		PER_CLIENT* pClient = (PER_CLIENT*)completionKey;


		switch (pIo->op) {
		case IO_ACCEPT:
		{
			// pIo->acceptSocket is the accepted socket
			SOCKET clientSocket = pIo->acceptSocket;


			// Create client context
			PER_CLIENT* newClient = (PER_CLIENT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_CLIENT));
			newClient->socket = clientSocket;


			// Associate with IOCP
			CreateIoCompletionPort((HANDLE)clientSocket, g_hIOCP, (ULONG_PTR)newClient, 0);


			// Set accepted socket options (non-blocking, etc)
			BOOL b = TRUE;
			setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&g_listenSocket, sizeof(g_listenSocket));


			// Post initial WSARecv
			ZeroMemory(&pIo->overlapped, sizeof(OVERLAPPED));
			pIo->wsaBuf.buf = pIo->buffer;
			pIo->wsaBuf.len = BUFFER_SIZE;
			pIo->op = IO_READ;
			DWORD flags = 0;
			DWORD recvBytes = 0;
			int rc = WSARecv(clientSocket, &pIo->wsaBuf, 1, &recvBytes, &flags, &pIo->overlapped, NULL);
			if (rc == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err != WSA_IO_PENDING) {
					fprintf(stderr, "WSARecv failed on accept: %d\n", err);
					closesocket(clientSocket);
					HeapFree(GetProcessHeap(), 0, newClient);
				}
			}


			// Post another AcceptEx for next connection
			// allocate a fresh accept context
			PER_IO_CONTEXT* pNewAccept = (PER_IO_CONTEXT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_CONTEXT));
			pNewAccept->op = IO_ACCEPT;
			pNewAccept->acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			ZeroMemory(&pNewAccept->overlapped, sizeof(OVERLAPPED));


			// Prepare AcceptEx buffer (address info)
			DWORD bytesReceived = 0;
			BOOL aok = lpfnAcceptEx(g_listenSocket, pNewAccept->acceptSocket, pNewAccept->buffer, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytesReceived, &pNewAccept->overlapped);
			if (!aok) {
				int err = WSAGetLastError();
				if (err != WSA_IO_PENDING) {
					fprintf(stderr, "AcceptEx failed to post: %d\n", err);
					closesocket(pNewAccept->acceptSocket);
					HeapFree(GetProcessHeap(), 0, pNewAccept);
				}
			}


			// Free original accept context (we reused pIo for read)
			// Note: we used same pIo for read on accepted socket, so don't free pIo here.
		}
		break;
		case IO_READ:
		{
			if (bytesTransferred == 0) {
				// Connection closed
				fprintf(stdout, "Client disconnected\n");
				closesocket(pClient->socket);
				HeapFree(GetProcessHeap(), 0, pClient);
				HeapFree(GetProcessHeap(), 0, pIo);
				break;
			}


			// Echo back the data
			pIo->bytesTransferred = bytesTransferred;
			ZeroMemory(&pIo->overlapped, sizeof(OVERLAPPED));
			pIo->wsaBuf.buf = pIo->buffer;
			pIo->wsaBuf.len = pIo->bytesTransferred;
			pIo->op = IO_WRITE;
			DWORD sent = 0;
			int rc = WSASend(pClient->socket, &pIo->wsaBuf, 1, &sent, 0, &pIo->overlapped, NULL);
			if (rc == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err != WSA_IO_PENDING) {
					fprintf(stderr, "WSASend failed: %d\n", err);
					closesocket(pClient->socket);
					HeapFree(GetProcessHeap(), 0, pClient);
					HeapFree(GetProcessHeap(), 0, pIo);
				}
			}
		}
		break;
		case IO_WRITE:
		{
			// After write completes, post another WSARecv
			ZeroMemory(&pIo->overlapped, sizeof(OVERLAPPED));
			pIo->wsaBuf.buf = pIo->buffer;
			pIo->wsaBuf.len = BUFFER_SIZE;
			pIo->op = IO_READ;
			DWORD flags = 0;
			DWORD recvBytes = 0;
			int rc = WSARecv(pClient->socket, &pIo->wsaBuf, 1, &recvBytes, &flags, &pIo->overlapped, NULL);
			if (rc == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err != WSA_IO_PENDING) {
					fprintf(stderr, "WSARecv failed after write: %d\n", err);
					closesocket(pClient->socket);
					HeapFree(GetProcessHeap(), 0, pClient);
					HeapFree(GetProcessHeap(), 0, pIo);
				}
			}
		}
		break;
		}
	}
	return 0;
}

int main() {
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		fprintf(stderr, "WSAStartup failed\n");
		return -1;
	}


	// Create IOCP
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (!g_hIOCP) {
		fprintf(stderr, "CreateIoCompletionPort failed: %u\n", GetLastError());
		return -1;
	}


	// Create listen socket
	g_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (g_listenSocket == INVALID_SOCKET) {
		fprintf(stderr, "WSASocket failed: %d\n", WSAGetLastError());
		return -1;
	}


	// Bind
	sockaddr_in srv;
	ZeroMemory(&srv, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = htonl(INADDR_ANY);
	srv.sin_port = htons(DEFAULT_PORT);


	if (bind(g_listenSocket, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
		fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
		return -1;
	}


	if (listen(g_listenSocket, BACKLOG) == SOCKET_ERROR) {
		fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
		return -1;
	}


	// Load AcceptEx function pointer
	GUID acceptex_guid = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	WSAIoctl(g_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptex_guid, sizeof(acceptex_guid), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &bytes, NULL, NULL);
	GUID getaddrs_guid = WSAID_GETACCEPTEXSOCKADDRS;
	WSAIoctl(g_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &getaddrs_guid, sizeof(getaddrs_guid), &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs), &bytes, NULL, NULL);


	// Associate listening socket with IOCP so completionKey can be used for server-level notifications
	CreateIoCompletionPort((HANDLE)g_listenSocket, g_hIOCP, (ULONG_PTR)NULL, 0);

	// Start worker threads
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int numThreads = si.dwNumberOfProcessors * 2; // heuristic
	HANDLE* threads = (HANDLE*)HeapAlloc(GetProcessHeap(), 0, sizeof(HANDLE) * numThreads);
	for (int i = 0; i < numThreads; ++i) {
		threads[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, NULL, 0, NULL);
	}


	// Post initial AcceptExs
	for (int i = 0; i < ACCEPT_EX_BUFFERS; ++i) {
		PER_IO_CONTEXT* pAccept = (PER_IO_CONTEXT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_CONTEXT));
		pAccept->op = IO_ACCEPT;
		pAccept->acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		ZeroMemory(&pAccept->overlapped, sizeof(OVERLAPPED));


		DWORD bytesReceived = 0;
		BOOL aok = lpfnAcceptEx(g_listenSocket, pAccept->acceptSocket, pAccept->buffer, 0,
			sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytesReceived, &pAccept->overlapped);
		if (!aok) {
			int err = WSAGetLastError();
			if (err != WSA_IO_PENDING) {
				fprintf(stderr, "Initial AcceptEx failed: %d\n", err);
				closesocket(pAccept->acceptSocket);
				HeapFree(GetProcessHeap(), 0, pAccept);
			}
		}
	}


	printf("IOCP echo server listening on port %d\n", DEFAULT_PORT);
	printf("Press Enter to stop...\n");
	getchar();


	// Shutdown
	g_running = false;
	// Post dummy completions to wake workers
	for (int i = 0; i < numThreads; ++i) {
		PostQueuedCompletionStatus(g_hIOCP, 0, 0, NULL);
	}


	WaitForMultipleObjects(numThreads, threads, TRUE, INFINITE);
	for (int i = 0; i < numThreads; ++i) CloseHandle(threads[i]);
	HeapFree(GetProcessHeap(), 0, threads);


	closesocket(g_listenSocket);
	CloseHandle(g_hIOCP);
	WSACleanup();


	return 0;
}