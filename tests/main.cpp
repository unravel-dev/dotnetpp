#include "dotnetpp_suite.h"

#include <dotnetpp/dotnet_jit.h>

int main()
{
	dotnet::compiler_paths paths;
	if(!dotnet::init(paths))
	{
		return 1;
	}

	dotnetpp::test_suite();

	dotnet::shutdown();

	return 0;
}
