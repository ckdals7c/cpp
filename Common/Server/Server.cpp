#include "Server.h"
#include "IOCP/IOCP.h"
#include "Define/Error.h"
#include "Logger/Logger.h"
#include "Context/ContextMgr.h"

#pragma comment(lib, "Ws2_32.lib")

struct CServer::IMPL
{	
	INT32  Port   = 0;              // 포트
	SOCKET Listen = INVALID_SOCKET; // Listen 소켓

	constexpr static INT32 BACK_LOG_COUNT = 2048; // 대기 큐 사이즈
};

CServer::CServer() : m_Impl(std::make_unique<CServer::IMPL>())
{
}
CServer::~CServer() = default;

ErrorType CServer::Init(const INT32 _port)
{
	WSADATA data;
	if (0 != WSAStartup(MAKEWORD(2, 2), &data)) {
		return Close(ErrorType::InitFailed, "window socket startup failed");
	}

	m_Impl->Listen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_Impl->Listen) {
		return Close(ErrorType::InitFailed, "create socket failed");
	}

	sockaddr_in srv{};
	srv.sin_family      = AF_INET;	
	srv.sin_port        = htons(_port);
	srv.sin_addr.s_addr = htonl(INADDR_ANY);

	if (SOCKET_ERROR == bind(m_Impl->Listen, (sockaddr*)&srv, sizeof(srv))) {
		return Close(ErrorType::InitFailed, "bind failed ");
	}	

	if (SOCKET_ERROR == listen(m_Impl->Listen, m_Impl->BACK_LOG_COUNT)) {
		return Close(ErrorType::InitFailed, "listen failed");
	}		

	ErrorType err = IOCP.Init();
	if (ErrorType::Empty != err) {
		return Close(err, "iocp init failed");
	}

	err = ContextMgr.Init(m_Impl->Listen);
	if (ErrorType::Empty != err) {
		return Close(err, "iocp init failed");
	}

	m_Impl->Port = _port;
	return ErrorType::Empty;
}

ErrorType CServer::Start()
{
	ErrorType err = IOCP.Regist((HANDLE)m_Impl->Listen);
	if (ErrorType::Empty != err) {
		return Close(err, "iocp regist failed");		
	}

	err = IOCP.Start();
	if (ErrorType::Empty != err) {
		return Close(err, "iocp start failed");		
	}

	err = ContextMgr.Start();
	if (ErrorType::Empty != err) {
		return Close(err, "session manager start failed");
	}	
	return ErrorType::Empty;
}

ErrorType CServer::Run() 
{
	printf("Press X to stop...\n");
	while (true) 
	{
		CHAR ch = getchar();
		if ('x' == ch || 'X' == ch) {
			break;
		}
	}	
	return ErrorType::Empty;
}

ErrorType CServer::Close(const ErrorType _err, const std::string_view _msg)
{
	closesocket(m_Impl->Listen);

	ErrorType err = IOCP.Close();
	if (ErrorType::Empty != err) {
		Logger.PushErr(err, "iocp close faield");
	}
	
	WSACleanup();
	if (ErrorType::Empty != _err) {
		return Logger.PushErr(_err, _msg);
	}
	return ErrorType::Empty;
}

ErrorType CServer::Close()
{	
	return Close(ErrorType::Empty, "");
}