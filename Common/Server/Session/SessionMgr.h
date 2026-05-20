#pragma once

#include <memory>

#include "Define/Type.h"
#include "Header/WinSock.h"
#include "Pattern/Singleton.h"

class CSession;

class CSessionMgr : public Singleton<CSessionMgr>
{
public:
	CSessionMgr();
	virtual ~CSessionMgr();

public:	
	bool Insert(const HANDLE _iocp, const SOCKET _listen, const SOCKET _client);
	bool Remove(CSession* _session);

	CSession* Find(const IndexPoolKey& _key);

private:
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;
};

#define SessionMgr CSessionMgr::Instance()