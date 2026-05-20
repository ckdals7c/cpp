#include <Server/Server.h>
#include <Define/Error.h>

int main() 
{
	ErrorType err = CommonServer.Init(9000);
	if (ErrorType::Empty != err) {
		return 0;
	}

	err = CommonServer.Start();
	if (ErrorType::Empty != err) {
		return 0;
	}

	err = CommonServer.Run();
	if (ErrorType::Empty != err) {
		return 0;
	}

	err = CommonServer.Close();
	return 0;
}