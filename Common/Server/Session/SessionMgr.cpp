#include "Session.h"
#include "SessionMgr.h"
#include "Pool/IndexPool.h"
#include "Global/Constants.h"

struct CSessionMgr::IMPL
{
public:
	IMPL() : Pool(SESSION_COUNT) {}

public:
	CIndexPool<CSession> Pool;	
};

CSessionMgr::CSessionMgr() : m_Impl(std::make_unique<IMPL>())
{	
}
CSessionMgr::~CSessionMgr() = default;

bool CSessionMgr::Insert(const HANDLE _iocp, const SOCKET _listen, const SOCKET _client)
{	
	std::pair<IndexPoolKey, CSession*> pair = m_Impl->Pool.Acquire();
	if (nullptr == pair.second) {
		return false;
	}

	ErrorType err = pair.second->Init(_iocp, _listen, _client, pair.first);
	if (ErrorType::Empty != err) {
		return Logger.PushErr(err, "session init failed");
	}
	return true;
}

bool CSessionMgr::Remove(CSession* _session)
{
	if (nullptr == _session) {
		return false;
	}
	return m_Impl->Pool.Release(_session->GetKey());
}

CSession* CSessionMgr::Find(const IndexPoolKey& _key) 
{
	return m_Impl->Pool.Find(_key);
}