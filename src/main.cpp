#include "CNetEvent.h"
#include "wLog.h"

int main(int argc, char* argv[])
{
	char logname[32] = "bio-";
	WLogInit(".", logname, 1024*1024, 1024*1024*10, 1024*10);

	CNetEvent* pNet = new CNetEvent();
	pNet->start(60000);
	delete pNet;
	pNet = NULL;

	return 0;
}