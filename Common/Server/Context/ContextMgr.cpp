#include "Context.h"
#include "ContextMgr.h"
#include "Define/Error.h"
#include "Define/Define.h"
#include "Logger/Logger.h"
#include "Pool/HybridPool.h"
#include "Global/Constants.h"

struct CContextMgr::IMPL
{		
	SOCKET                        Listen                    = INVALID_SOCKET;
	LPFN_ACCEPTEX                 AcceptFuncPtr             = nullptr; // 접속 허용 함수
	LPFN_GETACCEPTEXSOCKADDRS     GetAcceptSocktAddrFuncPtr = nullptr; // 주소 추출 함수

	HybridPoolPtr<CRecvContext>   RecvPool;
	HybridPoolPtr<CSendContext>   SendPool;
	HybridPoolPtr<CAcceptContext> AcceptPool;

	constexpr static size_t ACCEPT_ALLOC_COUNT = ACCEPT_COUNT;
	constexpr static size_t ACCEPT_CACHE_COUNT = ACCEPT_COUNT / COMMON_POOL_CACHE_DIV;
	constexpr static size_t ACCEPT_STORE_COUNT = ACCEPT_COUNT * COMMON_POOL_STORE_MUL;

	constexpr static size_t RECV_ALLOC_COUNT   = SESSION_COUNT;
	constexpr static size_t RECV_CACHE_COUNT   = SESSION_COUNT / COMMON_POOL_CACHE_DIV;
	constexpr static size_t RECV_STORE_COUNT   = SESSION_COUNT * COMMON_POOL_STORE_MUL;

	constexpr static size_t SEND_ALLOC_COUNT   = SESSION_COUNT * SESSION_SENDER_COUNT;
	constexpr static size_t SEND_CACHE_COUNT   = SESSION_COUNT * SESSION_SENDER_COUNT / COMMON_POOL_CACHE_DIV;
	constexpr static size_t SEND_STORE_COUNT   = SESSION_COUNT * SESSION_SENDER_COUNT * COMMON_POOL_STORE_MUL;
};

CContextMgr::CContextMgr() : m_Impl(std::make_unique<IMPL>())
{
	m_Impl->RecvPool = MakeHybridPool(CRecvContext);
	m_Impl->SendPool = MakeHybridPool(CSendContext);
	m_Impl->AcceptPool = MakeHybridPool(CAcceptContext);
}
CContextMgr::~CContextMgr() = default;

ErrorType CContextMgr::Init(SOCKET _listen)
{
	m_Impl->Listen = _listen;

	DWORD bytes = 0;
	GUID guid = WSAID_ACCEPTEX;;
	WSAIoctl(m_Impl->Listen, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &m_Impl->AcceptFuncPtr, sizeof(m_Impl->AcceptFuncPtr), &bytes, nullptr, nullptr);

	guid = WSAID_GETACCEPTEXSOCKADDRS;
	WSAIoctl(m_Impl->Listen, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &m_Impl->GetAcceptSocktAddrFuncPtr, sizeof(m_Impl->GetAcceptSocktAddrFuncPtr), &bytes, nullptr, nullptr);

	m_Impl->RecvPool->Init(
		m_Impl->RECV_CACHE_COUNT,
		m_Impl->RECV_ALLOC_COUNT,
		m_Impl->RECV_STORE_COUNT);
	
	m_Impl->SendPool->Init(
		m_Impl->SEND_CACHE_COUNT,
		m_Impl->SEND_ALLOC_COUNT,
		m_Impl->SEND_STORE_COUNT);

	m_Impl->AcceptPool->Init(
		m_Impl->ACCEPT_CACHE_COUNT,
		m_Impl->ACCEPT_ALLOC_COUNT,
		m_Impl->ACCEPT_STORE_COUNT);

	return ErrorType::Empty;
}

ErrorType CContextMgr::Start()
{
	// AcceptContext를 MaxCount 만큼 미리 준비
	for (size_t i = 0; i < m_Impl->ACCEPT_ALLOC_COUNT; ++i) {
		if (false == MakeAccept()) {
			return Logger.PushErr(ErrorType::InitFailed, "failed accepter init");
		}
	}
	return ErrorType::Empty;
}

bool CContextMgr::MakeAccept()
{
	std::shared_ptr<CAcceptContext> context = m_Impl->AcceptPool->Acquire(m_Impl->Listen);
	if (nullptr == context) {
		return false;
	}
	context->Active(context);

	// AcceptEx는 연결된 클라이언트 주소와 서버 주소를 반환할 때, 구조체 뒤에 16바이트 여유 공간 필요
	DWORD addressLen = sizeof(SOCKADDR_IN) + 16;
	DWORD byteReceived = 0;

	BOOL success = m_Impl->AcceptFuncPtr(
		m_Impl->Listen,
		context->GetClient(),
		context->GetDataBuffer(),
		0,
		addressLen,
		addressLen,
		&byteReceived,
		context->GetOverlapped());

	if (false == success)
	{
		if (WSA_IO_PENDING != WSAGetLastError()) {
			FreeAccept(context.get());
		}
	}
	return true;
}

void CContextMgr::FreeAccept(CAcceptContext* _context)
{
	if (nullptr == _context) {
		return;
	}

	closesocket(_context->GetClient());
	_context->Release();
}

std::shared_ptr<CSendContext> CContextMgr::AcquireSend()
{
	std::shared_ptr<CSendContext> context = m_Impl->SendPool->Acquire();
	context->Active(context);

	return context;
}

std::shared_ptr<CRecvContext> CContextMgr::AcquireRecv()
{
	std::shared_ptr<CRecvContext> context = m_Impl->RecvPool->Acquire();
	context->Active(context);

	return context;
}