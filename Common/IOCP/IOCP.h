#pragma once

#include <memory>
#include <concrt.h>

#include "Pattern/Singleton.h"

enum ErrorType;

class CIOCP final : public Singleton<CIOCP>
{
public:
	CIOCP();
	virtual ~CIOCP();

public:
	CIOCP(const CIOCP&) = delete;
	CIOCP& operator=(const CIOCP&) = delete;

	CIOCP(CIOCP&&) = delete;
	CIOCP& operator=(CIOCP&&) = delete;

public:
	void  Work();
	ErrorType Init();
	ErrorType Start();
	ErrorType Close();
	ErrorType Regist(const HANDLE _listen);

private:
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;	
};

#define IOCP CIOCP::Instance()