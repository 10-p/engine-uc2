/*=============================================================================
	UnMisc.cpp: Various core platform-independent functions.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

// Core includes.
#include "CorePrivate.h"
#include <stdlib.h>

CORE_API FCriticalSection *gethostbyname_mutex = NULL;

/*-----------------------------------------------------------------------------
	FOutputDevice implementation.
-----------------------------------------------------------------------------*/

void FOutputDevice::Log( EName Event, const TCHAR* Str )
{
	if( !FName::SafeSuppressed(Event) )
		Serialize( Str, Event );
}
void FOutputDevice::Log( const TCHAR* Str )
{
	if( !FName::SafeSuppressed(NAME_Log) )
		Serialize( Str, NAME_Log );
}
void FOutputDevice::Log( const FString& S )
{
	if( !FName::SafeSuppressed(NAME_Log) )
		Serialize( *S, NAME_Log );
}
void FOutputDevice::Log( enum EName Type, const FString& S )
{
	if( !FName::SafeSuppressed(Type) )
		Serialize( *S, Type );
}
void FOutputDevice::Logf( EName Event, const TCHAR* Fmt, ... )
{
	if( !FName::SafeSuppressed(Event) )
	{
		TCHAR TempStr[4096];
		GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Fmt,Fmt);
		Serialize( TempStr, Event );
	}
}
void FOutputDevice::Logf( const TCHAR* Fmt, ... )
{
	if( !FName::SafeSuppressed(NAME_Log) )
	{
		TCHAR TempStr[4096];
		GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Fmt,Fmt);
		Serialize( TempStr, NAME_Log );
	}
}

// rcg04262002 Not sure what the ramifications are for other platforms yet.
#if ((defined __LINUX__) && (UNICODE))
void FOutputDevice::Log( const ANSICHAR* S )
{
    Log(ANSI_TO_TCHAR(S));
}
void FOutputDevice::Log( enum EName Type, const ANSICHAR* S )
{
    Log(Type, ANSI_TO_TCHAR(S));
}
void FOutputDevice::Logf( const ANSICHAR* Fmt, ... )
{
    TCHAR *cvtfmt = ANSI_TO_TCHAR(Fmt);
	if( !FName::SafeSuppressed(NAME_Log) )
	{
		TCHAR TempStr[4096];
		GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Fmt,cvtfmt);
		Serialize( TempStr, NAME_Log );
	}
}
void FOutputDevice::Logf( EName Event, const ANSICHAR* Fmt, ... )
{
    TCHAR *cvtfmt = ANSI_TO_TCHAR(Fmt);
	if( !FName::SafeSuppressed(Event) )
	{
		TCHAR TempStr[4096];
		GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Fmt,cvtfmt);
		Serialize( TempStr, Event );
	}
}
#endif

/*-----------------------------------------------------------------------------
	FArray implementation.
-----------------------------------------------------------------------------*/

void FArray::Remove( INT Index, INT Count, INT ElementSize )
{
	guardSlow(FArray::Remove);
	if( Count )
	{
		appMemmove
		(
			(BYTE*)Data + (Index      ) * ElementSize,
			(BYTE*)Data + (Index+Count) * ElementSize,
			(ArrayNum - Index - Count ) * ElementSize
		);
		ArrayNum -= Count;
		if
		(	(3*ArrayNum<2*ArrayMax || (ArrayMax-ArrayNum)*ElementSize>=16384)
		&&	(ArrayMax-ArrayNum>64 || ArrayNum==0) )
		{
			ArrayMax = ArrayNum;
			Realloc( ElementSize );
		}
	}
	checkSlow(ArrayNum>=0);
	checkSlow(ArrayMax>=ArrayNum);
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	FString implementation.
-----------------------------------------------------------------------------*/

FString FString::Chr( TCHAR Ch )
{
	guardSlow(FString::Chr);
	TCHAR Temp[2]={Ch,0};
	return FString(Temp);
	unguardSlow;
}

FString FString::LeftPad( INT ChCount )
{
	guardSlow(FString::LeftPad);
	INT Pad = ChCount - Len();
	if( Pad > 0 )
	{
		TCHAR* Ch = (TCHAR*)appAlloca((Pad+1)*sizeof(TCHAR));
		INT i;
		for( i=0; i<Pad; i++ )
			Ch[i] = ' ';
		Ch[i] = 0;
		return FString(Ch) + *this;
	}
	else return *this;
	unguardSlow;
}
FString FString::RightPad( INT ChCount )
{
	guardSlow(FString::RightPad);
	INT Pad = ChCount - Len();
	if( Pad > 0 )
	{
		TCHAR* Ch = (TCHAR*)appAlloca((Pad+1)*sizeof(TCHAR));
		INT i;
		for( i=0; i<Pad; i++ )
			Ch[i] = ' ';
		Ch[i] = 0;
		return *this + FString(Ch);
	}
	else return *this;
	unguardSlow;
}
FString::FString( BYTE Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( SBYTE Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( _WORD Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( SWORD Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( INT Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( DWORD Arg, INT Digits )
: TArray<TCHAR>()
{
}
FString::FString( FLOAT Arg, INT Digits, INT RightDigits, UBOOL LeadZero )
: TArray<TCHAR>()
{
}
#ifndef __PSX2_EE__
FString::FString( DOUBLE Arg, INT Digits, INT RightDigits, UBOOL LeadZero )
: TArray<TCHAR>()
{
}
#endif
FString FString::Printf( const TCHAR* Fmt, ... )
{
	TCHAR TempStr[4096];
	GET_VARARGS( TempStr, ARRAY_COUNT(TempStr), Fmt, Fmt );
	return FString(TempStr);
}

/**
 * jg -- Turn off unicode
 */
CORE_API FArchive& operator<<( FArchive& Ar, FString& A )
{
	guard(FString<<);
	A.CountBytes( Ar );
	INT SaveNum = appIsPureAnsi(*A) ? A.Num() : -A.Num();
	Ar << AR_INDEX(SaveNum);
	if( Ar.IsLoading() )
	{
		// Protect against network packets allocating too much memory
		if( Ar.ArMaxSerializeSize && Abs(SaveNum) > Ar.ArMaxSerializeSize )
		{
			Ar.ArIsError = 1;
			Ar.ArIsCriticalError = 1;
			return Ar;
		}

		A.ArrayMax = A.ArrayNum = Abs(SaveNum);
		A.Realloc( sizeof(TCHAR) );
		// ANSI only string
		if( SaveNum>=0 )
		{
#if 0
			for( INT i=0; i<A.Num(); i++ )
			{
				ANSICHAR ACh;
				Ar << *(BYTE*)&ACh;
				A(i) = FromAnsi(ACh);
			}
#else
			// Just bulk read that data rather than doing one char at a time
			Ar.Serialize(&A(0),SaveNum);
#endif
		}
		else
		{
			for( INT i=0; i<A.Num(); i++ )
			{
				UNICHAR UCh;
				Ar << UCh;
				A(i) = FromUnicode(UCh);
			}
		}
		if( Ar.IsLoading() && A.Num()==1 )
		{
			A.Empty();
	}
	}
	else
	{
		if( SaveNum>=0 )
		{
#if 0
			for( INT i=0; i<A.Num(); i++ )
			{
				ANSICHAR ACh = ToAnsi(A(i));
				Ar << *(BYTE*)&ACh;
			}
#else
			// Just bulk save the data
			Ar.Serialize(&A(0),SaveNum);
#endif
		}
		else
		{
			check(0 && "jg -- This should never happen!");
			for( INT i=0; i<A.Num(); i++ )
			{
				UNICHAR UCh = ToUnicode(A(i));
				Ar << UCh;
			}
		}
	}
	return Ar;
	unguard;
}

/*-----------------------------------------------------------------------------
	String functions.
-----------------------------------------------------------------------------*/

//
// Returns whether the string is pure ANSI.
//
CORE_API UBOOL appIsPureAnsi( const TCHAR* Str )
{
#if UNICODE
	for( ; *Str; Str++ )
		if( *Str<0 || *Str>0xff )
			return 0;
#endif
	return 1;
}

//
// Failed assertion handler.
//warning: May be called at library startup time.
//
CORE_API void VARARGS appFailAssert( const ANSICHAR* Expr, const ANSICHAR* File, INT Line )
{
	appErrorf( TEXT("Assertion failed: %s [File:%s] [Line: %i]"), appFromAnsi(Expr), appFromAnsi(File), Line );
}

//
// Gets the extension of a file, such as "PCX".  Returns NULL if none.
// string if there's no extension.
//
CORE_API const TCHAR* appFExt( const TCHAR* fname )
{
	guard(appFExt);

	if( appStrstr(fname,TEXT(":")) )
		fname = appStrstr(fname,TEXT(":"))+1;

	while( appStrstr(fname,TEXT("/")) )
		fname = appStrstr(fname,TEXT("/"))+1;

	while( appStrstr(fname,TEXT(".")) )
		fname = appStrstr(fname,TEXT("."))+1;

	return fname;
	unguard;
}

//
// Get a static result string.
//
CORE_API TCHAR* appStaticString1024()
{
	guard(appStaticString1024);
	// scion sz - reduced to 16 from 256 entries
	const INT MAX_STATIC_STRINGS = 16;
	static TCHAR Results[MAX_STATIC_STRINGS][1024];
	static INT Count=0;
	TCHAR* Result = Results[Count++ & (MAX_STATIC_STRINGS-1)];
	*Result = 0;
	return Result;
	unguard;
}

//
// Get an ANSI static string.
//
CORE_API ANSICHAR* appAnsiStaticString1024()
{
	return (ANSICHAR*)appStaticString1024();
}


//
// Convert an integer to a string.
//
CORE_API TCHAR* appItoa( const INT Num )
{
#if _MSC_VER
#if UNICODE
	static TCHAR Buffer[20];
	appMemzero( Buffer, 20*sizeof(TCHAR) );
	return _itow( Num, Buffer, 10 );
#else
	static char Buffer[20];
	appMemzero( Buffer, 20*sizeof(char) );
	return _itoa( Num, Buffer, 10 );
#endif
#else
	// TCHAR is ANSI off MSVC (as engine-ut2004's UnAnsi.cpp does on Unix).
	static TCHAR Buffer[20];
	snprintf( Buffer, sizeof(Buffer), "%d", Num );
	return Buffer;
#endif
}

//
// Convert a 64 bit integer to a string.
//
CORE_API TCHAR* appI64toa( const SQWORD Num )
{
#if _MSC_VER
#if UNICODE
	static TCHAR Buffer[20];
	appMemzero( Buffer, 20*sizeof(TCHAR) );
	return _i64tow( Num, Buffer, 10 );
#else
	static char Buffer[20];
	appMemzero( Buffer, 20*sizeof(char) );
	return _i64toa( Num, Buffer, 10 );
#endif
#else
	static TCHAR Buffer[24];
	snprintf( Buffer, sizeof(Buffer), "%lld", (long long)Num );
	return Buffer;
#endif
}


//
// Find string in string, case insensitive, requires non-alphanumeric lead-in.
//
const TCHAR* appStrfind( const TCHAR* Str, const TCHAR* Find )
{
	guard(appStrfind);	
	UBOOL Alnum  = 0;
	TCHAR f      = (*Find<'a' || *Find>'z') ? (*Find) : (*Find+'A'-'a');
	INT   Length = appStrlen(Find++)-1;
	TCHAR c      = *Str++;
	while( c )
	{
		if( c>='a' && c<='z' )
			c += 'A'-'a';
		if( !Alnum && c==f && !appStrnicmp(Str,Find,Length) )
			return Str-1;
		Alnum = (c>='A' && c<='Z') || (c>='0' && c<='9');
		c = *Str++;
	}
	return NULL;
	unguard;
}

//
// Returns a certain number of spaces.
// Only one return value is valid at a time.
//
const TCHAR* appSpc( INT Num )
{
	guard(spc);
	static TCHAR Spacing[256];
	static INT OldNum=-1;
	if( Num != OldNum )
	{
		for( OldNum=0; OldNum<Num; OldNum++ )
			Spacing[OldNum] = ' ';
		Spacing[Num] = 0;
	}
	return Spacing;
	unguard;
}

// 
// Trim spaces from an asciiz string by zeroing them.
//
void appTrimSpaces( ANSICHAR* String )
{		
	guard(appTrimSpaces);
	// Find 0 terminator.
	INT t=0;
	while( (String[t]!=0 ) && (t< 1024) ) t++;
	if (t>0) t--;
	// Zero trailing spaces.
	while( (String[t]==32) && (t>0) )
	{
		String[t]=0;
		t--;
	}
	unguard;
}


/*-----------------------------------------------------------------------------
	Memory functions.
-----------------------------------------------------------------------------*/

//
// Memory functions.
//
CORE_API void appMemswap( void* Ptr1, void* Ptr2, DWORD Size )
{
	void* Temp = appAlloca(Size);
	appMemcpy( Temp, Ptr1, Size );
	appMemcpy( Ptr1, Ptr2, Size );
	appMemcpy( Ptr2, Temp, Size );
}

/*-----------------------------------------------------------------------------
	CRC functions.
-----------------------------------------------------------------------------*/

// CRC 32 polynomial.
#define CRC32_POLY 0x04c11db7
CORE_API DWORD GCRCTable[256];

//
// CRC32 computer based on CRC32_POLY.
//
DWORD appMemCrc( const void* InData, INT Length, DWORD CRC )
{
	guardSlow(appMemCrc);
	BYTE* Data = (BYTE*)InData;
	CRC = ~CRC;
	for( INT i=0; i<Length; i++ )
		CRC = (CRC << 8) ^ GCRCTable[(CRC >> 24) ^ Data[i]];
	return ~CRC;
	unguardSlow;
}

//
// String CRC.
//
DWORD appStrCrc( const TCHAR* Data )
{
	guardSlow(appStrCrc);
	INT Length = appStrlen( Data );
	DWORD CRC = 0xFFFFFFFF;
	for( INT i=0; i<Length; i++ )
	{
		TCHAR C   = Data[i];
		INT   CL  = (C&255);
		CRC       = (CRC << 8) ^ GCRCTable[(CRC >> 24) ^ CL];;
#if UNICODE
		INT   CH  = (C>>8)&255;
		CRC       = (CRC << 8) ^ GCRCTable[(CRC >> 24) ^ CH];;
#endif
	}
	return ~CRC;
	unguardSlow;
}

//
// String CRC, case insensitive.
//
DWORD appStrCrcCaps( const TCHAR* Data )
{
	guardSlow(appStrCrcCaps);
	INT Length = appStrlen( Data );
	DWORD CRC = 0xFFFFFFFF;
	for( INT i=0; i<Length; i++ )
	{
		TCHAR C   = appToUpper(Data[i]);
		INT   CL  = (C&255);
		CRC       = (CRC << 8) ^ GCRCTable[(CRC >> 24) ^ CL];
#if UNICODE
		INT   CH  = (C>>8)&255;
		CRC       = (CRC << 8) ^ GCRCTable[(CRC >> 24) ^ CH];
#endif
	}
	return ~CRC;
	unguardSlow;
}

//
// Returns smallest N such that (1<<N)>=Arg.
// Note: appCeilLogTwo(0)=0 because (1<<0)=1 >= 0.
//superville: ie. used to compute the number of bits needed to store Arg
//
static BYTE GLogs[257];
CORE_API BYTE appCeilLogTwo( DWORD Arg )
{
	if( --Arg == MAXDWORD )
		return 0;
	BYTE Shift = Arg<=0x10000 ? (Arg<=0x100?0:8) : (Arg<=0x1000000?16:24);
	return Shift + GLogs[Arg>>Shift];
}

/*-----------------------------------------------------------------------------
	Exceptions.
-----------------------------------------------------------------------------*/

//
// Throw a string exception with a message.
//
CORE_API void VARARGS appThrowf( const TCHAR* Fmt, ... )
{
	static TCHAR TempStr[4096];
	GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Fmt,Fmt);
	throw( TempStr );
}

/*-----------------------------------------------------------------------------
	Parameter parsing.
-----------------------------------------------------------------------------*/

//
// Get a string from a text string.
//
CORE_API UBOOL Parse
(
	const TCHAR* Stream, 
	const TCHAR* Match,
	TCHAR*		 Value,
	INT			 MaxLen
)
{
	guard(ParseString);

	const TCHAR* Found = appStrfind(Stream,Match);
	const TCHAR* Start;

	if( Found )
	{
		Start = Found + appStrlen(Match);
		if( *Start == '\x22' )
		{
#if 1

#if 0	// slower UT2004 version
			const TCHAR* Pos = Start + 1;
			INT i;
			for( i=0;i<MaxLen-1 && *Pos; i++,Pos++ )
			{
				if( *Pos == '\x22' )
					break;
				if( *Pos == '\\' && (Pos[1]=='\\' || Pos[1]=='\x22') )
				{
					Pos++;
					if( *Pos == 0 )
						break;
				}
				Value[i] = *Pos;
			}
			Value[i]=0;
#else	// faster console version

			// copy the entire string into a temporary buffer
			TCHAR* Buffer = new TCHAR[MaxLen];
			appStrncpy(Buffer,Start+1,MaxLen);

			// set our starting position to the beginning of our temp buffer
			TCHAR* current = Buffer;

			// pre-terminate the result string
			*Value=0;

			// get the position of the next " character
			// this might be the end of our delimited string, but we'll need
			// to check for escape sequences first
			TCHAR* endQuote = appStrchr(current, '\x22');

			// see if the string has any escapes
			while ( TCHAR* esc = appStrchr(current, '\\') )
			{
				// if the " chracter came before the escape char, then we're done
				if ( endQuote && endQuote < esc )
					break;

				// otherwise, if the next character after the escape char is
				// a \ or ", we've found an escape sequence
				if ( esc[1] == '\\' || esc[1] == '\x22' )
				{
					// move the real character into the escape character's position
					*esc = esc[1];
					// advance the pointer, and null terminate at the position immediately
					// after the previously escaped character
					*++esc = 0;

					// copy the string [up to this point] into our result, and set our
					// "current" pointer to the next character (it's currently pointing 
					// at the terminating character)
					appStrncat(Value, current, MaxLen);
					current=esc+1;

					// search for the next " character
					endQuote = appStrchr(current+1,'\x22');
				}
				// otherwise, it was just a single backslash...since these should always be escaped,
				// we'll assume that this is a string that expects the previous behavior (no support
				// for escaped characters), so we should assume endQuote is currently pointing at the
				// real closing quote
				else break;
			}

			// replace the closing quote with the null terminator
			if ( endQuote )
				*endQuote = 0;

			// and copy the remainder of the string into our result
			appStrncat(Value,current,MaxLen);

			delete [] Buffer;
#endif // slow/fast version

#else
			// rjp - nuked as it doesn't support escaped characters
			// Quoted string with spaces.
			appStrncpy( Value, Start+1, MaxLen );
			Value[MaxLen-1]=0;
			TCHAR* Temp = appStrstr( Value, TEXT("\x22") );
			if( Temp != NULL )
				*Temp=0;
#endif
		}
		else
		{
			// Non-quoted string without spaces.
			appStrncpy( Value, Start, MaxLen );
			Value[MaxLen-1]=0;
			TCHAR* Temp;
			Temp = appStrstr( Value, TEXT(" ")  ); if( Temp ) *Temp=0;
			Temp = appStrstr( Value, TEXT("\r") ); if( Temp ) *Temp=0;
			Temp = appStrstr( Value, TEXT("\n") ); if( Temp ) *Temp=0;
			Temp = appStrstr( Value, TEXT("\t") ); if( Temp ) *Temp=0;
			Temp = appStrstr( Value, TEXT(",")  ); if( Temp ) *Temp=0;
		}
		return 1;
	}
	else return 0;
	unguard;
}

//
// See if a command-line parameter exists in the stream.
//
UBOOL CORE_API ParseParam( const TCHAR* Stream, const TCHAR* Param )
{
	guard(GetParam);
	const TCHAR* Start = Stream;
	if( *Stream )
		while( (Start=appStrfind(Start+1,Param)) != NULL )
			if( Start>Stream && (Start[-1]=='-' || Start[-1]=='/') )
				return 1;
	return 0;
	unguard;
}

// 
// Parse a string.
//
UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, FString& Value )
{
	guard(FString::Parse);
	TCHAR Temp[4096]=TEXT("");
	if( ::Parse( Stream, Match, Temp, ARRAY_COUNT(Temp) ) )
	{
		Value = Temp;
		return 1;
	}
	else return 0;
	unguard;
}

//
// Parse a quadword.
//
UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, QWORD& Value )
{
	guard(ParseQWORD);
	return Parse( Stream, Match, *(SQWORD*)&Value );
	unguard;
}

//
// Parse a signed quadword.
//
UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, SQWORD& Value )
{
	guard(ParseSQWORD);
	TCHAR Temp[4096]=TEXT(""), *Ptr=Temp;
	if( ::Parse( Stream, Match, Temp, ARRAY_COUNT(Temp) ) )
	{
		Value = 0;
		UBOOL Negative = (*Ptr=='-');
		Ptr += Negative;
		while( *Ptr>='0' && *Ptr<='9' )
			Value = Value*10 + *Ptr++ - '0';
		if( Negative )
			Value = -Value;
		return 1;
	}
	else return 0;
	unguard;
}

//
// Get an object from a text stream.
//
CORE_API UBOOL ParseObject( const TCHAR* Stream, const TCHAR* Match, UClass* Class, UObject*& DestRes, UObject* InParent )
{
	guard(ParseUObject);
	TCHAR TempStr[256];
	if( !Parse( Stream, Match, TempStr, NAME_SIZE ) )
	{
		return 0;
	}
	else if( appStricmp(TempStr,TEXT("NONE"))==0 )
	{
		DestRes = NULL;
		return 1;
	}
	else
	{
		// Look this object up.
		UObject* Res;
		Res = UObject::StaticFindObject( Class, InParent, TempStr );
		if( !Res )
			return 0;
		DestRes = Res;
		return 1;
	}
	unguard;
}

//
// Get a name.
//
CORE_API UBOOL Parse
(
	const TCHAR* Stream, 
	const TCHAR* Match, 
	FName& Name
)
{
	guard(ParseFName);
	TCHAR TempStr[NAME_SIZE];

	if( !Parse(Stream,Match,TempStr,NAME_SIZE) )
		return 0;
	Name = FName( TempStr );

	return 1;
	unguard;
}

//
// Get a DWORD.
//
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, DWORD& Value )
{
	guard(ParseDWORD);

	const TCHAR* Temp = appStrfind(Stream,Match);
	TCHAR* End;
	if( Temp==NULL )
		return 0;
	Value = appStrtoi( Temp + appStrlen(Match), &End, 10 );

	return 1;
	unguard;
}

//
// Get a byte.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, BYTE& Value )
{
	guard(ParseBYTE);

	const TCHAR* Temp = appStrfind(Stream,Match);
	if( Temp==NULL )
		return 0;
	Temp += appStrlen( Match );
	Value = (BYTE)appAtoi( Temp );
	return Value!=0 || appIsDigit(Temp[0]);

	unguard;
}

//
// Get a signed byte.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, SBYTE& Value )
{
	guard(ParseCHAR);
	const TCHAR* Temp = appStrfind(Stream,Match);
	if( Temp==NULL )
		return 0;
	Temp += appStrlen( Match );
	Value = appAtoi( Temp );
	return Value!=0 || appIsDigit(Temp[0]);
	unguard;
}

//
// Get a word.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, _WORD& Value )
{
	guard(ParseWORD);
	const TCHAR* Temp = appStrfind( Stream, Match );
	if( Temp==NULL )
		return 0;
	Temp += appStrlen( Match );
	Value = (_WORD)appAtoi( Temp );
	return Value!=0 || appIsDigit(Temp[0]);
	unguard;
}

//
// Get a signed word.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, SWORD& Value )
{
	guard(ParseSWORD);
	const TCHAR* Temp = appStrfind( Stream, Match );
	if( Temp==NULL )
		return 0;
	Temp += appStrlen( Match );
	Value = (SWORD)appAtoi( Temp );
	return Value!=0 || appIsDigit(Temp[0]);
	unguard;
}

//
// Get a floating-point number.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, FLOAT& Value )
{
	guard(ParseFLOAT);
	const TCHAR* Temp = appStrfind( Stream, Match );
	if( Temp==NULL )
		return 0;
	Value = appAtof( Temp+appStrlen(Match) );
	return 1;
	unguard;
}

//
// Get a signed double word.
//
UBOOL CORE_API Parse( const TCHAR* Stream, const TCHAR* Match, INT& Value )
{
	guard(ParseINT);
	const TCHAR* Temp = appStrfind( Stream, Match );
	if( Temp==NULL )
		return 0;
	Value = appAtoi( Temp + appStrlen(Match) );
	return 1;
	unguard;
}

//
// Get a boolean value.
//
UBOOL CORE_API ParseUBOOL( const TCHAR* Stream, const TCHAR* Match, UBOOL& OnOff )
{
	guard(ParseUBOOL);
	TCHAR TempStr[16];
	if( Parse( Stream, Match, TempStr, 16 ) )
	{
		OnOff
		=	!appStricmp(TempStr,TEXT("On"))
		||	!appStricmp(TempStr,TEXT("True"))
		||	!appStricmp(TempStr,GTrue)
		||	!appStricmp(TempStr,TEXT("1"));
		return 1;
	}
	else return 0;
	unguard;
}

//
// Get a globally unique identifier.
//
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, class FGuid& Guid )
{
	guard(ParseGUID);

	TCHAR Temp[256];
	if( !Parse( Stream, Match, Temp, ARRAY_COUNT(Temp) ) )
		return 0;

	Guid.A = Guid.B = Guid.C = Guid.D = 0;
	if( appStrlen(Temp)==32 )
	{
		TCHAR* End;
		Guid.D = appStrtoi( Temp+24, &End, 16 ); Temp[24]=0;
		Guid.C = appStrtoi( Temp+16, &End, 16 ); Temp[16]=0;
		Guid.B = appStrtoi( Temp+8,  &End, 16 ); Temp[8 ]=0;
		Guid.A = appStrtoi( Temp+0,  &End, 16 ); Temp[0 ]=0;
	}
	return 1;

	unguard;
}

//
// Sees if Stream starts with the named command.  If it does,
// skips through the command and blanks past it.  Returns 1 of match,
// 0 if not.
//
CORE_API UBOOL ParseCommand
(
	const TCHAR** Stream, 
	const TCHAR*  Match
)
{
	guard(ParseCommand);

	while( (**Stream==' ')||(**Stream==9) )
		(*Stream)++;

	if( appStrnicmp(*Stream,Match,appStrlen(Match))==0 )
	{
		*Stream += appStrlen(Match);
		if( !appIsAlnum(**Stream) )
		{
			while ((**Stream==' ')||(**Stream==9)) (*Stream)++;
			return 1; // Success.
		}
		else
		{
			*Stream -= appStrlen(Match);
			return 0; // Only found partial match.
		}
	}
	else return 0; // No match.
	unguard;
}

//
// Get next command.  Skips past comments and cr's.
//
CORE_API void ParseNext( const TCHAR** Stream )
{
	guard(ParseNext);

	// Skip over spaces, tabs, cr's, and linefeeds.
	SkipJunk:
	while( **Stream==' ' || **Stream==9 || **Stream==13 || **Stream==10 )
		++*Stream;

	if( **Stream==';' )
	{
		// Skip past comments.
		while( **Stream!=0 && **Stream!=10 && **Stream!=13 )
			++*Stream;
		goto SkipJunk;
	}

	// Upon exit, *Stream either points to valid Stream or a nul.
	unguard;
}

//
// Grab the next space-delimited string from the input stream.
// If quoted, gets entire quoted string.
//
CORE_API UBOOL ParseToken( const TCHAR*& Str, TCHAR* Result, INT MaxLen, UBOOL UseEscape )
{
	guard(ParseToken);
	INT Len=0;

	// Skip spaces and tabs.
	while( *Str==' ' || *Str==9 )
		Str++;
	if( *Str == 34 )
	{
		// Get quoted string.
		Str++;
		while( *Str && *Str!=34 && (Len+1)<MaxLen )
		{
			TCHAR c = *Str++;
			if( c=='\\' && UseEscape )
			{
				// Get escape.
				c = *Str++;
				if( !c )
					break;
			}
			if( (Len+1)<MaxLen )
				Result[Len++] = c;
		}
		if( *Str==34 )
			Str++;
	}
	else
	{
		// Get unquoted string.
		for( ; *Str && *Str!=' ' && *Str!=9; Str++ )
			if( (Len+1)<MaxLen )
				Result[Len++] = *Str;
	}
	Result[Len]=0;
	return Len!=0;
	unguard;
}
CORE_API UBOOL ParseToken( const TCHAR*& Str, FString& Arg, UBOOL UseEscape )
{
	TCHAR Buffer[1024];
	if( ParseToken( Str, Buffer, ARRAY_COUNT(Buffer), UseEscape ) )
	{
		Arg = Buffer;
		return 1;
	}
	return 0;
}
CORE_API FString ParseToken( const TCHAR*& Str, UBOOL UseEscape )
{
	TCHAR Buffer[1024];
	if( ParseToken( Str, Buffer, ARRAY_COUNT(Buffer), UseEscape ) )
		return Buffer;
	else
		return TEXT("");
}

//
// Get a line of Stream (everything up to, but not including, CR/LF.
// Returns 0 if ok, nonzero if at end of stream and returned 0-length string.
//
CORE_API UBOOL ParseLine
(
	const TCHAR**	Stream,
	TCHAR*			Result,
	INT				MaxLen,
	UBOOL			Exact
)
{
	guard(ParseLine);
	UBOOL GotStream=0;
	UBOOL IsQuoted=0;
	UBOOL Ignore=0;

	*Result=0;
	while( **Stream!=0 && **Stream!=10 && **Stream!=13 && --MaxLen>0 )
	{
		// Start of comments.
		if( !IsQuoted && !Exact && (*Stream)[0]=='/' && (*Stream)[1]=='/' )
			Ignore = 1;
		
		// Command chaining.
		if( !IsQuoted && !Exact && **Stream=='|' )
			break;

		// Check quoting.
		IsQuoted = IsQuoted ^ (**Stream==34);
		GotStream=1;

		// Got stuff.
		if( !Ignore )
			*(Result++) = *((*Stream)++);
		else
			(*Stream)++;
	}
	if( Exact )
	{
		// Eat up exactly one CR/LF.
		if( **Stream == 13 )
			(*Stream)++;
		if( **Stream == 10 )
			(*Stream)++;
	}
	else
	{
		// Eat up all CR/LF's.
		while( **Stream==10 || **Stream==13 || **Stream=='|' )
			(*Stream)++;
	}
	*Result=0;
	return **Stream!=0 || GotStream;
	unguard;
}
CORE_API UBOOL ParseLine
(
	const TCHAR**	Stream,
	FString&		Result,
	UBOOL			Exact
)
{
	guard(ParseLine);
	TCHAR Temp[4096]=TEXT("");
	UBOOL Success = ParseLine( Stream, Temp, ARRAY_COUNT(Temp), Exact );
	Result = Temp;
	return Success;
	unguard;
}

/*----------------------------------------------------------------------------
	String substitution.
----------------------------------------------------------------------------*/

CORE_API FString appFormat( FString Src, const TMultiMap<FName,FString>& Map )
{
	guard(appFormat);
	FString Result;
	for( INT Toggle=0; ; Toggle^=1 )
	{
		INT Pos=Src.InStr(TEXT("%")), NewPos=Pos>=0 ? Pos : Src.Len();
		FString Str = Src.Left( NewPos );
		if( Toggle )
		{
			const FString* Ptr = Map.Find(FName(*Str));
			if( Ptr )
				Result += *Ptr;
			else if( NewPos!=Src.Len() )
				Result += US + TEXT("%") + Str + TEXT("%");
			else
				Result += US + TEXT("%") + Str;
		}
		else Result += Str;
		Src = Src.Mid( NewPos+1 );
		if( Pos<0 )
			break;
	}
	return Result;
	unguard;
}

/*----------------------------------------------------------------------------
	Localization.
----------------------------------------------------------------------------*/

/*==========================================================================================================
function	::LocalizePropertyValue
Desc:		Retrieve the localized string for a specified package, section, key.

input:		Section - name of section in localization file (i.e. GameInfo)
			Key - key to search for in the section specified (i.e. GameInfo.GameName)
			Package - name of file to look in (i.e. Engine)
			Size - size of the buffer specified by out_Result
			LangExt - abbreviation for the language to use
			Optional - query for localized value is not required 
					(do not use english version if localized version isn't found) 


output:		out_Result  - TCHAR buffer that will hold the result

return:		Returns whether the value was found.

last modified by:	Ron Prestenback
==========================================================================================================*/
CORE_API UBOOL LocalizePropertyValue( const TCHAR* Section, const TCHAR* Key, const TCHAR* Package, TCHAR* out_Result, INT Size, const TCHAR* LangExt, UBOOL bOptional)
{
	guard(LocalizePropertyValue);

	if( !GIsStarted || !GConfig )
	{
		appStrncpy(out_Result, Key, Size);
		return true;
	}

	TCHAR Filename[256];
	UBOOL bResult = false;

	// Build file name from localization folder, package name, and language extension
	// Composite is "<FolderName>\<PackageName>.<LangExtension>"
	// (ie "Localization\jap\Core.jap")
	appSprintf( Filename, TEXT("%s%s.%s"), UObject::GetLocalizationFolder(), Package, LangExt );

	if( GConfig->GetString(Section, Key, out_Result, Size, Filename) )
		bResult = true;

	// If Key was NOT found in given file/section of localization folder
	else
	{
		if ( appStricmp(UObject::GetLanguage(), TEXT("int")) )
		{
			// Try default localization folder next - rebuild file name
			appSprintf( Filename, TEXT("%s%s.%s"), UObject::GetDefaultLocalizationFolder(), Package, TEXT("int") );

			if( GConfig->GetString(Section, Key, out_Result, Size, Filename) )
				bResult = true;
		}

		// If key was NOT found in default localization folder either
		if ( !bResult )
		{

			// At this point we were looking in Default localization folder already
			// and "we ain't foun' shiiiiit!"
			if ( bOptional )
				*out_Result = 0;

			// If query wasn't optional, don't allow NULL to be returned
			else
			{
				for (INT i = 0; i < GSys->LocPaths.Num(); i++)
				{
					// Strip wildcard.
					INT StrIndex = GSys->LocPaths(i).InStr("*.");
					FString BasePath = StrIndex >= 0 ? GSys->LocPaths(i).Left(StrIndex) : GSys->Paths(i);

					appSprintf( Filename, TEXT("%s%s%s.int"), *BasePath, UObject::GetDefaultLocalizationFolder(), Package );
					if( GConfig->GetString( Section, Key, out_Result, Size, Filename ) )
					{
						bResult = true;
						break;
					}
				}

				// No matching localization file found.
				if ( !bResult )
				{
					// Log that the property wasn't found
					debugf( NAME_Localization, TEXT("No localization: %s.%s.%s (%s)"), Package, Section, Key, LangExt );
					// Result becomes composite that couldn't be found
					appSprintf( out_Result, TEXT("<?%s?%s.%s.%s?>"), LangExt, Package, Section, Key );
				}
			}
		}
	}

	return bResult;

	unguard;
}

//scion ===========================================================
//	SCION Function: ::Localize
//	Author: superville
//
//	Description: Retrieve the localized string for given lang,
//		package, section, and key.
//
//	Input:	const TCHAR* Section - Name of section in config file (ie "Engine.GameInfo")
//			const TCHAR* Key	 - Name of variable value to find in section (ie "GameName")
//			const TCHAR* Package - Name of package localized variable exists in (ie "Core")
//					DEFAULT ARG = GPackage
//			const TCHAR* LangExt - Initial language extension to search through
//					DEFAULT ARG = UObject::GetLanguage()
//			UBOOL Optional		 - Query for localized value is not required (safe to return NULL)
//					DEFAULT ARG = 0
//
//	Output: const TCHAR* value of Key if successful, NULL or composite string of failed path if NOT
//				depending on Optional bool
//	Last Modified: 11/10/04 (ron)
// ================================================================
CORE_API const TCHAR* Localize( const TCHAR* Section, const TCHAR* Key, const TCHAR* Package, const TCHAR* LangExt, UBOOL Optional )
{
	guard(Localize);

	//scion jg -- Verify this is called correctly
	checkSlow(LangExt);
	TCHAR* Result = appStaticString1024();
	LocalizePropertyValue(Section, Key, Package, Result, 1024, LangExt, Optional);
	return Result;

	unguard;
}
CORE_API const TCHAR* LocalizeError( const TCHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return Localize( TEXT("Errors"), Key, Package, LangExt );
}
CORE_API const TCHAR* LocalizeProgress( const TCHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return Localize( TEXT("Progress"), Key, Package, LangExt );
}
CORE_API const TCHAR* LocalizeQuery( const TCHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return Localize( TEXT("Query"), Key, Package, LangExt );
}
CORE_API const TCHAR* LocalizeGeneral( const TCHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return Localize( TEXT("General"), Key, Package, LangExt );
}

#if 0//UNICODE
CORE_API const TCHAR* Localize( const ANSICHAR* Section, const ANSICHAR* Key, const TCHAR* Package, const TCHAR* LangExt, UBOOL Optional )
{
	return Localize( appFromAnsi(Section), appFromAnsi(Key), Package, LangExt, Optional );
}
CORE_API UBOOL LocalizePropertyValue( const ANSICHAR* Section, const ANSICHAR* Key, const TCHAR* Package, TCHAR* Result, INT Size, const TCHAR* LangExt, UBOOL bOptional)
{
	return LocalizePropertyValue(appFromAnsi(Section), appFromAnsi(Key), Package, Result, Size, LangExt, bOptional);
}

CORE_API const TCHAR* LocalizeError( const ANSICHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return LocalizeError( appFromAnsi(Key), Package, LangExt );
}
CORE_API const TCHAR* LocalizeProgress( const ANSICHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return LocalizeProgress( appFromAnsi(Key), Package, LangExt );
}
CORE_API const TCHAR* LocalizeQuery( const ANSICHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return LocalizeQuery( appFromAnsi(Key), Package, LangExt );
}
CORE_API const TCHAR* LocalizeGeneral( const ANSICHAR* Key, const TCHAR* Package, const TCHAR* LangExt )
{
	return LocalizeGeneral( appFromAnsi(Key), Package, LangExt );
}
#endif

/*-----------------------------------------------------------------------------
	High level file functions.
-----------------------------------------------------------------------------*/

//
// Update file modification time.
//
CORE_API UBOOL appUpdateFileModTime( TCHAR* Filename )
{
	guard(appUpdateFileModTime);
	FArchive* Ar = GFileManager->CreateFileWriter(Filename,FILEWRITE_Append,GNull);
	if( Ar )
	{
		delete Ar;
		return 1;
	}
	return 0;
	unguard;
}

//
// Load a binary file to a dynamic array.
//
CORE_API UBOOL appLoadFileToArray( TArray<BYTE>& Result, const TCHAR* Filename, FFileManager* FileManager )
{
	guard(appLoadFileToArray);
	FArchive* Reader = FileManager->CreateFileReader( Filename );
	if( !Reader )
		return 0;
	Result.Empty();
	Result.Add( Reader->TotalSize() );
	Reader->Serialize( &Result(0), Result.Num() );
	UBOOL Success = Reader->Close();
	delete Reader;
	return Success;
	unguard;
}

#ifdef _XBOX//jg -- Much faster string loading code for the demo
/**
 * Reads the data directly into the output string, rather than going
 * through various conversion steps.
 *
 * @param Result the out param that gets the file contents
 * @param Filename the name of the file to load
 * @param FileManager the file manager to open the file with
 */
CORE_API UBOOL appLoadFileToString(FString& Result,const TCHAR* Filename,
	FFileManager* FileManager)
{
	guard(appLoadFileToString);
	UBOOL bSuccess = 0;
	// Avoid the dirty disc message by checking for the file before reading
	if (FileManager->FileSize(Filename) > 0)
	{
		// Use faster reader for these files if possible
		FArchive* Reader = FileManager->CreateFileReader(Filename);
		if (Reader != NULL)
		{
			INT Size = Reader->TotalSize();
			// Used to bypass FString's protected inheritence of TArray members
			TArray<TCHAR>& BypassFString = (TArray<TCHAR>&)Result;
			// Make sure there is enough room for the null terminator. Presize so
			// there is no slack too
			BypassFString.Empty(Size + 1);
			BypassFString.Add(Size + 1);
			// Read the INI file directly in
			Reader->Serialize(&BypassFString(0),Size);
			// NULL terminate it
			BypassFString.Last() = TEXT('\0');
			// Clean up
			bSuccess = Reader->Close();
			delete Reader;
		}
	}
	return bSuccess;
	unguard;
}

#else
//
// Load a text file to an FString.
// Supports all combination of ANSI/Unicode files and platforms.
//
CORE_API UBOOL appLoadFileToString( FString& Result, const TCHAR* Filename, FFileManager* FileManager )
{
	guard(appLoadFileToString);
	//jg -- Use faster reader for these files if possible
	FArchive* Reader = FileManager->CreateLinearReader(Filename);
	// If the file manager doesn't support the faster reader fall back to old style
	if (Reader == NULL)
	{
		Reader = FileManager->CreateFileReader(Filename);
	}
	if( !Reader )
		return 0;
	INT Size = Reader->TotalSize();
	TArray<ANSICHAR> Ch( Size+2 );
	Reader->Serialize( &Ch(0), Size );
	UBOOL Success = Reader->Close();
	delete Reader;
	Ch( Size+0 )=0;
	Ch( Size+1 )=0;
	TArray<TCHAR>& ResultArray = Result.GetCharArray();
	ResultArray.Empty();
	if( Size>=2 && !(Size&1) && (BYTE)Ch(0)==0xff && (BYTE)Ch(1)==0xfe )
	{
		// Unicode Intel byte order.
		ResultArray.Add( Size/sizeof(TCHAR) );
		for( INT i=0; i<ResultArray.Num()-1; i++ )
			ResultArray( i ) = FromUnicode( (_WORD)(ANSICHARU)Ch(i*2+2) + (_WORD)(ANSICHARU)Ch(i*2+3)*256 );
	}
	else if( Size>=2 && !(Size&1) && (BYTE)Ch(0)==0xfe && (BYTE)Ch(1)==0xff )
	{
		// Unicode non-Intel byte order.
		ResultArray.Add( Size/sizeof(TCHAR) );
		for( INT i=0; i<ResultArray.Num()-1; i++ )
			ResultArray( i ) = FromUnicode( (_WORD)(ANSICHARU)Ch(i*2+3) + (_WORD)(ANSICHARU)Ch(i*2+2)*256 );
	}
	else
	{
		// ANSI.
		ResultArray.Add( Size+1 );
		for( INT i=0; i<ResultArray.Num()-1; i++ )
			ResultArray( i ) = FromAnsi( Ch(i) );
	}
	ResultArray.Last() = 0;
	return Success;
	unguard;
}
#endif

//
// Save a binary array to a file.
//
CORE_API UBOOL appSaveArrayToFile( const TArray<BYTE>& Array, const TCHAR* Filename, FFileManager* FileManager )
{
	guard(appSaveArrayToFile);
	FArchive* Ar = FileManager->CreateFileWriter( Filename );
	if( !Ar )
		return 0;
	Ar->Serialize( const_cast<BYTE*>(&Array(0)), Array.Num() );
	delete Ar;
	return 1;
	unguard;
}

//
// Write the FString to a file.
// Supports all combination of ANSI/Unicode files and platforms.
//
CORE_API UBOOL appSaveStringToFile( const FString& String, const TCHAR* Filename, FFileManager* FileManager )
{
	guard(appSaveStringToFile);
	if( !String.Len() )
		return 0;
	FArchive* Ar = FileManager->CreateFileWriter( Filename );
	if( !Ar )
		return 0;
	UBOOL SaveAsUnicode=0, Success=1;
#if UNICODE
	for( INT i=0; i<String.Len(); i++ )
	{
		if( (*String)[i] != (TCHAR)(ANSICHARU)ToAnsi((*String)[i]) )
		{
			UNICHAR BOM = UNICODE_BOM;
			Ar->Serialize( &BOM, sizeof(BOM) );
			SaveAsUnicode = 1;
			break;
		}
	}
#endif
	if( SaveAsUnicode || sizeof(TCHAR)==1 )
	{
		Ar->Serialize( const_cast<TCHAR*>(*String), String.Len()*sizeof(TCHAR) );
	}
	else
	{
		TArray<ANSICHAR> AnsiBuffer(String.Len());
		for( INT i=0; i<String.Len(); i++ )
			AnsiBuffer(i) = ToAnsi((*String)[i]);
		Ar->Serialize( const_cast<ANSICHAR*>(&AnsiBuffer(0)), String.Len() );
	}
	delete Ar;
	if( !Success )
		GFileManager->Delete( Filename );
	return Success;
	unguard;
}

static INT BitmapIndex = -1;
UBOOL appCreateBitmap( const TCHAR* Pattern, INT Width, INT Height, DWORD* Data, FFileManager* FileManager )
{
	TCHAR File[64];

	if( BitmapIndex == -1 )
	{
		for( INT TestBitmapIndex=0; TestBitmapIndex<65536; TestBitmapIndex++ )
		{
			appSprintf( File, TEXT("%s%05i.bmp"), Pattern, TestBitmapIndex );
			if( FileManager->FileSize(File) < 0 )
			{
				BitmapIndex = TestBitmapIndex;
				break;
			}
		}
	}

	appSprintf( File, TEXT("%s%05i.bmp"), Pattern, BitmapIndex++ );

	if( FileManager->FileSize(File)<0 )
	{
		FArchive* Ar = FileManager->CreateFileWriter( File );
		if( Ar )
		{
			// Types.
			#if SUPPORTS_PRAGMA_PACK
				#pragma pack (push,1)
			#endif
			struct BITMAPFILEHEADER
			{
				_WORD   bfType GCC_PACK(1);
				DWORD   bfSize GCC_PACK(1);
				_WORD   bfReserved1 GCC_PACK(1); 
				_WORD   bfReserved2 GCC_PACK(1);
				DWORD   bfOffBits GCC_PACK(1);
			} FH; 
			struct BITMAPINFOHEADER
			{
				DWORD  biSize GCC_PACK(1); 
				INT    biWidth GCC_PACK(1);
				INT    biHeight GCC_PACK(1);
				_WORD  biPlanes GCC_PACK(1);
				_WORD  biBitCount GCC_PACK(1);
				DWORD  biCompression GCC_PACK(1);
				DWORD  biSizeImage GCC_PACK(1);
				INT    biXPelsPerMeter GCC_PACK(1); 
				INT    biYPelsPerMeter GCC_PACK(1);
				DWORD  biClrUsed GCC_PACK(1);
				DWORD  biClrImportant GCC_PACK(1); 
			} IH;
			#if SUPPORTS_PRAGMA_PACK
				#pragma pack (pop)
			#endif

			// File header.
			FH.bfType		= 'B' + 256*'M';
			FH.bfSize		= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 3 * Width * Height;
			FH.bfReserved1	= 0;
			FH.bfReserved2	= 0;
			FH.bfOffBits	= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
			Ar->Serialize( &FH, sizeof(FH) );

			// Info header.
			IH.biSize			= sizeof(BITMAPINFOHEADER);
			IH.biWidth			= Width;
			IH.biHeight			= Height;
			IH.biPlanes			= 1;
			IH.biBitCount		= 24;
			IH.biCompression	= 0; //BI_RGB
			IH.biSizeImage		= Width * Height * 3;
			IH.biXPelsPerMeter	= 0;
			IH.biYPelsPerMeter	= 0;
			IH.biClrUsed		= 0;
			IH.biClrImportant	= 0;
			Ar->Serialize( &IH, sizeof(IH) );

			// Colors.
			for( INT i=Height-1; i>=0; i-- )
			{
				for( INT j=0; j<Width; j++ )
				{
					Ar->Serialize( &Data[i*Width+j], 3 );
				}
			}

			// Success.
			delete Ar;
		}
		else 
			return 0;
	}
	else 
		return 0;

	// Success.
	return 1;
}


/*-----------------------------------------------------------------------------
	Files.
-----------------------------------------------------------------------------*/

//
// Find a file.
//
UBOOL appFindPackageFile( const TCHAR* InIn, const FGuid* Guid, TCHAR* Out )
{
	guard(appFindPackageFile);
	TCHAR Temp[256];
	TCHAR In[200];
	appStrncpy( In, InIn, ARRAY_COUNT(In) );	// only 200 chars, allow room for extra path stuff appened to filename.

	// Don't return it if it's a library.
	if( appStrlen(In)>appStrlen(DLLEXT) && appStricmp( In + appStrlen(In)-appStrlen(DLLEXT), DLLEXT )==0 )
		return 0;

	// If using non-default language, search for internationalized version.
	UBOOL International = (appStricmp(UObject::GetLanguage(),TEXT("int"))!=0);

	//jg -- Special hack for detecting package-less packages
	if (appStricmp(TEXT("XboxDrv"),InIn) == 0 ||
		appStricmp(TEXT("XboxAudio"),InIn) == 0 ||
		appStricmp(TEXT("D3DDrv"),InIn) == 0)
	{
		return 0;
	}
	// Try checking the TOC version first
	if (GFileManager->FindPackageFile(In,Out) != 0)
	{
		return 1;
	}

	// Try file as specified.
	appStrcpy( Out, In );
	if( GFileManager->FileSize( Out ) >= 0 )
		return 1;

	// Try all of the predefined paths.
	INT DoCd;
	for( DoCd=0; DoCd<(1+(GCdPath[0]!=0)); DoCd++ )
	{
		for( INT i=DoCd; i<GSys->Paths.Num()+(Guid!=NULL); i++ )
		{
			for( INT j=0; j<International+1; j++ )
			{
				// Get directory only.
				const TCHAR* Ext;
				*Temp = 0;
				if( DoCd )
				{
					appStrcat( Temp, GCdPath );
					appStrcat( Temp, TEXT("System") );
                    appStrcat( Temp, PATH_SEPARATOR );
				}
				if( i<GSys->Paths.Num() )
				{
					appStrcat( Temp, *GSys->Paths(i) );
					TCHAR* Ext2 = appStrstr(Temp,TEXT("*"));
					if( Ext2 )
						*Ext2++ = 0;
					Ext = Ext2;
					appStrcpy( Out, Temp );
					appStrcat( Out, In );
				}
				else
				{
					appStrcat( Temp, *GSys->CachePath );
					appStrcat( Temp, PATH_SEPARATOR );
					Ext = *GSys->CacheExt;
					appStrcpy( Out, Temp );
					appStrcat( Out, Guid->String() );
				}

				// Check for file.
				UBOOL Found = 0;
				Found = (GFileManager->FileSize(Out)>=0);
				if( !Found && Ext )
				{
					appStrcat( Out, TEXT(".") );
					if( International-j )
					{
						appStrcat( Out, UObject::GetLanguage() );
						appStrcat( Out, TEXT("_") );
					}
					appStrcat( Out, Ext+1 );
					Found = (GFileManager->FileSize( Out )>=0);
				}
				if( Found )
				{
					if( i==GSys->Paths.Num() )
						appUpdateFileModTime( Out );
					return 1;
				}
			}
		}
	}

	// Try case-insensitive search.
	for( DoCd=0; DoCd<(1+(GCdPath[0]!=0)); DoCd++ )
	{
		for( INT i=0; i<GSys->Paths.Num()+(Guid!=NULL); i++ )
		{
			// Get directory only.
			const TCHAR* Ext;
			*Temp = 0;
			if( DoCd )
			{
				appStrcat( Temp, GCdPath );
				appStrcat( Temp, TEXT("System") );
                appStrcat( Temp, PATH_SEPARATOR );
			}
			if( i<GSys->Paths.Num() )
			{
				appStrcat( Temp, *GSys->Paths(i) );
				TCHAR* Ext2 = appStrstr(Temp,TEXT("*"));
				if( Ext2 )
					*Ext2++ = 0;
				Ext = Ext2;
				appStrcpy( Out, Temp );
				appStrcat( Out, In );
			}
			else
			{
				appStrcat( Temp, *GSys->CachePath );
				appStrcat( Temp, PATH_SEPARATOR );
				Ext = *GSys->CacheExt;
				appStrcpy( Out, Temp );
				appStrcat( Out, Guid->String() );
			}

			// Find files.
			TCHAR Spec[256];
			*Spec = 0;
			TArray<FString> Files;
			appStrcpy( Spec, Temp );
			appStrcat( Spec, TEXT("*") );
			if( Ext )
				appStrcat( Spec, Ext );
			Files = GFileManager->FindFiles( Spec, 1, 0 );

			// Check for match.
			UBOOL Found = 0;
			TCHAR InExt[256];
			*InExt = 0;
			if( Ext )
			{
				appStrcpy( InExt, In );
				appStrcat( InExt, Ext );
			}
			for( INT j=0; Files.IsValidIndex(j); j++ )
			{
				if( (appStricmp( *(Files(j)), In )==0) ||
					(appStricmp( *(Files(j)), InExt)==0) )
				{
					appStrcpy( Out, Temp );
					appStrcat( Out, *(Files(j)));
					Found = (GFileManager->FileSize( Out )>=0);
				}
			}
			if( Found )
			{
				debugf( TEXT("Case-insensitive search: %s -> %s"), In, Out );
				if( i==GSys->Paths.Num() )
					appUpdateFileModTime( Out );
				return 1;
			}
		}
	}

	// Not found.
	return 0;
	unguard;
}

//
// Create a temporary file.
//
CORE_API void appCreateTempFilename( const TCHAR* Path, TCHAR* Result256 )
{
	guard(appCreateTempFilename);
	static INT i=0;
	do
		appSprintf( Result256, TEXT("%s%04X.tmp"), Path, i++ );
	while( GFileManager->FileSize(Result256)>0 );
	unguard;
}

//
// File Stream implementation.
//

FFileStream* FFileStream::Init( INT InMaxStreams )
{
	if (Instance == NULL)
	{
		Instance	= NEW FFileStream;
		Streams		= new FStream[InMaxStreams];
		CriticalSection	= GSynchronizeFactory->CreateCriticalSection();
		MaxStreams	= InMaxStreams;
		appMemzero( Streams, sizeof(FStream)*InMaxStreams );
	}
	return Instance;
}

void FFileStream::Destroy()
{
	if ( Instance != NULL && Destroyed == 0 )
	{
		Destroyed = 1;
#ifndef _XBOX
		debugf(TEXT("Waiting for file streaming thread to finish..."));
		while (Destroyed == 1)
			appSleep(1);
#endif
		if ( Streams )
		{
			delete [] Streams;
			Streams = NULL;
		}
		delete CriticalSection;
		delete Instance;
		Instance = NULL;
	}
}

INT FFileStream::CreateStream( const TCHAR* Filename, INT ChunkSize, INT InitialChunks, void* Data, EFileStreamType Type, void* TDD )
{
	FScopeLock sl( CriticalSection );

	Streams[StreamIndex].Type				= Type;
	Streams[StreamIndex].TDD				= TDD;

	if (!Create( StreamIndex, Filename ) )
	{
		appMemzero( &Streams[StreamIndex], sizeof(Streams[StreamIndex]) );
		return -1;
	}
		
	Streams[StreamIndex].Data				= Data;
	Streams[StreamIndex].ChunkSize			= ChunkSize;
	Streams[StreamIndex].ChunksRequested	= 0;
	Streams[StreamIndex].EndOfFile			= 0;
	Streams[StreamIndex].Used				= 1; 

	Read( StreamIndex, ChunkSize * InitialChunks );

	INT SI = StreamIndex; 
	++StreamIndex %= MaxStreams;
	return SI;
}

void FFileStream::DestroyStream( INT StreamId, UBOOL ReadQueuedChunks )
{
	FScopeLock sl( CriticalSection );
	
	if (Streams[StreamId].Used)
	{
		if (ReadQueuedChunks)
			Read( StreamId, Streams[StreamId].ChunkSize * Streams[StreamId].ChunksRequested ); 
		Destroy(StreamId);
	}
}

void FFileStream::RequestChunks( INT StreamId, INT Chunks, void* Data )
{
	FScopeLock sl( CriticalSection );

	Streams[StreamId].ChunksRequested += Chunks;
	Streams[StreamId].Data = Data;
}

UBOOL FFileStream::QueryStream( INT StreamId, INT& ChunksQueued )
{
	FScopeLock sl( CriticalSection );

	ChunksQueued = Streams[StreamId].ChunksRequested;
	UBOOL Alive	 = !Streams[StreamId].EndOfFile && Streams[StreamId].Used;

	return Alive;
}

FFileStream* FFileStream::Instance	= 0;
FStream* FFileStream::Streams		= 0;
INT	FFileStream::StreamIndex		= 0;
INT FFileStream::MaxStreams			= 0;
INT FFileStream::Destroyed			= 0;
FCriticalSection* FFileStream::CriticalSection	= 0;

/*-----------------------------------------------------------------------------
	Init and Exit.
-----------------------------------------------------------------------------*/

//
// General initialization.
//
#ifndef FINAL_RELEASE
TCHAR GCmdLine[4096]=TEXT("");
#else
TCHAR GCmdLine[256]=TEXT("");
#endif
CORE_API const TCHAR* appCmdLine()
{
	return GCmdLine;
}

CORE_API void appInit( const TCHAR* InPackage, const TCHAR* InCmdLine, FMalloc* InMalloc, FOutputDevice* InLog, FOutputDeviceError* InError, FFeedbackContext* InWarn, FFileManager* InFileManager, FConfigCache*(*ConfigFactory)(), UBOOL RequireConfig )
{
	guard(appInit);
	GFileManager = InFileManager;

	// Init CRC table.
    for( DWORD iCRC=0; iCRC<256; iCRC++ )
		for( DWORD c=iCRC<<24, j=8; j!=0; j-- )
			GCRCTable[iCRC] = c = c & 0x80000000 ? (c << 1) ^ CRC32_POLY : (c << 1);

	// Init log table.
	{for( INT i=0,e=-1,c=0; i<=256; i++ )
	{
		GLogs[i] = e+1;
		if( !i || ++c>=(1<<e) )
			c=0, e++;
	}}

	// Command line.
	// Only the Windows command line starts with the executable's name; the portable launchers pass the
	// arguments alone, so skipping a token there would eat the first argument (the map URL).
#if !defined(_XBOX) && defined(_MSC_VER)
	if( *InCmdLine=='\"' )
	{
		InCmdLine++;
		while( *InCmdLine && *InCmdLine!='\"' )
			InCmdLine++;
		if( *InCmdLine )
			InCmdLine++;
	}
	while( *InCmdLine && *InCmdLine!=' ' )
		InCmdLine++;
	if( *InCmdLine )
		InCmdLine++;
#endif
	appStrcpy( GCmdLine, InCmdLine );

	// Error history.
	appStrcpy( GErrorHist, TEXT("General protection fault!\r\n\r\nHistory: ") );

	// Subsystems.
	GLog         = InLog;
	GError       = InError;
	GWarn        = InWarn;

	// Memory allocator.
	GMalloc = InMalloc;
	GMalloc->Init();

	//jg -- Initialize our synchronization factory
	if (GSynchronizeFactory != NULL)
	{
		GSynchronizeFactory->Init();
	}

	//jg -- Initialize our synchronization factory
	if (GThreadFactory != NULL)
	{
		GThreadFactory->Init();
	}

	// Init names.
	FName::StaticInit();

	// Platform specific pre-init.
	appPlatformPreInit();

	// Switch into executable's directory.
	GFileManager->Init(1);
#ifndef _XBOX//jg -- Removed this since the constructor sets it in the default case and I set it for demos
	GFileManager->SetDefaultDirectory( appBaseDir() );
#endif

	// Command line.
	debugf( NAME_Init, TEXT("Version: %i"), ENGINE_VERSION );
	debugf( NAME_Init, TEXT("Compiled: %s %s"), appFromAnsi(__DATE__), appFromAnsi(__TIME__) );
	debugf( NAME_Init, TEXT("Command line: %s"), appCmdLine() );
#ifndef _XBOX//jg -- Get the base dir setting from the file manager
	debugf( NAME_Init, TEXT("Base directory: %s"), appBaseDir() );
#else
	debugf( NAME_Init, TEXT("Base directory: %s"), *GFileManager->GetDefaultDirectory() );
#endif
	debugf( NAME_Init, TEXT("Character set: %s"), sizeof(TCHAR)==1 ? TEXT("ANSI") : TEXT("Unicode") );

	// Parameters.
	GIsStrict = ParseParam( appCmdLine(), TEXT("STRICT") );

	// Ini.
#ifndef _XBOX//jg -- Always use the INI name and always read the default
	if( !Parse( appCmdLine(), TEXT("INI="), GIni, ARRAY_COUNT(GIni) ) )
		appSprintf( GIni, TEXT("%s.ini"), InPackage );
#endif
	if( GFileManager->FileSize(GIni)<0 && RequireConfig )
	{
		// Create Package.ini from default.ini.
		FString S;
		if( !appLoadFileToString( S, TEXT("Default.ini"), GFileManager ) )
			appErrorf( LocalizeError(TEXT("MisingIni"),TEXT("Core")), "Default.ini" );
		appSaveStringToFile( S, GIni );
	}

	// User Ini.
#ifndef _XBOX//jg -- Always use the INI name and always read the default user
	if( !Parse( appCmdLine(), TEXT("USERINI="), GUserIni, ARRAY_COUNT(GUserIni) ) )
		appStrcpy( GUserIni, TEXT("User.ini") );
#endif
	if( GFileManager->FileSize(GUserIni)<0 && RequireConfig )
	{
		// Create User.ini from DefUser.ini.
		FString S;
		if( !appLoadFileToString( S, TEXT("DefUser.ini"), GFileManager ) )
			appErrorf( LocalizeError(TEXT("MisingIni"),TEXT("Core")), "DefUser.ini" );
		appSaveStringToFile( S, GUserIni );
	}

	debugf( NAME_Init, TEXT("Ini:%s   UserIni:%s"), GIni, GUserIni );

 	// Init config.
	GConfig = ConfigFactory();
	GConfig->Init( GIni, GUserIni, RequireConfig );

	// Language.
#ifdef _XBOX
	//scion jg -- Pick the correct language based upon the xbox's language
	DWORD dwLang = appGetLanguageID();
	check(dwLang > LC_INT && dwLang < LC_MAXLANGS);
	// Now tell the UObject system to use this language for localization
	UObject::SetLanguage(GLanguageExt[dwLang]);
#else
	TCHAR Temp[256];
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("Language"), Temp, ARRAY_COUNT(Temp) ) )
		UObject::SetLanguage( Temp );
#endif

	// Object initialization.
	UObject::StaticInit();

	// Memory initalization.
	GMem.Init( 65536 );

	// Init the file streaming singleton.
	GFileStream = FFileStream::Init( 16 );

	// Platform specific init.
	appPlatformInit();

	check(gethostbyname_mutex == NULL);
	gethostbyname_mutex = GSynchronizeFactory->CreateCriticalSection();

	unguard;
}

//
// Pre-shutdown.
// Called from within guarded exit code, only during non-error exits.
//
CORE_API void appPreExit()
{
	guard(appPreExit);

	debugf( NAME_Exit, TEXT("Preparing to exit.") );
	appPlatformPreExit();
	GMem.Exit();
	UObject::StaticExit();

	unguard;
}

//
// Shutdown.
// Called outside guarded exit code, during all exits (including error exits).
//
void appExit()
{
	guard(appExit);
	debugf( NAME_Exit, TEXT("Exiting.") );
	GFileStream->Destroy();
	appPlatformExit();
	if( GConfig )
	{
		GConfig->Exit();
		delete GConfig;
		GConfig = NULL;
	}
	FName::StaticExit();
#ifndef _XBOX
	if( !GIsCriticalError )
		GMalloc->DumpAllocs();
#endif

	delete gethostbyname_mutex;

	unguard;
}

/*-----------------------------------------------------------------------------
	String conversion.
-----------------------------------------------------------------------------*/

// TCHAR to ANSICHAR.
CORE_API const ANSICHAR* appToAnsi( const TCHAR* Str, ANSICHAR* ACh )
{
	guard(appToAnsi);
#if UNICODE
	if( !Str )
		return NULL;
	ANSICHAR* InCh = ACh;
	if( !ACh )
		ACh = appAnsiStaticString1024();
	INT Count;
	for( Count=0; (InCh || Count<1024-1) && Str[Count]; Count++ )
		ACh[Count] = ToAnsi( Str[Count] );
	ACh[Count] = 0;
	return ACh;
#else
	return ACh && Str ? appStrcpy(ACh, Str) : Str;
#endif
	unguard;
}

// TCHAR to UNICHAR.
CORE_API const UNICHAR* appToUnicode( const TCHAR* Str, UNICHAR* UChar )
{
#if UNICODE
	return UChar && Str ? appStrcpy(UChar, Str) : Str;
#else
	guard(appToUnicode);
	if( !Str )
		return NULL;
	static UNICHAR UCh[1024];
	INT Count;

	UNICHAR* Result = UChar ? UChar : UCh;
	for( Count=0; (UChar || Count<ARRAY_COUNT(UCh)-1) && Str[Count]; Count++ )
		Result[Count] = ToUnicode( Str[Count] );

	Result[Count] = 0;
	return Result;
	unguard;
#endif
}

// ANSICHAR to TCHAR.
CORE_API const TCHAR* appFromAnsi( const ANSICHAR* ACh, TCHAR* Ch )
{
#if UNICODE
	guard(appFromAnsi);
	if( !ACh )
		return NULL;
	TCHAR* InCh = Ch;
	if( !Ch )
		Ch = appStaticString1024();
	INT Count;
	for( Count=0; (InCh || Count<1024-1) && ACh[Count]; Count++ )
		Ch[Count] = FromAnsi( ACh[Count] );
	Ch[Count] = 0;
	return Ch;
	unguard;
#else
	return ACh && Ch ? appStrcpy(Ch, ACh) : ACh;
#endif
}

// UNICHAR to TCHAR.
CORE_API const TCHAR* appFromUnicode( const UNICHAR* UCh )
{
#if UNICODE
	return UCh;
#else
	guard(appFromUnicode);
	if( !UCh )
		return NULL;
	TCHAR* Ch = appStaticString1024();
	INT Count;
	for( Count=0; Count<1024-1 && UCh[Count]; Count++ )
		Ch[Count] = FromUnicode( UCh[Count] );
	Ch[Count] = 0;
	return Ch;
	unguard;
#endif
}

/*-----------------------------------------------------------------------------
	Error handling.
-----------------------------------------------------------------------------*/

//
// Unwind the stack.
//
CORE_API void VARARGS appUnwindf( const TCHAR* Fmt, ... )
{
	GIsCriticalError = 1;

	TCHAR TempStr[4096];
	GET_VARARGS( TempStr, ARRAY_COUNT(TempStr), Fmt, Fmt );

	static INT Count=0;
	if( Count++ )
		appStrncat( GErrorHist, TEXT(" <- "), ARRAY_COUNT(GErrorHist) );
	appStrncat( GErrorHist, TempStr, ARRAY_COUNT(GErrorHist) );

	debugf( NAME_Critical, TempStr );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

