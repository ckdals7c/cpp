#include <vector>

#include "Session.h"
#include "Token/Token.h"
#include "Define/Type.h"
#include "Define/Error.h"
#include "Logger/Logger.h"
#include "Context/Context.h"
#include "Global/Constants.h"
#include "Context/ContextMgr.h"

struct CSession::IMPL {
	IndexPoolKey Key    = 0;
	SessionToken Token  = "";
	SOCKET       Client = INVALID_SOCKET;
};

CSession::CSession() : m_Impl(std::make_unique<CSession::IMPL>())
{	
}

CSession::~CSession()
{
	Close();
}

ErrorType CSession::Init(const HANDLE _iocp, const SOCKET _listen, const SOCKET _client, const IndexPoolKey& _key)
{	
	m_Impl->Key    = _key;
	m_Impl->Token  = CToken::Create();
	m_Impl->Client = _client;

	if (INVALID_SOCKET == m_Impl->Client) {
		return ErrorType::InvalidParameter;
	}

	auto error = [this](ErrorType _err, std::string_view _msg)
		{
			Close();
			return Logger.PushErr(_err, _msg);
		};

	std::shared_ptr<CRecvContext> recv = ContextMgr.AcquireRecv();
	if (nullptr == recv) {
		return error(ErrorType::Nullptr, "session receive context is nullptr");
	}
	recv->Regist(m_Impl->Client);

	setsockopt(m_Impl->Client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&_listen, sizeof(_listen));
	CreateIoCompletionPort((HANDLE)m_Impl->Client, _iocp, m_Impl->Key, 0);

	return ErrorType::Empty;
}

ErrorType CSession::Send(const CHAR* _buffer, const DWORD _length)
{
	std::shared_ptr<CSendContext> send = ContextMgr.AcquireSend();
	if (nullptr == send) {
		return Logger.PushErr(ErrorType::Nullptr, "session receive context is nullptr");
	}

	send->Regist(m_Impl->Client, _buffer, _length);
	return ErrorType::Empty;
}

ErrorType CSession::Receive(const CHAR* _buffer, const DWORD _length)
{
	ErrorType err = Process(_buffer, _length);
	if (ErrorType::Empty != err) {
		Logger.PushErr(err, "session receive context is nullptr");
	}

	std::shared_ptr<CRecvContext> recv = ContextMgr.AcquireRecv();
	if (nullptr == recv) {
		return Logger.PushErr(ErrorType::Nullptr, "session receive context is nullptr");
	}

	recv->Regist(m_Impl->Client);
	return ErrorType::Empty;
}

ErrorType CSession::Process(const CHAR* _buffer, const DWORD _length)
{
	// 에코 처리
	Send(_buffer, _length);
	return ErrorType::Empty;
}

void CSession::Close()
{
	if (INVALID_SOCKET == m_Impl->Client) {
		return;
	}

	CancelIoEx((HANDLE)m_Impl->Client, nullptr);
	closesocket(m_Impl->Client);

	m_Impl->Client = INVALID_SOCKET;
}

IndexPoolKey CSession::GetKey() const {
	return m_Impl->Key;
}

SOCKET CSession::GetClient() const {
	return m_Impl->Client;
}


