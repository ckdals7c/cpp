#pragma once

#include <memory>

#include "Header/WinSock.h"
#include "Pattern/Singleton.h"

enum  ErrorType;

class CSession;
class CRecvContext;
class CSendContext;
class CAcceptContext;

class CContextMgr final : public Singleton<CContextMgr>
{
public:
	CContextMgr();
	virtual ~CContextMgr();

public:
	ErrorType Init(SOCKET _listen);
	ErrorType Start();

	bool MakeAccept();
	void FreeAccept(CAcceptContext* _context);

	std::shared_ptr<CSendContext> AcquireSend();
	std::shared_ptr<CRecvContext> AcquireRecv();

private:
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;
};

#define ContextMgr CContextMgr::Instance()