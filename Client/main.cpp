// iocp_client.cpp
// 간단한 동기식 TCP 에코 클라이언트
// 빌드: cl /EHsc iocp_client.cpp /link Ws2_32.lib

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define NOMINMAX

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

constexpr int BUFFER_SIZE = 4096;

int main(int argc, char** argv)
{
    const char* serverIp = "127.0.0.1";
    unsigned short serverPort = 9000;

    // if (argc >= 2) serverIp = argv[1];
    // if (argc >= 3) serverPort = (unsigned short)atoi(argv[2]);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup 실패\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket 실패: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIp, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("connect 실패: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("서버 연결 성공 (%s:%u)\n", serverIp, serverPort);
    printf("메시지를 입력하세요 (exit 입력 시 종료)\n");

    char sendBuf[BUFFER_SIZE];
    char recvBuf[BUFFER_SIZE];

    while (true) {
        std::cout << "> ";
        std::string line;
        std::getline(std::cin, line);

        if (line == "exit")
            break;

        int sendLen = (int)line.size();
        if (sendLen == 0)
            continue;

        int sent = send(sock, line.c_str(), sendLen, 0);
        if (sent == SOCKET_ERROR) {
            printf("send 실패: %d\n", WSAGetLastError());
            break;
        }

        int recvd = recv(sock, recvBuf, BUFFER_SIZE - 1, 0);
        if (recvd == 0) {
            printf("서버가 연결을 종료했습니다\n");
            break;
        }
        if (recvd == SOCKET_ERROR) {
            printf("recv 실패: %d\n", WSAGetLastError());
            break;
        }

        recvBuf[recvd] = '\0';
        printf("echo: %s\n", recvBuf);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
