#pragma once

#include <memory>
#include <string_view>

#include "Define/Type.h"
#include "Pattern/Singleton.h"

enum ErrorType;

class CServer: public Singleton<CServer>
{
public:
	CServer();
	virtual ~CServer();

public:
	CServer(const CServer&) = delete;
	CServer& operator=(const CServer&) = delete;

	CServer(CServer&&) = delete;
	CServer& operator=(CServer&&) = delete;

public:
	ErrorType Init(const INT32 _port);
	ErrorType Start();
	ErrorType Run();
	ErrorType Close(const ErrorType _err, const std::string_view _msg);
	ErrorType Close();		

private:
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;
};

#define CommonServer CServer::Instance()