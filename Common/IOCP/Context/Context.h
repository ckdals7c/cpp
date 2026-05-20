#pragma once

#include <memory>
#include <windows.h>
#include <functional>

#include "Define/Type.h"
#include "Global/Constants.h"

// OVERLAPPED 는 첫번째 멤버 변수로 설정( 자신 또는 하위 클래스 가상 함수 사용 금지 )
class CIOCPContext
{
	using IOCPProcess = std::function<void(HANDLE, ULONG_PTR, DWORD)>;

public:
	inline       OVERLAPPED* GetOverlapped()          { return &m_Overlapped;       }
	inline       CHAR*       GetDataBuffer()          { return m_DataBuffer;        }	
	inline const DWORD       GetBytesTransferred()    { return m_BytesTransferred;  }

public:
	void Reset()
	{
		ZeroMemory(&m_Overlapped, sizeof(m_Overlapped));				
		m_BytesTransferred = 0;
	}

	void Process(const HANDLE _iocp, const ULONG_PTR _key, const DWORD _length)
	{
		m_BytesTransferred = _length;
		if (nullptr != m_Process) {
			m_Process(_iocp, _key, _length);
		}
	}

protected:
	OVERLAPPED  m_Overlapped{};                        // IO 처리 결과	
	DWORD       m_BytesTransferred{};                  // 수신 바이트 길이	
	CHAR        m_DataBuffer[IOCP_DATA_BUFFER_SIZE]{}; // 데이터 버퍼
	IOCPProcess m_Process = nullptr;
};

template <typename T>
class CSelfContext : public CIOCPContext
{
public:
	void Active(std::shared_ptr<T> _self) {
		m_Self = _self;
	}

	void Release()
	{
		m_Self.reset();
		CIOCPContext::Reset();
	}

protected:
	std::shared_ptr<T> m_Self;
};