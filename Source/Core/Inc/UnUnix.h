/*============================================================================
	UnUnix.h: Unix system-specific declarations.

	Revision history:
		* Created by Mike Danylchuk
		* Modified for Unreal Engine standards compliance by Brandon Reinhart
============================================================================*/

// Pathnames.
#define PATH(s) appUnixPath( s )
char* appUnixPath( const char* Path );

// Load a genuine external library (OpenAL, a driver), as opposed to an engine package: defined in
// UnUnix.cpp. NULL when the library is absent.
CORE_API void* appGetSystemDllHandle( const ANSICHAR* DllName );	// ANSICHAR == TCHAR (not yet typedef'd here)

// Networking.
DWORD appGetLocalIP( void );	// an IPv4 address is 32 bits on LP64 too

// String functions.
int stricmp( const char* s, const char* t );
int strnicmp( const char* s, const char* t, int n );
char* strupr( char* s );

// Globally Unique Identifiers.
void appGetGUID( void* GUID );

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/


