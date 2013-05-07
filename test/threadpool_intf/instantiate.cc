#include "mock.h"


int
main()
{
	mock_client c;
	mock_service s;

	ilias::threadpool_attach(c, s);
}
