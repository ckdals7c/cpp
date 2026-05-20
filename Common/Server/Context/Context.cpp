#include "Context.h"
#include "Define/Type.h"
#include "Define/Error.h"
#include "Logger/Logger.h"
#include "Session/Session.h"
#include "Session/SessionMgr.h"
#include "Context/ContextMgr.h"

////////////////////////////////////////////////////////////////////////
// Accept Context
////////////////////////////////////////////////////////////////////////

CAcceptContext::CAcceptContext(const SOCKET _listen) :
	m_WSABuffer(), m_Listen(_listen), m_Client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
{	
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));
	CIOCPContext::Reset();

	m_Process = [this](HANDLE _iocp, ULONG_PTR _key, DWORD _length) {
		Process(_iocp, _key, _length);
	};
}

void CAcceptContext::Release()
{	
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));
	m_Listen = INVALID_SOCKET;
	m_Client = INVALID_SOCKET;	
	CSelfContext::Release();
}

void CAcceptContext::Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length)
{	
	bool success = SessionMgr.Insert(_iocp, m_Listen, m_Client);
	if (false == success) {
		Logger.PushErr(ErrorType::CreateFaield, "failed insert session");
	}

	ContextMgr.MakeAccept();
	Release();
}

////////////////////////////////////////////////////////////////////////
// Send Context
////////////////////////////////////////////////////////////////////////

CSendContext::CSendContext()
{
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));
	CIOCPContext::Reset();

	m_Process = [this](HANDLE _iocp, ULONG_PTR _key, DWORD _length) {
		Process(_iocp, _key, _length);
	};
}

ErrorType CSendContext::Regist(const SOCKET& _client, const CHAR* _buffer, INT32 _length)
{
	DWORD flags = 0;
	DWORD bytes = 0;

	auto error = [this](ErrorType _err, std::string_view _msg)
		{
			Release();
			return Logger.PushErr(_err, _msg);
		};

	if (_length > IOCP_DATA_BUFFER_SIZE) {
		return error(ErrorType::InvalidParameter, "buffer size exceeds the maximum allowed length");
	}
	
	memcpy(m_DataBuffer, _buffer, _length);
	m_WSABuffer.buf = m_DataBuffer;
	m_WSABuffer.len = _length;

	const int result = WSASend(_client, &m_WSABuffer, SEND_BUFFER_COUNT, &bytes, flags, &m_Overlapped, nullptr);
	if (SOCKET_ERROR == result) {
		const int err = WSAGetLastError();
		if (WSA_IO_PENDING != err) {
			return error(ErrorType::InitFailed, "window socket api send failed");
		}
	}	
	return ErrorType::Empty;
}

void CSendContext::Release()
{	
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));
	CSelfContext::Release();
}

void CSendContext::Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length)
{	
	Release();
}

////////////////////////////////////////////////////////////////////////
// Recv Context
////////////////////////////////////////////////////////////////////////

CRecvContext::CRecvContext()
{
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));	
	CIOCPContext::Reset();

	m_Process = [this](HANDLE _iocp, ULONG_PTR _key, DWORD _length) {
		this->Process(_iocp, _key, _length);
	};
}

ErrorType CRecvContext::Regist(const SOCKET& _client)
{
	DWORD flags = 0;
	DWORD bytes = 0;

	auto error = [this](ErrorType _err, std::string_view _msg)
		{
			Release();
			return Logger.PushErr(_err, _msg);
		};
	
	m_WSABuffer.buf = m_DataBuffer;
	m_WSABuffer.len = IOCP_DATA_BUFFER_SIZE;

	const int result = WSARecv(_client, &m_WSABuffer, RECV_BUFFER_COUNT, &bytes, &flags, &m_Overlapped, nullptr);
	if (SOCKET_ERROR == result) {
		const int err = WSAGetLastError();
		if (WSA_IO_PENDING != err) {
			return error(ErrorType::InitFailed, "window socket api receive failed");
		}
	}
	return ErrorType::Empty;
}

void CRecvContext::Release()
{	
	ZeroMemory(&m_WSABuffer, sizeof(WSABUF));
	CSelfContext::Release();
}

void CRecvContext::Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length)
{
	CSession* session = SessionMgr.Find(static_cast<IndexPoolKey>(_key));
	if (nullptr != session) {
		if (0 == m_BytesTransferred) {
			SessionMgr.Remove(session);
		}
		else {
			session->Receive(m_DataBuffer, _length);
		}
	}
	Release();
}
