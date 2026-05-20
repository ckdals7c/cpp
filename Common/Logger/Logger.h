#pragma once

#include <format>
#include <string>
#include <string_view>

#include "Pattern/Singleton.h"

enum ErrorType;

class CLogger : public Singleton<CLogger>
{
public:
	CLogger();
	virtual ~CLogger();

public:
	CLogger(const CLogger&) = delete;
	CLogger& operator=(const CLogger&) = delete;

	CLogger(CLogger&&) = delete;
	CLogger& operator=(CLogger&&) = delete;

public:
	ErrorType PushErr(const ErrorType& _err, const std::string_view _str);

	template<typename... Args>
	ErrorType PushErrByArgs(const ErrorType& _err, const Args&&... args)
	{
		Print(Merge(std::forward<Args>(args)...));
		return  _err;
	}

private:
	template<typename... Args>
	std::string Merge(const Args&&... args)
	{
		std::string msg;
		((msg += ToString(std::forward<Args>(args)) + " "), ...);

		return msg;
	}

	template<typename T>
	std::string ToString(T&& value)
	{
		return std::format("{}", std::forward<T>(value));
	}

	void Print(const std::string_view msg);
	
private:	
	struct IMPL;
	std::unique_ptr<IMPL> m_Impl;
};

#define Logger CLogger::Instance()