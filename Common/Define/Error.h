#pragma once

#include "Type.h"

enum ErrorType
{
	Empty,
	Common,
	Nullptr,
	InitFailed,
	CreateFaield,
	InvalidLogic,
	InvalidParameter,
};

enum IOCPContextType : INT32
{
	Accept,
	Read,
	Write,
};
