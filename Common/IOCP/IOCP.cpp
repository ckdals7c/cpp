#include <thread>
#include <vector>

#include "IOCP.h"
#include "Define/Error.h"
#include "Logger/Logger.h"
#include "Context/Context.h"

struct CIOCP::IMPL
{
	HANDLE                    Handle    = nullptr;	
	bool                      IsRunning = true;	
	DWORD                     ThreadCnt = 0;
	std::vector<std::jthread> Threads;

	constexpr static DWORD MIN_THREAD_CNT = 4;
};

CIOCP::CIOCP() : m_Impl(std::make_unique<CIOCP::IMPL>())
{
	m_Impl->ThreadCnt = std::thread::hardware_concurrency() * 2;
	if (0 >= m_Impl->ThreadCnt) {
		m_Impl->ThreadCnt = m_Impl->MIN_THREAD_CNT;
	}
}
CIOCP::~CIOCP() = default;

void  CIOCP::Work()
{
	while (true == m_Impl->IsRunning)
	{	
		LPOVERLAPPED overlapped = nullptr;
		ULONG_PTR    key         = 0;
		DWORD        len         = 0;

		BOOL success = GetQueuedCompletionStatus(m_Impl->Handle, &len, &key, &overlapped, INFINITE);
		if (false == m_Impl->IsRunning) {
			break;
		}

		if (false == success) 
		{
			if (nullptr == overlapped) {
				continue;
			}
		}
		
		CIOCPContext* ptr = std::bit_cast<CIOCPContext*>(overlapped);
		if (nullptr == ptr) {
			continue;
		}
		ptr->Process(m_Impl->Handle, key, len);
	}
}

ErrorType CIOCP::Init()
{
	m_Impl->Handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, m_Impl->ThreadCnt);
	if (nullptr == m_Impl->Handle) {
		return Logger.PushErr(ErrorType::InitFailed, "completion port create failed");
	}
	return ErrorType::Empty;
}

ErrorType CIOCP::Start() 
{	
	m_Impl->Threads.reserve(m_Impl->ThreadCnt);
	for (DWORD i = 0; i < m_Impl->ThreadCnt; ++i) 
	{
		m_Impl->Threads.emplace_back([this]
			{
				Work();
			}
		);
	}
	return ErrorType::Empty;
}

ErrorType CIOCP::Close() 
{	
	m_Impl->IsRunning = false;
	for (size_t i = 0; i < m_Impl->Threads.size(); ++i) {
		PostQueuedCompletionStatus(m_Impl->Handle, 0, 0, nullptr);
	}
	return ErrorType::Empty;
}

ErrorType CIOCP::Regist(const HANDLE _listen)
{
	const HANDLE ret = CreateIoCompletionPort(_listen, m_Impl->Handle, (ULONG_PTR)nullptr, 0);
	if (nullptr == ret) {
		return Logger.PushErr(ErrorType::Common, "completion port create failed");
	}
	return ErrorType::Empty;
}