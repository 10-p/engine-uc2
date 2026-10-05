/*=============================================================================
	UC2MovieSelfTest.cpp: `ucc-bin UC2Movie.UC2MovieSelfTest` (V3, written next).
=============================================================================*/

#include "UC2MoviePrivate.h"

void UUC2MovieSelfTestCommandlet::StaticConstructor()
{
	guard(UUC2MovieSelfTestCommandlet::StaticConstructor);
	LogToStdout		= 1;
	IsClient		= 0;
	IsEditor		= 0;
	IsServer		= 0;
	LazyLoad		= 1;
	ShowErrorCount	= 0;
	unguard;
}

INT UUC2MovieSelfTestCommandlet::Main( const TCHAR* Parms )
{
	return 0;
}
