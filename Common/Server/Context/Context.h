#pragma once

#include "Define/Type.h"
#include "Header/WinSock.h"
#include "IOCP/Context/Context.h"

enum ErrorType;
class CSession;

class CAcceptContext final : public CSelfContext<CAcceptContext>
{
public:	
	CAcceptContext() = delete;
	CAcceptContext(const SOCKET _listen);

public:
	inline SOCKET GetClient() const { 
		return m_Client;
	}	

public:	
	void Release();
	void Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length);

private:		
	WSABUF m_WSABuffer; // 데이터 버퍼 정보	
	SOCKET m_Listen = INVALID_SOCKET;
	SOCKET m_Client = INVALID_SOCKET;		
};

class CSendContext final : public CSelfContext<CSendContext>
{
public:
	CSendContext();

public:
	ErrorType Regist(const SOCKET& _client, const CHAR* _buffer, INT32 _length);

private:
	void Release();
	void Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length);

private:
	WSABUF m_WSABuffer{}; // 데이터 버퍼 정보	

private:
	static const DWORD SEND_BUFFER_COUNT = 1;
};

class CRecvContext final : public CSelfContext<CRecvContext>
{
public:	
	CRecvContext();

public:
	ErrorType Regist(const SOCKET& _client);

private:	
	void Release();	
	void Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length);

private:		
	WSABUF m_WSABuffer{}; // 데이터 버퍼 정보		

private:
	constexpr static DWORD RECV_BUFFER_COUNT = 1;
};