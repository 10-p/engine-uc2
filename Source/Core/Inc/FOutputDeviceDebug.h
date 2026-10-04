/*=============================================================================
	FOutputDeviceDebug.h: Windows debugging text output device.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

//
// ANSI file output device.
//
class FOutputDeviceDebug : public FOutputDevice
{
public:
	void Serialize( const TCHAR* Data, enum EName Event )
	{
		static UBOOL Entry=0;
		if( !GIsCriticalError || Entry )
		{
			if( !FName::SafeSuppressed(Event) )
			{
				if( Event!=NAME_Title )
				{
#ifndef _XBOX
					TCHAR	Temp[1024];
					appSprintf(Temp, TEXT("%s: %s"),FName::SafeString(Event),Data);
					OutputDebugString(Temp);
#else
#ifdef DEBUG_LOGGING
					// multiple calls to OutputDebugString interfere when logging time-sensitive events,
					// such as replication problems and network timeouts.  Also, XbWatson adds a timestamp
					// entry for each call to OutputDebugString, so multiple calls to OutputDebugString make
					// the timestamps option of watson very difficulty to use.
					static TCHAR Buffer[2048]; 
					appSnprintf(Buffer, 2048, TEXT("%s: %s\n"), FName::SafeString(Event), Data);
					OutputDebugString(Buffer);
#endif
#endif
				}

				if( GLogHook )
					GLogHook->Serialize( Data, Event );
			}
		}
		else
		{
			Entry=1;
			guard(Serialize);
			{
				// Ignore errors to prevent infinite-recursive exception reporting.
				Serialize( Data, Event );
			}
			unguard;
			Entry=0;
		}
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

