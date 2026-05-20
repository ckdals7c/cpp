#pragma once

#include <memory>

#include "Define/Type.h"
#include "Header/WinSock.h"

enum ErrorType;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
	CSession();	
	~CSession();

public:
	ErrorType Init(const HANDLE _iocp, const SOCKET _listen, const SOCKET _client, const IndexPoolKey& _key);
	ErrorType Send(const CHAR* _buffer, const DWORD _length);
	ErrorType Receive(const CHAR* _buffer, const DWORD _length);
	ErrorType Process(const CHAR* _buffer, const DWORD _length);

	void Close();

public:
	IndexPoolKey GetKey() const;
	SOCKET       GetClient() const;	
	

private:
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;
};