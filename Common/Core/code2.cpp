// iocp_server_refactor.cpp
// 리팩터링된 IOCP 서버 예제 (단일 파일)
// 주요 변경사항:
//  - Acceptor, Session, SessionMgr, WorkerPool 클래스로 역할 분리
//  - SendPool 유지 (RAII-friendly)
//  - 수명/메모리 관리 지침 및 주석 한글화
// 빌드: cl /EHsc iocp_server_refactor.cpp /link Ws2_32.lib Mswsock.lib

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define NOMINMAX

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>
#include <string>
#include <iostream>
#include <algorithm>

#include <list>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

constexpr int DEFAULT_PORT = 9000;
constexpr int BACKLOG = 10;
constexpr int BUFFER_SIZE = 4096;
constexpr int INITIAL_ACCEPTS = 16;
constexpr int SEND_POOL_SIZE = 128;

enum class IO_OPERATION : int { ACCEPT, RECV, SEND };

// 전방 선언
struct PER_IO_CONTEXT;
class Session;
class SessionMgr;
class AcceptContext;

using IoContextPtr = PER_IO_CONTEXT*;
using AcceptContextPtr = AcceptContext*;

///////////////////////////////////////////////////////////////////////////
// 유틸
///////////////////////////////////////////////////////////////////////////
void printWSAError(const char* tag) {
    int err = WSAGetLastError();
    std::fprintf(stderr, "%s: WSAGetLastError=%d\n", tag, err);
}

void safeCloseSocket(SOCKET s) {
    if (s != INVALID_SOCKET) closesocket(s);
}

///////////////////////////////////////////////////////////////////////////
// PER_IO_CONTEXT: OVERLAPPED를 첫 멤버로 가지는 공용 컨텍스트
///////////////////////////////////////////////////////////////////////////
struct PER_IO_CONTEXT {
    OVERLAPPED ov;
    IO_OPERATION op;
    WSABUF wsaBuf;
    char buffer[BUFFER_SIZE];
    int bytesTransferred;
    Session* owner; // 소유 Session (nullable)

    PER_IO_CONTEXT() : op(IO_OPERATION::RECV), bytesTransferred(0), owner(nullptr) {
        ZeroMemory(&ov, sizeof(ov));
        wsaBuf.buf = buffer;
        wsaBuf.len = BUFFER_SIZE;
    }

    void resetForRecv() {
        ZeroMemory(&ov, sizeof(ov));
        op = IO_OPERATION::RECV;
        wsaBuf.buf = buffer;
        wsaBuf.len = BUFFER_SIZE;
        bytesTransferred = 0;
        owner = nullptr;
    }
    void resetForSend(const char* data, int len) {
        ZeroMemory(&ov, sizeof(ov));
        op = IO_OPERATION::SEND;
        if (len > BUFFER_SIZE) len = BUFFER_SIZE;
        memcpy(buffer, data, len);
        wsaBuf.buf = buffer;
        wsaBuf.len = len;
        bytesTransferred = 0;
        owner = nullptr;

        std::list<int> aa;
        aa.emplace_back(1);
        aa.back();

        std::weak_ptr<int> p;
        p.expired();
    }
};

///////////////////////////////////////////////////////////////////////////
// AcceptContext: AcceptEx 전용 OVERLAPPED 컨텍스트
///////////////////////////////////////////////////////////////////////////
struct AcceptContext {
    OVERLAPPED ov;
    SOCKET acceptSocket;
    char addrBuf[(sizeof(SOCKADDR_IN) + 16) * 2 + 32];
    AcceptContext() : acceptSocket(INVALID_SOCKET) { ZeroMemory(&ov, sizeof(ov)); ZeroMemory(addrBuf, sizeof(addrBuf)); }
};

///////////////////////////////////////////////////////////////////////////
// SendPool: PER_IO_CONTEXT 풀
///////////////////////////////////////////////////////////////////////////
class SendPool {
    std::mutex mtx;
    std::vector<std::unique_ptr<PER_IO_CONTEXT>> pool;
public:
    SendPool() { pool.reserve(SEND_POOL_SIZE); for (int i = 0; i < SEND_POOL_SIZE; ++i) pool.emplace_back(new PER_IO_CONTEXT()); }
    ~SendPool() = default;
    PER_IO_CONTEXT* acquire() {
        std::lock_guard<std::mutex> lg(mtx);
        if (pool.empty()) return new PER_IO_CONTEXT();
        auto p = pool.back().release();
        pool.pop_back();
        return p;
    }
    void release(PER_IO_CONTEXT* p) {
        if (!p) return;
        p->resetForRecv();
        std::lock_guard<std::mutex> lg(mtx);
        pool.emplace_back(p);
    }
};

///////////////////////////////////////////////////////////////////////////
// Session: 클라이언트 연결을 캡슐화
// - 수명 관리는 SessionMgr가 소유
// - Session은 자기 소유권(포인터)을 통해 IOCP와 동작
///////////////////////////////////////////////////////////////////////////
class Session {
    SOCKET sock{ INVALID_SOCKET };
    std::unique_ptr<PER_IO_CONTEXT> recvCtx;
    std::mutex sendMutex;
    std::atomic<bool> sending{ false };
public:
    Session(SOCKET s) : sock(s) {
        recvCtx.reset(new PER_IO_CONTEXT());
        recvCtx->owner = this;
    }
    ~Session() {
        if (sock != INVALID_SOCKET) closesocket(sock);
    }

    SOCKET getSocket() const { return sock; }

    PER_IO_CONTEXT* getRecvCtx() { return recvCtx.get(); }

    // recv 등록 (비동기)
    bool postRecv() {
        if (!recvCtx) return false;
        recvCtx->resetForRecv();
        recvCtx->owner = this;
        DWORD flags = 0;
        DWORD bytes = 0;
        int rv = WSARecv(sock, &recvCtx->wsaBuf, 1, &bytes, &flags, &recvCtx->ov, nullptr);
        if (rv == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) { printWSAError("WSARecv failed"); return false; }
        }
        return true;
    }

    // send 요청 (데이터는 복사되어 전송됨)
    bool postSend(SendPool& pool, const char* data, int len) {
        // note: 이 예제는 간단화를 위해 큐/연속성 처리를 하지 않음
        PER_IO_CONTEXT* ctx = pool.acquire();
        ctx->resetForSend(data, len);
        ctx->owner = this;

        DWORD bytes = 0;
        int rv = WSASend(sock, &ctx->wsaBuf, 1, &bytes, 0, &ctx->ov, nullptr);
        if (rv == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) { printWSAError("WSASend failed"); pool.release(ctx); return false; }
        }
        return true;
    }
};

///////////////////////////////////////////////////////////////////////////
// SessionMgr: 세션 소유 및 수명 관리
// - 간단한 싱글턴 스타일로 구현 (데모 목적)
///////////////////////////////////////////////////////////////////////////
class SessionMgr {
    std::mutex mtx;
    std::vector<std::unique_ptr<Session>> sessions;
public:
    void add(Session* s) {
        std::lock_guard<std::mutex> lg(mtx);
        sessions.emplace_back(s);
    }
    // 세션 제거: 소유권을 찾아 삭제한다
    void remove(Session* s) {
        std::lock_guard<std::mutex> lg(mtx);
        auto it = std::find_if(sessions.begin(), sessions.end(), [&](const std::unique_ptr<Session>& up) { return up.get() == s; });
        if (it != sessions.end()) sessions.erase(it);
    }
};

static SessionMgr g_sessionMgr;
static SendPool g_sendPool;
static LPFN_ACCEPTEX lpfnAcceptEx = nullptr;
static SOCKET g_listenSocket = INVALID_SOCKET;
static HANDLE g_hIOCP = nullptr;
static std::atomic<bool> g_running{ false };
static std::vector<std::thread> g_workers;
static int g_workerCount = 0;

///////////////////////////////////////////////////////////////////////////
// Accept 처리: AcceptEx 완료 시 Session 생성 및 IOCP 등록
///////////////////////////////////////////////////////////////////////////
void handleAccept(AcceptContext* actx) {
    if (!actx) return;
    SOCKET s = actx->acceptSocket;
    // SO_UPDATE_ACCEPT_CONTEXT 필요
    setsockopt(s, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&g_listenSocket, sizeof(g_listenSocket));

    // 세션 생성 및 IOCP에 등록
    auto* session = new Session(s);
    CreateIoCompletionPort((HANDLE)s, g_hIOCP, (ULONG_PTR)session, 0);
    g_sessionMgr.add(session);

    // 초기 recv 등록
    if (!session->postRecv()) {
        fprintf(stderr, "초기 recv 등록 실패\n");
        g_sessionMgr.remove(session);
        delete session; // 안전하게 지운다 (remove 후 소유권 없다)
    }

    delete actx; // AcceptContext는 여기서 소멸
}

///////////////////////////////////////////////////////////////////////////
// recv 완료 처리
///////////////////////////////////////////////////////////////////////////
void handleRecv(PER_IO_CONTEXT* pIo, Session* session, DWORD bytesTransferred) {
    if (!pIo || !session) return;
    if (bytesTransferred == 0) {
        // 정상 종료: 세션 제거
        fprintf(stdout, "클라이언트 정상 종료\n");
        g_sessionMgr.remove(session);
        delete session;
        return;
    }

    // 간단 에코: 받은 데이터를 다시 전송
post:
    {
        // pIo->buffer는 유효하므로 복사해서 전송
        session->postSend(g_sendPool, pIo->buffer, static_cast<int>(bytesTransferred));
    }

    // 계속해서 recv 등록
    session->postRecv();
}

///////////////////////////////////////////////////////////////////////////
// send 완료 처리: 풀로 반환
///////////////////////////////////////////////////////////////////////////
void handleSend(PER_IO_CONTEXT* pIo) {
    if (!pIo) return;
    g_sendPool.release(pIo);
}

///////////////////////////////////////////////////////////////////////////
// 워커 스레드
///////////////////////////////////////////////////////////////////////////
void workerProc() {
    while (g_running) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED po = nullptr;

        BOOL ok = GetQueuedCompletionStatus(g_hIOCP, &bytes, &key, &po, INFINITE);
        if (!g_running) break;
        if (!ok && !po) { printWSAError("GQCS failed"); break; }
        if (!po) continue;

        bool isAccept = (key == 0);
        if (isAccept) {
            AcceptContext* actx = reinterpret_cast<AcceptContext*>(po);
            handleAccept(actx);
            continue;
        }

        Session* session = reinterpret_cast<Session*>(key);
        PER_IO_CONTEXT* pIo = CONTAINING_RECORD(po, PER_IO_CONTEXT, ov);

        switch (pIo->op) {
        case IO_OPERATION::RECV:
            handleRecv(pIo, session, bytes);
            break;
        case IO_OPERATION::SEND:
            handleSend(pIo);
            break;
        default:
            fprintf(stderr, "알 수 없는 IO 작업\n");
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////
// 초기 AcceptEx 등록
///////////////////////////////////////////////////////////////////////////
bool postInitialAccepts(SOCKET listenSocket) {
    for (int i = 0; i < INITIAL_ACCEPTS; ++i) {
        AcceptContext* actx = new AcceptContext();
        actx->acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (actx->acceptSocket == INVALID_SOCKET) { printWSAError("accept socket 생성 실패"); delete actx; return false; }
        DWORD bytes = 0;
        BOOL ok = lpfnAcceptEx(listenSocket, actx->acceptSocket, actx->addrBuf, 0,
            sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, &actx->ov);
        if (!ok) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) { printWSAError("AcceptEx 실패"); closesocket(actx->acceptSocket); delete actx; return false; }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////
// 서버 시작/중지
///////////////////////////////////////////////////////////////////////////
bool startServer(unsigned short port, int workerThreads) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printWSAError("WSAStartup 실패"); return false; }

    g_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (g_listenSocket == INVALID_SOCKET) { printWSAError("WSASocket 실패"); WSACleanup(); return false; }

    BOOL opt = TRUE; setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (bind(g_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { printWSAError("bind 실패"); closesocket(g_listenSocket); WSACleanup(); return false; }
    if (listen(g_listenSocket, BACKLOG) == SOCKET_ERROR) { printWSAError("listen 실패"); closesocket(g_listenSocket); WSACleanup(); return false; }

    GUID guid = WSAID_ACCEPTEX; DWORD bytes = 0;
    if (WSAIoctl(g_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &bytes, nullptr, nullptr) == SOCKET_ERROR) {
        printWSAError("AcceptEx ptr 획득 실패"); closesocket(g_listenSocket); WSACleanup(); return false;
    }

    g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!g_hIOCP) { fprintf(stderr, "IOCP 생성 실패: %u\n", GetLastError()); closesocket(g_listenSocket); WSACleanup(); return false; }

    CreateIoCompletionPort((HANDLE)g_listenSocket, g_hIOCP, 0, 0);

    g_running = true; g_workerCount = workerThreads;
    for (int i = 0; i < workerThreads; ++i) g_workers.emplace_back(workerProc);

    if (!postInitialAccepts(g_listenSocket)) { fprintf(stderr, "초기 Accept 등록 실패\n"); return false; }
    printf("서버 시작: port=%u, workers=%d\n", port, workerThreads);
    return true;
}

void stopServer() {
    g_running = false;
    for (int i = 0; i < g_workerCount; ++i) PostQueuedCompletionStatus(g_hIOCP, 0, 0, nullptr);
    for (auto& t : g_workers) if (t.joinable()) t.join();
    g_workers.clear();
    if (g_hIOCP) CloseHandle(g_hIOCP); g_hIOCP = nullptr;
    if (g_listenSocket != INVALID_SOCKET) closesocket(g_listenSocket); g_listenSocket = INVALID_SOCKET;
    WSACleanup();
}

///////////////////////////////////////////////////////////////////////////
// 메모리/수명 관련 주의사항(요약)
// 1) AcceptContext: AcceptEx를 포스트한 쪽에서 소유하고, 완료 시 처리 후 반드시 delete
// 2) Session 소유권: SessionMgr가 소유(unique_ptr), IOCP의 completionKey는 raw pointer를 전달
//    - 세션을 delete 할 때 반드시 IOCP에서 해당 키로 들어오는 이벤트가 더 이상 없음을 보장하거나
//      delete 전에 세션을 IOCP 등록 해제를 고려해야 함
// 3) PER_IO_CONTEXT 풀: acquire로 얻은 포인터는 send 완료시 반드시 release해야 함
// 4) 에러/종료 경로에서 중복 close/delete 방지(예: 두 스레드가 동시에 같은 Session을 close하지 않도록 설계 필요)
// 5) 더 안전한 방법: shared_ptr + enable_shared_from_this를 사용해 IO 완료시 소유권을 임시로 확보
///////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
    unsigned short port = DEFAULT_PORT;
    int workers = std::max(1u, std::thread::hardware_concurrency());
    if (argc >= 2) port = (unsigned short)atoi(argv[1]);
    if (argc >= 3) workers = atoi(argv[2]);

    if (!startServer(port, workers)) { fprintf(stderr, "서버 시작 실패\n"); return EXIT_FAILURE; }

    printf("엔터 키를 누르면 서버 종료\n"); getchar();
    stopServer();
    printf("서버 종료 완료\n");
    return 0;
}
