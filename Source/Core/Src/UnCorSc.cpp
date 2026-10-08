/*=============================================================================
	UnCorSc.cpp: UnrealScript execution and support code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Description:
	UnrealScript execution and support code.

Revision history:
	* Created by Tim Sweeney

=============================================================================*/

#include "CorePrivate.h"

#if PLATFORM_64BITS
// M13 (ufront 2.83, engine-ut99 253aaf2): the last None-context zero-fill, where it wrote and how many bytes.
static BYTE* GNullContextResult = NULL;
static INT   GNullContextSize   = 0;
#endif
#include "UnColor.h" //scion bcl -- need FColor to define ColorLerp

#ifndef _XBOX
#include "OpCode.h"
#include "UC2ScriptSaveConfig.h"
#endif

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

CORE_API void (UObject::*GNatives[EX_Max])( FFrame &Stack, RESULT_DECL );
CORE_API INT GNativeDuplicate=0;

CORE_API void (UObject::*GCasts[CST_Max])( FFrame &Stack, RESULT_DECL );
CORE_API INT GCastDuplicate=0;

//scion jg -- Added line number support for accessed nones
CORE_API INT GLineNumber = -1;

#define RUNAWAY_LIMIT 1000000
#define RECURSE_LIMIT 100

#if DO_GUARD
	static INT Runaway=0;
	static INT Recurse=0;
#ifndef _XBOX
	#define CHECK_RUNAWAY {if( ++Runaway > RUNAWAY_LIMIT ) {if(!ParseParam(appCmdLine(),TEXT("norunaway"))&&(!GDebugger||!GDebugger->NotifyInfiniteLoop())) Stack.Logf( NAME_Critical, TEXT("Runaway loop detected (over %i iterations)"), RUNAWAY_LIMIT ); Runaway=0;}}
#else
	#define CHECK_RUNAWAY {if( ++Runaway > RUNAWAY_LIMIT ) {Stack.Logf( NAME_Critical, TEXT("Runaway loop detected (over %i iterations)"), RUNAWAY_LIMIT ); Runaway=0;}}
#endif
	CORE_API void GInitRunaway() {Recurse=Runaway=0;}
#else
	#define CHECK_RUNAWAY
	CORE_API void GInitRunaway() {}
#endif

#define IMPLEMENT_CAST_FUNCTION(cls,num,func) \
		IMPLEMENT_FUNCTION(cls,-1,func); \
		static BYTE cls##func##CastTemp = GRegisterCast( num, int##cls##func );


/*-----------------------------------------------------------------------------
	Call graph profiling.
-----------------------------------------------------------------------------*/

#if PROFILE
/**
 * Constructor
 *
 * @param InSoftMemoryLimit		Max number of bytes used by call graph data (soft limit).
 */
FScriptCallGraph::FScriptCallGraph( DWORD InSoftMemoryLimit )
{
	check( InSoftMemoryLimit );
	Reset( InSoftMemoryLimit );
}

/**
 * Resets data collection and memory use.
 *
 * @param InSoftMemoryLimit		Max number of bytes used by call graph data (soft limit).
 */
void FScriptCallGraph::Reset( DWORD InSoftMemoryLimit )
{
	if( InSoftMemoryLimit )
	{
		SoftMemoryLimit = InSoftMemoryLimit;
	}

	Data.Empty( SoftMemoryLimit * 11 / 10 / sizeof(DWORD) );
	Data.AddZeroed( SoftMemoryLimit * 11 / 10 / sizeof(DWORD) );
}

/**
 * Serializes the profiling data to the passed in archive.	
 *
 * @param Ar	Archive to serialize data to.
 */
void FScriptCallGraph::Serialize( FArchive& Ar )
{
	if( Data.Num() )
	{
		DWORD StartIndex	= 0;
		DWORD EndIndex		= 0;

		// Skip the first frame as we most likely are in the middle of the call stack.
		while( StartIndex<Data.Num() && !IsFrameEndMarker(Data(StartIndex)) )
		{
			StartIndex++;
		}
		check(StartIndex<Data.Num());

		// Find the end of the call graph data.
		EndIndex = StartIndex;
		while( EndIndex<Data.Num() && !IsEndMarker(Data(EndIndex)) )
		{
			EndIndex++;
		}

		// Create a list of all functions currently loaded.
		TArray<UFunction*> Functions;
		TMap<DWORD, UFunction*> FuncMap;
		for( TObjectIterator<UFunction> It; It; ++It )
		{
			Functions.AddItem( *It );
			FuncMap.Add((DWORD)*It, *It);
		}

		// First value to serialize is the number of usecs per cycle. (DOUBLE)
		DOUBLE usecsPerCycle = GSecondsPerCycle * 1000.0 * 1000.0;
		Ar << usecsPerCycle;

		// Second value is the number of functions that are being serialized (DWORD).
		DWORD FunctionCount = Functions.Num();
		Ar << FunctionCount;

		// postprocess the data to remove cycles spent in functions we don't care about
		DWORD Index = StartIndex + 1;
		// current stack depth
		INT Depth = 0;
		// cycles used by ignored function(s) at each stack depth
		TArray<DWORD> IgnoreCycles;
		while (Index < EndIndex)
		{
			while (IsFrameEndMarker(Data(Index)))
			{
				Index++;
			}
			UFunction** Function;
			while (Index < EndIndex && (Function = FuncMap.Find(Data(Index))) != NULL)
			{
				Depth++;
				Index++;
				// see if we should ignore this function
				static FName IgnoreNames[] =	{	TEXT("PlaySoundCue"), TEXT("LocalPlaySoundCue"),
													TEXT("RadiusPlaySoundCue"), TEXT("ClientPlaySoundCue")	};
				UBOOL bIgnored = false;
				for (INT i = 0; i < ARRAY_COUNT(IgnoreNames) && !bIgnored; i++)
				{
					if ((*Function)->GetFName() == IgnoreNames[i])
					{
						INT IgnoreDepth = Depth;
						// ignore functions called by the ignored function
						while (Index < EndIndex && ((Function = FuncMap.Find(Data(Index))) != NULL || Depth > IgnoreDepth))
						{
							if (Function != NULL)
							{
								Depth++;
							}
							else
							{
								check((Data(Index) & 3) == 1);
								// remove the cycles used by this ignored function
								// note: need to keep the lowest bit set as that marks the data as a number of cycles
								Data(Index) = 1;
								Depth--;
							}
							Index++;
						}
						// we should now be at the token that indicates the number of cycles used by the ignored function
						check((Data(Index) & 3) == 1);
						// make sure the array is large enough
						if (IgnoreCycles.Num() < Depth)
						{
							IgnoreCycles.AddZeroed(Depth - IgnoreCycles.Num());
						}
						// add the cycles used by the ignored function to the ignored cycles for each stack depth
						for (INT i = 0; i < Depth; i++)
						{
							IgnoreCycles(i) += Data(Index);
						}
						// remove the cycles used by the ignored function
						// note: need to keep the lowest bit set as that marks the data as a number of cycles
						Data(Index) = 1;
						// advance past the ignored function
						Index++;
						Depth--;
						
						bIgnored = true;
					}
				}
			}
			if (Index < EndIndex)
			{
				if (Depth == 0)
				{
					// if we get there, something went wrong
					if ((Data(Index) & 3) == 1)
					{
						debugf(TEXT("Cycle count with no depth at index %u"), Index);
					}
					else
					{
						debugf(TEXT("Unknown value %u at index %u"), Data(Index), Index);
					}
					Index++;
				}
				else
				{
					for (; (Data(Index) & 3) == 1 && Index < EndIndex && Depth > 0; Depth--, Index++)
					{
						// if a function was ignored at this depth
						if (IgnoreCycles.Num() > Depth && IgnoreCycles(Depth) > 0)
						{
							// remove the cycles of the ignored function from the time spent in this function
							// note: need to keep the lowest bit set as that marks the data as a number of cycles
							Data(Index) = ((Data(Index) - IgnoreCycles(Depth)) & ~3) | 1;
							IgnoreCycles(Depth) = 0;
						}
					}
				}
			}
		}

		for( INT FunctionIndex=0; FunctionIndex<Functions.Num(); FunctionIndex++ )
		{
			TCHAR*		Name		= (TCHAR*) Functions(FunctionIndex)->GetPathName();
			DWORD		Length		= appStrlen(Name);
			DWORD		Pointer		= (DWORD) Functions(FunctionIndex);
#ifdef _UNICODE
			// We can't use TCHAR_TO_ANSI as it uses appAlloca and we'd be overflowing the stack with a lot
			// of functions.
			ANSICHAR*	Temp		= (ANSICHAR*) appMalloc( winGetSizeANSI(Name) );
			ANSICHAR*	AnsiName	= winToANSI( Temp, Name, winGetSizeANSI(Name) );
#else
			// TCHAR == ANSICHAR so don't need to convert
			ANSICHAR*	AnsiName	= Name;
#endif
	
			// Serialize function pointer, length of name in characters and then serialize name without the
			// trailing '\0' as a chain of ANSI characters.
			Ar << Pointer;	// (DWORD)
			Ar << Length;	// (DWORD)
			for( DWORD i=0; i<Length; i++ )
			{
				Ar << AnsiName[i];	// (ANSICHAR)
			}

#ifdef _UNICODE
			appFree(Temp);
#endif
		}

		// Serialize raw collected data.
		Ar.Serialize( &Data(StartIndex), (EndIndex - StartIndex) * sizeof(DWORD) );
	}
}

/**
 * Emits the end of frame marker. Needs to be called when there are no script functions
 * on the stack. Also responsible for reseting call graph data if memory limit is reached.
 */
void FScriptCallGraph::Tick()
{
	EmitFrameEnd();
	if( (Data.Num()*sizeof(DWORD)) > SoftMemoryLimit )
	{
		Reset( SoftMemoryLimit );
	}
}

/** Global script call graph profiler */
FScriptCallGraph* GScriptCallGraph = NULL;
#endif
	
/*-----------------------------------------------------------------------------
	FFrame implementation.
-----------------------------------------------------------------------------*/

//
// Error or warning handler.
//
void FFrame::Serialize( const TCHAR* V, EName Event )
{
	guard(FFrame::Serialize);
	if( Event==NAME_Critical || GIsStrict ) appErrorf
	(
		TEXT("%s (%s:%04X) %s"),
		Object->GetFullName(),
		Node->GetFullName(),
		Code - &Node->Script(0),
		V
	);
	else debugf
	(
		NAME_ScriptWarning,
		TEXT("%s (%s:%04X) %s"),
		Object->GetFullName(),
		Node->GetFullName(),
		Code - &Node->Script(0),
		V
	);
	unguard;
}

/*-----------------------------------------------------------------------------
	Global script execution functions.
-----------------------------------------------------------------------------*/

//
// Have an object go to a named state, and idle at no label.
// If state is NAME_None or was not found, goes to no state.
// Returns 1 if we went to a state, 0 if went to no state.
//
//scion rwd - added option to skip Begin/End state notifies
EGotoState UObject::GotoState( FName NewState, UBOOL bReset, UBOOL bSkipNotifications = false )
{
	guard(UObject::GotoState);
	if( !StateFrame )
		return GOTOSTATE_NotFound;

	StateFrame->LatentAction = 0;
	UState* StateNode = NULL;
	FName OldStateName = StateFrame->StateNode!=Class ? StateFrame->StateNode->GetFName() : FName(NAME_None);
	if( NewState != NAME_Auto )
	{
		// Find regular state.
		StateNode = FindState( NewState );
	}
	else
	{
		// Find auto state.
		//epic moelfke: the script compiler now marks the class itself as auto when no state is so we can skip the iterator
		if (!(GetClass()->StateFlags & STATE_Auto))
		{
			for (TFieldIterator<UState, CLASS_IsAUState> It(GetClass()); It && !StateNode; ++It)
			{
				if (It->StateFlags & STATE_Auto)
				{
					StateNode = *It;
				}
			}
			if (!StateNode)
			{
				debugf(NAME_Warning, TEXT("Class %s not marked as auto but should be"), GetClass()->GetName());
			}
		}
	}

	if( !StateNode )
	{
		// Going nowhere.
		NewState  = NAME_None;
		StateNode = GetClass();
	}
	else if( NewState == NAME_Auto )
	{
		// Going to auto state.
		NewState = StateNode->GetFName();
	}

	// Send EndState notification.
	if
	(	OldStateName!=NAME_None
	&&	NewState!=OldStateName
	&&	!(GetFlags() & RF_InEndState) )
	{
		if ( IsProbing(NAME_EndState) )
		{
		    ClearFlags( RF_StateChanged );
		    //scion rwd - skip the begin state notification
		    if (!bSkipNotifications)
		    {
			    SetFlags( RF_InEndState );
			    eventEndState();
			    ClearFlags( RF_InEndState );
			    if( GetFlags() & RF_StateChanged )
				    return GOTOSTATE_Preempted;
		    }
		}

#ifndef _XBOX
		if ( !bSkipNotifications )
		{
			/* UDebugger - rjp
			When changing states from within state code, before the StateFrame's code pointer is changed,
			the matching PREVSTACK for NEWSTACK that entered this state must be processed by the UDebugger
			in order to clear this state's stacknode from the UDebugger's callstack
			-- UDebugger */
			if ( StateFrame->Node == StateFrame->StateNode &&
				StateFrame->Code && *StateFrame->Code == EX_DebugInfo )
				StateFrame->Step(this,NULL);
		}
		else if ( GDebugger )
		{
			/* UDebugger - rjp
			Otherwise, manually pop the debugger stack node for this state...we'll restore it later
			*/

			GDebugger->DebugInfo(this, GetStateFrame(), DI_PrevStackState, 0, 0);
		}
#endif
	}

	// Go there.
	StateFrame->Node	   = StateNode;
	StateFrame->StateNode  = StateNode;
	StateFrame->Code	   = NULL;
	StateFrame->ProbeMask  = (StateNode->ProbeMask | GetClass()->ProbeMask) & StateNode->IgnoreMask;

	/* SCRIPTTIME
	unclock(GScriptCycles);
	debugf(TEXT("%s goto state %s at %f"),GetName(),*NewState, GScriptCycles * GSecondsPerCycle * 1000.f);
	clock(GScriptCycles);
	*/

	// Send BeginState notification.
	if( NewState!=NAME_None && (NewState!=OldStateName || bReset) && IsProbing(NAME_BeginState) )
	{
		ClearFlags( RF_StateChanged );
		//scion rwd - skip the end state notification
		if (!bSkipNotifications)
		{
			eventBeginState();
			if( GetFlags() & RF_StateChanged )
				return GOTOSTATE_Preempted;
		}
	}

	// Return result.
	if( NewState != NAME_None )
	{
		SetFlags( RF_StateChanged );
		return GOTOSTATE_Success;
	}
	else return GOTOSTATE_NotFound;

	unguard;
}

//
// Goto a label in the current state.
// Returns 1 if went, 0 if not found.
//
UBOOL UObject::GotoLabel( FName FindLabel )
{
	guard(UObject::GotoLabel);
	if( StateFrame )
	{
		StateFrame->LatentAction = 0;
		if( FindLabel != NAME_None )
		{
			for( UState* SourceState=StateFrame->StateNode; SourceState; SourceState=SourceState->GetSuperState() )
			{
				if( SourceState->LabelTableOffset != MAXWORD )
				{
					for( FLabelEntry* Label = (FLabelEntry *)&SourceState->Script(SourceState->LabelTableOffset); Label->Name!=NAME_None; Label++ )
					{
						if( Label->Name==FindLabel )
						{
							StateFrame->Node = SourceState;
							StateFrame->Code = &SourceState->Script(Label->iCode);
							return 1;
						}
					}
				}
			}
		}
		StateFrame->Code = NULL;
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Natives.
-----------------------------------------------------------------------------*/

//////////////////////////////
// Undefined native handler //
//////////////////////////////

void UObject::execUndefined( FFrame& Stack, RESULT_DECL  )
{
	guardSlow(UObject::execUndefined);

	Stack.Logf( NAME_Critical, TEXT("Unknown code token %02X"), Stack.Code[-1] );

	unguardexecSlow;
}

///////////////
// Variables //
///////////////

void UObject::execLocalVariable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLocalVariable);

	checkSlow(Stack.Object==this);
	checkSlow(Stack.Locals!=NULL);
	GProperty = (UProperty*)Stack.ReadObject();
	GPropAddr = Stack.Locals + GProperty->Offset;
	GPropObject = NULL;

	if( Result )
		GProperty->CopyCompleteValue( Result, GPropAddr );
	
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_LocalVariable, execLocalVariable );

void UObject::execInstanceVariable( FFrame& Stack, RESULT_DECL)
{
	guardSlow(UObject::execInstanceVariable);

	GProperty = (UProperty*)Stack.ReadObject();
	GPropAddr = (BYTE*)this + GProperty->Offset;
	GPropObject = this;
	if( Result )
		GProperty->CopyCompleteValue( Result, GPropAddr );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_InstanceVariable, execInstanceVariable );

void UObject::execDefaultVariable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDefaultVariable);

	GProperty = (UProperty*)Stack.ReadObject();

	// DELTA DEFAULTS
	// make sure default object exists
	GPropAddr = GetClass()->GetDefaultsSafe(GProperty->Offset);
	GPropObject = NULL;
	if( Result )
		GProperty->CopyCompleteValue( Result, GPropAddr );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DefaultVariable, execDefaultVariable );

//epic moelfke: special bytecode for out params so they can be seperated to really be pass by ref
void UObject::execLocalOutVariable(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UObject::execLocalOutVariable);

	checkSlow(Stack.Object == this);
	GProperty = (UProperty*)Stack.ReadObject();
	GPropObject = NULL;
	GPropAddr = NULL;
	for (FOutParmRec* Out = Stack.OutParms; !GPropAddr; Out = Out->NextOutParm)
	{
		checkSlow(Out);
		if (Out->Property == GProperty)
		{
			GPropAddr = Out->PropAddr;
		}
	}
	checkSlow(GPropAddr);

	if (Result)
	{
		GProperty->CopyCompleteValue(Result, GPropAddr);
	}
	
	unguardexecSlow;
}
IMPLEMENT_FUNCTION(UObject, EX_LocalOutVariable, execLocalOutVariable);

void UObject::execClassContext( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execClassContext);

	// Get class expression.
	UClass* ClassContext=NULL;
	Stack.Step( Stack.Object, &ClassContext );

	// Execute expression in class context.
	if( ClassContext )
	{
		Stack.Code += 3;
		Stack.Step( ClassContext->GetDefaultObject(), Result );
	}
	else
	{
		if ( GProperty )
			Stack.Logf( NAME_Error, TEXT("Accessed null class context '%s'"), GProperty->GetName() );	// rjp
		else Stack.Logf( NAME_Error, TEXT("Accessed null class context") ); // gam

#ifndef _XBOX
		if ( GDebugger )
			GDebugger->NotifyAccessedNone();
#endif

		INT wSkip = Stack.ReadWord();
		BYTE bSize = *Stack.Code++;
		Stack.Code += wSkip;
		GPropAddr = NULL;
		GPropObject = NULL;
		GProperty = NULL;
		if( Result )
		{
			appMemzero( Result, bSize );
#if PLATFORM_64BITS
			GNullContextResult = (BYTE*)Result;
			GNullContextSize   = bSize;
#endif
		}
	}
	unguardexec;
}
IMPLEMENT_FUNCTION( UObject, EX_ClassContext, execClassContext );

void UObject::execArrayElement( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execArrayElement);

	// Get array index expression.
	INT Index=0;
	Stack.Step( Stack.Object, &Index );

	// Get base element (must be a variable!!).
	GProperty = NULL;
	Stack.Step( this, NULL );
	GPropObject = this;

	// Add scaled offset to base pointer.
	if( GProperty && GPropAddr )
	{
		// Bounds check.
		if( Index>=GProperty->ArrayDim || Index<0 )
		{
			// Display out-of-bounds warning and continue on with index clamped to valid range.
			Stack.Logf( NAME_Error, TEXT("Accessed array '%s' out of bounds (%i/%i)"), GProperty->GetName(), Index, GProperty->ArrayDim ); // gam, rjp
			Index = Clamp( Index, 0, GProperty->ArrayDim - 1 );
		}

		// Update address.
		GPropAddr += Index * GProperty->ElementSize;
		guardSlow(ArrayElementCopySingleValue);
		if( Result )//!!
			GProperty->CopySingleValue( Result, GPropAddr );
		unguardSlow;
	}

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_ArrayElement, execArrayElement );

void UObject::execDebugInfo( FFrame& Stack, RESULT_DECL ) //DEBUGGER
{
	guardSlow(UObject::execDebugInfo);
	INT GVER		= Stack.ReadInt();
	
	// Correct version?
	if ( GVER != 100 )
	{
		Stack.Code -= sizeof(INT);
		Stack.Code--;
		return;
	}
	
	//scion jg -- Added printing of line number support for accessed nones
	GLineNumber		= Stack.ReadInt();
	INT InputPos	= Stack.ReadInt();
	BYTE OpCode		= *Stack.Code++;

#ifndef _XBOX
	// Only valid when the debugger is running.
	if ( GDebugger != NULL )
		GDebugger->DebugInfo( this, &Stack, OpCode, GLineNumber, InputPos );
#endif

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DebugInfo, execDebugInfo );

void UObject::execDynArrayElement( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDynArrayElement);

	// Get array index expression.
	INT Index=0;
	Stack.Step( Stack.Object, &Index );

	GProperty = NULL;
	Stack.Step( this, NULL );
	GPropObject = this;

	checkSlow(GProperty);
	checkSlow(GProperty->IsA(UArrayProperty::StaticClass()));

	// Add scaled offset to base pointer.
	if( GProperty && GPropAddr )
	{
		FArray* Array=(FArray*)GPropAddr;
		UArrayProperty* ArrayProp = (UArrayProperty*)GProperty;
		if( Index>=Array->Num() || Index<0 )
		{
			//if we are returning a value, check for out-of-bounds
			if ( Result || Index<0 )
			{
				Stack.Logf( TEXT("Accessed array '%s' out of bounds (%i/%i)"), ArrayProp->GetName(), Index, Array->Num() );
				GPropAddr = 0;
				GPropObject = NULL;
				if (Result) appMemzero( Result, ArrayProp->Inner->ElementSize );
				return;
			}
			//if we are setting a value, allow the array to be resized
			else
			{
				Array->AddZeroed(ArrayProp->Inner->ElementSize,Index-Array->Num()+1);
			}
		}

		GPropAddr = (BYTE*)Array->GetData() + Index * ArrayProp->Inner->ElementSize;

		// Add scaled offset to base pointer.
		if( Result )
		{
			ArrayProp->Inner->CopySingleValue( Result, GPropAddr );
		}
	}

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DynArrayElement, execDynArrayElement );

void UObject::execDynArrayLength( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDynArrayLength);

	GProperty = NULL;
	Stack.Step( this, NULL );
	GPropObject = this;

	if (GPropAddr)
	{
		FArray* Array=(FArray*)GPropAddr;
		if ( !Result )
			GRuntimeUCFlags |= RUC_ArrayLengthSet; //so that EX_Let knows that this is a length 'set'-ting
		else
			*(INT*)Result = Array->Num();
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DynArrayLength, execDynArrayLength );

void UObject::execDynArrayInsert( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDynArrayInsert);

	GPropObject = this;
	GProperty = NULL;
	Stack.Step( this, NULL );
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>(GProperty);
	FArray* Array=(FArray*)GPropAddr;

	P_GET_INT(Index);
	P_GET_INT(Count);
	if (Array && Count)
	{
		if ( Count < 0 )
		{
			Stack.Logf( TEXT("Attempt to insert a negative number of elements") );
			return;
		}
		if ( Index < 0 || Index > Array->Num() )
		{
			Stack.Logf( TEXT("Attempt to insert %i elements at %i an %i-element array"), Count, Index, Array->Num() );
			Index = Clamp(Index, 0,Array->Num());
		}
		Array->InsertZeroed( Index, Count, ArrayProperty->Inner->ElementSize);
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DynArrayInsert, execDynArrayInsert );

void UObject::execDynArrayRemove( FFrame& Stack, RESULT_DECL )
{	
	guardSlow(UObject::execDynArrayRemove);

	GProperty = NULL;
	GPropObject = this;
	Stack.Step( this, NULL );
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>(GProperty);
	FArray* Array=(FArray*)GPropAddr;

	P_GET_INT(Index);
	P_GET_INT(Count);
	if (Array && Count)
	{
		if ( Count < 0 )
		{
			Stack.Logf( TEXT("Attempt to remove a negative number of elements") );
			return;
		}
		if ( Index < 0 || Index >= Array->Num() || Index + Count > Array->Num() )
		{
			if (Count == 1)
				Stack.Logf( TEXT("Attempt to remove element %i in an %i-element array"), Index, Array->Num() );
			else
				Stack.Logf( TEXT("Attempt to remove elements %i through %i in an %i-element array"), Index, Index+Count-1, Array->Num() );
			Index = Clamp(Index, 0,Array->Num());
			if ( Index + Count > Array->Num() )
				Count = Array->Num() - Index;
		}

		for (INT i=Index+Count-1; i>=Index; i--)
			ArrayProperty->Inner->DestroyValue((BYTE*)Array->GetData() + ArrayProperty->Inner->ElementSize*i);
		Array->Remove( Index, Count, ArrayProperty->Inner->ElementSize);
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DynArrayRemove, execDynArrayRemove );

//scion rwd - array shuffle
void UObject::execDynArrayShuffle(FFrame &Stack, RESULT_DECL)
{
	// grab the array we're manipulating
	GProperty = NULL;
	GPropObject = this;
	Stack.Step(this,NULL);
	FArray *Array = (FArray*)GPropAddr;
	// grab the array property as well, for the element size
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>(GProperty);
	// if we have a valid array, property, etc...
	if (Array != NULL &&
		ArrayProperty != NULL &&
		Array->Num() > 0)
	{
		// figure out the element size
		INT ElementSize = ArrayProperty->Inner->ElementSize;
		// and start swapping elements randomnly
		for (INT i = 0; i < Array->Num(); i++)
		{
			INT NewIdx = i + (appRand()%((Array->Num())-i));
			Array->SwapElements(i,NewIdx,ElementSize);
		}
	}
}
IMPLEMENT_FUNCTION(UObject,EX_DynArrayShuffle,execDynArrayShuffle);

//epic moelfke: append an array to another array of the same type
void UObject::execDynArrayAppendArray(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UObject::execDynArrayAppendArray);

	// get the array to append on to
	GProperty = NULL;
	GPropObject = this;
	Stack.Step(this, NULL);
	FArray* BaseArray = (FArray*)GPropAddr;
	// get the array property
	checkSlow(GProperty);
	checkSlow(GProperty->IsA(UArrayProperty::StaticClass()));
	UArrayProperty* ArrayProperty = (UArrayProperty*)GProperty;
	// get the array we're appending
	GProperty = NULL;
	Stack.Step(this, NULL);
	FArray* ArrayToAppend = (FArray*)GPropAddr;

	// do the append
	if (BaseArray && ArrayToAppend && ArrayToAppend->Num())
	{
		INT StartingIndex = BaseArray->Num();
		INT Size = ArrayProperty->Inner->ElementSize;
		if (ArrayProperty->Inner->PropertyFlags & CPF_NeedCtorLink)
		{
			// have to do it the slow way
			BaseArray->AddZeroed(Size, ArrayToAppend->Num());
			BYTE* BaseData  = (BYTE*)BaseArray->GetData();
			BYTE* AppendData = (BYTE*)ArrayToAppend->GetData();
			for (INT i = 0; i < ArrayToAppend->Num(); i++)
			{
				ArrayProperty->Inner->CopyCompleteValue(BaseData + (i + StartingIndex) * Size, AppendData + i * Size);
			}
		}
		else
		{
			// nothing needs a construction so we can just copy the data directly
			BaseArray->Add(ArrayToAppend->Num(), Size);
			appMemcpy((BYTE*)BaseArray->GetData() + StartingIndex * Size, ArrayToAppend->GetData(), ArrayToAppend->Num() * Size);
		}
	}

	unguardSlow;
}
IMPLEMENT_FUNCTION(UObject, EX_DynArrayAppendArray, execDynArrayAppendArray);

void UObject::execBoolVariable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execBoolVariable);

	// Get bool variable.
	BYTE B = *Stack.Code++;
	UBoolProperty* Property;
#if SERIAL_POINTER_INDEX
	Property = (UBoolProperty*) UObject::SerialIndexPointer( *(INT*)Stack.Code );
#else
  #if __PSX2_EE__ || __GCN__
	appMemcpy(&Property, Stack.Code, sizeof(INT));
  #else
	Property = *(UBoolProperty**)Stack.Code;
  #endif
#endif

	(this->*GNatives[B])( Stack, NULL );
	GProperty = Property;
	GPropObject = this;

	// Note that we're not returning an in-place pointer to to the bool, so EX_Let
	// must take special precautions with bools.
	if( Result )
		*(BITFIELD*)Result = (GPropAddr && (*(BITFIELD*)GPropAddr & ((UBoolProperty*)GProperty)->BitMask)) ? 1 : 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_BoolVariable, execBoolVariable );

void UObject::execStructMember( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStructMember);

	// Get structure element.
	UProperty* Property = (UProperty*)Stack.ReadObject();

	// Get struct expression.
	UStruct* Struct = CastChecked<UStruct>(Property->GetOuter());
	BYTE* Buffer = (BYTE*)appAlloca(Struct->PropertiesSize);
	appMemzero( Buffer, Struct->PropertiesSize );
	GPropAddr = NULL;
	Stack.Step( this, Buffer );

	// Set result.
	GProperty = Property;
	GPropObject = this;
	if( GPropAddr )
		GPropAddr += Property->Offset;
	guardSlow(StructMemberCopyComplete);
	if( Result )
		Property->CopyCompleteValue( Result, Buffer+Property->Offset );
	unguardSlow;
	guardSlow(StructMemberDestroyValue);
	for( UProperty* P=Struct->ConstructorLink; P; P=P->ConstructorLinkNext )
		P->DestroyValue( Buffer + P->Offset );
	unguardSlow;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_StructMember, execStructMember );

/////////////
// Nothing //
/////////////

void UObject::execNothing( FFrame& Stack, RESULT_DECL )
{
	// Do nothing.
}
IMPLEMENT_FUNCTION( UObject, EX_Nothing, execNothing );

void UObject::execNativeParm( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNativeParm);

	UProperty* Property = (UProperty*)Stack.ReadObject();
	if( Result )
	{
		GPropAddr = Stack.Locals + Property->Offset;
		GPropObject = NULL;
		Property->CopyCompleteValue( Result, Stack.Locals + Property->Offset );
	}

	unguardSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_NativeParm, execNativeParm );

void UObject::execEndFunctionParms( FFrame& Stack, RESULT_DECL )
{
	// For skipping over optional function parms without values specified.
	GPropObject = NULL;
	Stack.Code--;
}
IMPLEMENT_FUNCTION( UObject, EX_EndFunctionParms, execEndFunctionParms );

//////////////
// Commands //
//////////////

void UObject::execStop( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStop);
	Stack.Code = NULL;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Stop, execStop );

//!!warning: Does not support UProperty's fully, will break
// when TArray's are supported in UnrealScript!
void UObject::execSwitch( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSwitch);

	// Get switch size.
	BYTE bSize = *Stack.Code++;

	// Get switch expression.
	BYTE SwitchBuffer[1024], Buffer[1024];
	appMemzero( Buffer,       sizeof(FString) );
	appMemzero( SwitchBuffer, sizeof(FString) );
	Stack.Step( Stack.Object, SwitchBuffer );

	// Check each case clause till we find a match.
	for( ; ; )
	{
		// Skip over case token.
		checkSlow(*Stack.Code==EX_Case);
		Stack.Code++;

		// Get address of next handler.
		INT wNext = Stack.ReadWord();
		if( wNext == MAXWORD ) // Default case or end of cases.
			break;

		// Get case expression.
		Stack.Step( Stack.Object, Buffer );

		// Compare.
#if PLATFORM_64BITS
		// M13: the compiled size is the 32-bit one (4 for an object); compare at least a pointer's width. Both
		// buffers are zeroed past it above, so smaller values compare as before (engine-ut99 253aaf2).
		if( bSize ? (appMemcmp(SwitchBuffer,Buffer,Max<INT>(bSize,sizeof(UObject*)))==0) : (*(FString*)SwitchBuffer==*(FString*)Buffer) )
#else
		if( bSize ? (appMemcmp(SwitchBuffer,Buffer,bSize)==0) : (*(FString*)SwitchBuffer==*(FString*)Buffer) )
#endif
			break;

		// Jump to next handler.
		Stack.Code = &Stack.Node->Script(wNext);
	}
	if( !bSize )
	{
		(*(FString*)SwitchBuffer).~FString();
		(*(FString*)Buffer      ).~FString();
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Switch, execSwitch );

void UObject::execCase( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execCase);
	INT wNext = Stack.ReadWord();
	if( wNext != MAXWORD )
	{
		// Skip expression.
		BYTE Buffer[1024];
		appMemzero( Buffer, sizeof(FString) );
		Stack.Step( Stack.Object, Buffer );
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Case, execCase );

void UObject::execJump( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execJump);
	CHECK_RUNAWAY;

	// Jump immediate.
	INT Offset = Stack.ReadWord();
	Stack.Code = &Stack.Node->Script(Offset);
//	Stack.Code = &Stack.Node->Script(Stack.ReadWord() );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Jump, execJump );

void UObject::execJumpIfNot( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execJumpIfNot);
	CHECK_RUNAWAY;

	// Get code offset.
	INT wOffset = Stack.ReadWord();

	// Get boolean test value.
	UBOOL Value=0;
	Stack.Step( Stack.Object, &Value );

	// Jump if false.
	if( !Value )
		Stack.Code = &Stack.Node->Script( wOffset );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_JumpIfNot, execJumpIfNot );

void UObject::execAssert( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAssert);

	// Get line number.
	INT wLine = Stack.ReadWord();

	// Get boolean assert value.
	DWORD Value=0;
	Stack.Step( Stack.Object, &Value );

	// Check it.
	if( !Value )
	{
#ifndef _XBOX
		if ( !GDebugger || !GDebugger->NotifyAssertionFailed( wLine ) )
#endif
		{
			Stack.Logf( NAME_Critical, TEXT("Assertion failed, line %i"), wLine );
		}
	}

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Assert, execAssert );

void UObject::execGotoLabel( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGotoLabel);

	P_GET_NAME(N);
	if( !GotoLabel( N ) )
		Stack.Logf( TEXT("GotoLabel (%s): Label not found"), *N );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_GotoLabel, execGotoLabel );

////////////////
// Assignment //
////////////////

void UObject::execLet( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLet);
	checkSlow(!IsA(UBoolProperty::StaticClass()));

	// Get variable address.
	GPropAddr = NULL;
	Stack.Step( Stack.Object, NULL ); // Evaluate variable.
	if( !GPropAddr )
	{
		//scion jg -- Added line number reporting
		if (GLineNumber != -1)
		{
			Stack.Logf( NAME_ScriptWarning, TEXT("Attempt to assign variable through None on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf( NAME_ScriptWarning, TEXT("Attempt to assign variable through None") );
		}
		static BYTE Crud[1024];//!!temp
		GPropAddr = Crud;
		appMemzero( GPropAddr, sizeof(FString) );
	}
	else if ( GPropObject && GProperty && (GProperty->PropertyFlags & CPF_Net) )
		GPropObject->NetDirty(GProperty); //FIXME - use object property instead for performance

	if (GRuntimeUCFlags & RUC_ArrayLengthSet)
	{
		GRuntimeUCFlags &= ~RUC_ArrayLengthSet;
		FArray* Array=(FArray*)GPropAddr;
		UArrayProperty* ArrayProp = (UArrayProperty*)GProperty;
		INT NewSize = 0;
		Stack.Step( Stack.Object, &NewSize); // Evaluate expression into variable.
		if (NewSize > Array->Num())
		{
			Array->AddZeroed(ArrayProp->Inner->ElementSize, NewSize-Array->Num());
		}
		else if (NewSize < Array->Num())
		{
			for (INT i=Array->Num()-1; i>=NewSize; i--)
				ArrayProp->Inner->DestroyValue((BYTE*)Array->GetData() + ArrayProp->Inner->ElementSize*i);
			Array->Remove(NewSize, Array->Num()-NewSize, ArrayProp->Inner->ElementSize );
		}
	} else
#if PLATFORM_64BITS
	{
		// M13: `X = None.Y` zero-fills X with the compiler's byte count, the value's size in the 32-bit
		// packages (4 for any object, 32-bit struct sizes). Where X is bigger in memory (an 8-byte pointer, a
		// struct holding one) the rest kept its old bytes: the high half of the previous pointer. When the
		// fill landed exactly on this variable and fell short, zero all of it (ufront 2.83, engine-ut99 253aaf2).
		UProperty* DestProperty = GProperty;
		BYTE* Dest = GPropAddr;
		GNullContextResult = NULL;
		Stack.Step( Stack.Object, GPropAddr ); // Evaluate expression into variable.
		if( GNullContextResult==Dest && DestProperty && GNullContextSize>0 && GNullContextSize<DestProperty->ElementSize )
			appMemzero( Dest, DestProperty->ElementSize );
		GNullContextResult = NULL;
	}
#else
		Stack.Step( Stack.Object, GPropAddr ); // Evaluate expression into variable.
#endif
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Let, execLet );

void UObject::execLetBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLetBool);

	// Get variable address.
	GPropAddr = NULL;
	GProperty = NULL;
	GPropObject = NULL;
	Stack.Step( Stack.Object, NULL ); // Variable.
	BITFIELD*      BoolAddr     = (BITFIELD*)GPropAddr;
	UBoolProperty* BoolProperty = (UBoolProperty*)GProperty;
	INT Value=0;  // must be INT...execTrue/execFalse return 32 bits. --ryan.
	if ( GPropObject && GProperty && (GProperty->PropertyFlags & CPF_Net) )
		GPropObject->NetDirty(GProperty);
	Stack.Step( Stack.Object, &Value );
	if( BoolAddr )
	{
		checkSlow(BoolProperty->IsA(UBoolProperty::StaticClass()));
		if( Value ) *BoolAddr |=  BoolProperty->BitMask;
		else        *BoolAddr &= ~BoolProperty->BitMask;
	}

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_LetBool, execLetBool );

/////////////////////////
// Context expressions //
/////////////////////////

void UObject::execSelf( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSelf);

	// Get Self actor for this context.
	*(UObject**)Result = this;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Self, execSelf );

void UObject::execContext( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execContext);

	// Get actor variable.
	UObject* NewContext=NULL;
	Stack.Step( this, &NewContext );

	// Execute or skip the following expression in the actor's context.
	if( NewContext != NULL )
	{
		Stack.Code += 3;
		Stack.Step( NewContext, Result );
	}
	else
	{
		//scion jg -- Added for reporting of line number information
		if (GLineNumber != -1)
		{
			Stack.Logf(TEXT("Accessed None '%s' on line %d"),GProperty ? GProperty->GetName() : TEXT("Unknown"), GLineNumber);
		}
		else
		{
			// scion rwd -- what did we try to access?
			Stack.Logf( TEXT("Accessed None [%s]"), GProperty != NULL ? GProperty->GetName() : TEXT("Unknown") );
		}
		// DEBUGGER

#ifndef _XBOX
		if (GDebugger)
			GDebugger->NotifyAccessedNone();	// jmw
#endif

		INT wSkip = Stack.ReadWord();
		BYTE bSize = *Stack.Code++;
		Stack.Code += wSkip;
		GPropAddr = NULL;
		GProperty = NULL;
		GPropObject = NULL;
		if( Result )
		{
			appMemzero( Result, bSize );
#if PLATFORM_64BITS
			GNullContextResult = (BYTE*)Result;
			GNullContextSize   = bSize;
#endif
		}
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Context, execContext );

////////////////////
// Function calls //
////////////////////

void UObject::execVirtualFunction( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVirtualFunction);

	// Call the virtual function.
	CallFunction( Stack, Result, FindFunctionChecked(Stack.ReadName()) );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_VirtualFunction, execVirtualFunction );

void UObject::execFinalFunction( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFinalFunction);

	// Call the final function.
	CallFunction( Stack, Result, (UFunction*)Stack.ReadObject() );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_FinalFunction, execFinalFunction );

void UObject::execGlobalFunction( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGlobalFunction);

	// Call global version of virtual function.
	CallFunction( Stack, Result, FindFunctionChecked(Stack.ReadName(),1) );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_GlobalFunction, execGlobalFunction );

///////////////////////
// Delegates         //
///////////////////////

void UObject::execDelegateFunction( FFrame& Stack, RESULT_DECL )
{
	//guardSlow(UObject::execDelegateFunction);
	guard(UObject::execDelegateFunction);

	// Look up delegate property
	UProperty* DelegateProperty = (UProperty*)Stack.ReadObject();
	FScriptDelegate* Delegate = (FScriptDelegate*)((BYTE*)this + DelegateProperty->Offset);
	FName DelegateName = Stack.ReadName();

	UFunction* DelegateFunction = FindFunctionChecked(DelegateName);
	if( Delegate->Object && (DelegateFunction->FunctionFlags & FUNC_Delegate) )
	{
		if ( Delegate->Object->IsPendingKill() )
		{
			Delegate->Object = NULL;
			Delegate->FunctionName = NAME_None;
		}
		else
		{
			Delegate->Object->CallFunction( Stack, Result, Delegate->Object->FindFunctionChecked(Delegate->FunctionName) );
			return;
		}
	}
	
	CallFunction( Stack, Result, DelegateFunction );
	unguardexec;
	//unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DelegateFunction, execDelegateFunction );

void UObject::execFinalDelegate( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execFinalDelegate);

	// Look up delegate property
	UProperty* DelegateProperty = (UProperty*)Stack.ReadObject();
	FScriptDelegate* Delegate = (FScriptDelegate*)((BYTE*)this + DelegateProperty->Offset);
	FName DelegateName = Stack.ReadName();
	if( Delegate->Object && Delegate->Object->IsPendingKill() )
	{
		Delegate->Object = NULL;
		Delegate->FunctionName = NAME_None;
	}
	if( Delegate->Object )
	{
		Stack.Code += sizeof(INT);	// Advance past the stored function
		Delegate->Object->CallFunction( Stack, Result, Delegate->Object->FindFunctionChecked(Delegate->FunctionName) );	
	}
	else
		CallFunction( Stack, Result, (UFunction*)Stack.ReadObject() );

	unguard;
}
IMPLEMENT_FUNCTION( UObject, EX_FinalDelegate, execFinalDelegate );

void UObject::execDelegateProperty( FFrame& Stack, RESULT_DECL )
{
//	guardSlow(UObject::execDelegateProperty);
	guard(UObject::execDelegateProperty);
	FName FunctionName = Stack.ReadName();
	((FScriptDelegate*)Result)->FunctionName = FunctionName;
	if( FunctionName == NAME_None )
		((FScriptDelegate*)Result)->Object = NULL;
	else
		((FScriptDelegate*)Result)->Object = this;
	unguardexec;
//	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DelegateProperty, execDelegateProperty );

void UObject::execLetDelegate( FFrame& Stack, RESULT_DECL )
{
//	guardSlow(UObject::execLetDelegate);
	guard(UObject::execLetDelegate);

	// Get variable address.
	GPropAddr = NULL;
	GProperty = NULL;
	GPropObject = NULL;
	Stack.Step( Stack.Object, NULL ); // Variable.
	FScriptDelegate* DelegateAddr = (FScriptDelegate*)GPropAddr;
	FScriptDelegate Delegate;
	Stack.Step( Stack.Object, &Delegate );
	if( DelegateAddr )
	{
		DelegateAddr->FunctionName = Delegate.FunctionName;
		DelegateAddr->Object	   = Delegate.Object;
	}
	unguardexec;
	//unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_LetDelegate, execLetDelegate );

#if 1 //scion rwd - merged in tertiary operator support from Lucas Alonso of digitalo
void UObject::execConditional( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execConditional);
	
	// Get test expression
	UBOOL CondValue = 0;
	Stack.Step( Stack.Object, &CondValue );
	
	// Get first skip offset
	INT SkipOffset = Stack.ReadWord();
	
	// Skip ahead if false.
	if( !CondValue )
	{
		Stack.Code += SkipOffset+2;
		Stack.Step(Stack.Object, Result);
	}
	else
	{
		Stack.Step(Stack.Object, Result);
		// Get second skip offset
		SkipOffset = Stack.ReadWord();
		// Skip ahead if true
		Stack.Code += SkipOffset;
	}
	
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_Conditional, execConditional );
#endif

///////////////////////
// Struct comparison //
///////////////////////

void UObject::execStructCmpEq( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStructCmpEq);

	UStruct* Struct  = (UStruct*)Stack.ReadObject();
	BYTE*    Buffer1 = (BYTE*)appAlloca(Struct->PropertiesSize);
	BYTE*    Buffer2 = (BYTE*)appAlloca(Struct->PropertiesSize);
	appMemzero( Buffer1, Struct->PropertiesSize );
	appMemzero( Buffer2, Struct->PropertiesSize );
	Stack.Step( this, Buffer1 );
	Stack.Step( this, Buffer2 );
	*(DWORD*)Result  = Struct->StructCompare( Buffer1, Buffer2 );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_StructCmpEq, execStructCmpEq );

void UObject::execStructCmpNe( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStructCmpNe);

	UStruct* Struct = (UStruct*)Stack.ReadObject();
	BYTE*    Buffer1 = (BYTE*)appAlloca(Struct->PropertiesSize);
	BYTE*    Buffer2 = (BYTE*)appAlloca(Struct->PropertiesSize);
	appMemzero( Buffer1, Struct->PropertiesSize );
	appMemzero( Buffer2, Struct->PropertiesSize );
	Stack.Step( this, Buffer1 );
	Stack.Step( this, Buffer2 );
	*(DWORD*)Result = !Struct->StructCompare(Buffer1,Buffer2);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_StructCmpNe, execStructCmpNe );

///////////////
// Constants //
///////////////

void UObject::execIntConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntConst);
	*(INT*)Result = Stack.ReadInt();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_IntConst, execIntConst );

void UObject::execFloatConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFloatConst);
	*(FLOAT*)Result = Stack.ReadFloat();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_FloatConst, execFloatConst );

void UObject::execStringConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringConst);
	*(FString*)Result = appFromAnsi((ANSICHAR*)Stack.Code);
	while( *Stack.Code )
		Stack.Code++;
	Stack.Code++;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_StringConst, execStringConst );

void UObject::execUnicodeStringConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringConst);
	*(FString*)Result = appFromUnicode((UNICHAR*)Stack.Code);
	while( *(_WORD*)Stack.Code )
		Stack.Code+=sizeof(_WORD);
	Stack.Code+=sizeof(_WORD);
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_UnicodeStringConst, execUnicodeStringConst );

void UObject::execObjectConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execObjectConst);
	*(UObject**)Result = (UObject*)Stack.ReadObject();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_ObjectConst, execObjectConst );

void UObject::execNameConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNameConst);
	*(FName*)Result = Stack.ReadName();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_NameConst, execNameConst );

void UObject::execByteConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execByteConst);
	*(BYTE*)Result = *Stack.Code++;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_ByteConst, execByteConst );

void UObject::execIntZero( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntZero);
	*(INT*)Result = 0;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_IntZero, execIntZero );

void UObject::execIntOne( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntOne);
	*(INT*)Result = 1;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_IntOne, execIntOne );

void UObject::execTrue( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execTrue);
	*(INT*)Result = 1;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_True, execTrue );

void UObject::execFalse( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFalse);
	*(DWORD*)Result = 0;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_False, execFalse );

void UObject::execNoObject( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNoObject);
	*(UObject**)Result = NULL;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_NoObject, execNoObject );

void UObject::execIntConstByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntConstByte);
	*(INT*)Result = *Stack.Code++;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_IntConstByte, execIntConstByte );

/////////////////
// Conversions //
/////////////////

void UObject::execDynamicCast( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDynamicCast);

	// Get destination class of dynamic actor class.
	UClass* Class = (UClass *)Stack.ReadObject();

	// Compile object expression.
	UObject* Castee = NULL;
	Stack.Step( Stack.Object, &Castee );
	*(UObject**)Result = (Castee && Castee->IsA(Class)) ? Castee : NULL;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_DynamicCast, execDynamicCast );

void UObject::execMetaCast( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMetaCast);

	// Get destination class of dynamic actor class.
	UClass* MetaClass = (UClass*)Stack.ReadObject();

	// Compile actor expression.
	UObject* Castee=NULL;
	Stack.Step( Stack.Object, &Castee );
	*(UObject**)Result = (Castee && Castee->IsA(UClass::StaticClass()) && ((UClass*)Castee)->IsChildOf(MetaClass)) ? Castee : NULL;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_MetaCast, execMetaCast );

void UObject::execPrimitiveCast( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execPrimitiveCast);

	INT B = *(Stack.Code)++;
	(Stack.Object->*GCasts[B])( Stack, Result );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_PrimitiveCast, execPrimitiveCast );

void UObject::execByteToInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execByteToInt);
	BYTE B=0;
	Stack.Step( Stack.Object, &B );
	*(INT*)Result = B;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ByteToInt, execByteToInt );

void UObject::execByteToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execByteToBool);
	BYTE B=0;
	Stack.Step( Stack.Object, &B );
	*(DWORD*)Result = B ? 1 : 0;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ByteToBool, execByteToBool );

void UObject::execByteToFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execByteToFloat);
	BYTE B=0;
	Stack.Step( Stack.Object, &B );
	*(FLOAT*)Result = B;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ByteToFloat, execByteToFloat );

void UObject::execByteToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execByteToString);
	P_GET_BYTE(B);
	//scion rwd - useful enum logging
	UByteProperty *ByteProp = Cast<UByteProperty>(GProperty);
	if (ByteProp != NULL &&
		ByteProp->Enum != NULL &&
		B >= 0 && B < ByteProp->Enum->Names.Num())
	{
		*(FString*)Result = *ByteProp->Enum->Names(B);
	}
	else
	{
		*(FString*)Result = FString::Printf(TEXT("%i"),B);
	}
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ByteToString, execByteToString );

void UObject::execIntToByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntToByte);
	INT I=0;
	Stack.Step( Stack.Object, &I );
	*(BYTE*)Result = I;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_IntToByte, execIntToByte );

void UObject::execIntToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntToBool);
	INT I=0;
	Stack.Step( Stack.Object, &I );
	*(INT*)Result = I ? 1 : 0;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_IntToBool, execIntToBool );

void UObject::execIntToFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntToFloat);
	INT I=0;
	Stack.Step( Stack.Object, &I );
	*(FLOAT*)Result = I;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_IntToFloat, execIntToFloat );

void UObject::execIntToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIntToString);
	P_GET_INT(I);
	*(FString*)Result = FString::Printf(TEXT("%i"),I);
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_IntToString, execIntToString );

void UObject::execBoolToByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execBoolToByte);
	UBOOL B=0;
	Stack.Step( Stack.Object, &B );
	*(BYTE*)Result = B & 1;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_BoolToByte, execBoolToByte );

void UObject::execBoolToInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execBoolToInt);
	UBOOL B=0;
	Stack.Step( Stack.Object, &B );
	*(INT*)Result = B & 1;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_BoolToInt, execBoolToInt );

void UObject::execBoolToFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execBoolToFloat);
	UBOOL B=0;
	Stack.Step( Stack.Object, &B );
	*(FLOAT*)Result = B & 1;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_BoolToFloat, execBoolToFloat );

void UObject::execBoolToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execBoolToString);
	P_GET_UBOOL(B);
	*(FString*)Result = B ? GTrue : GFalse;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_BoolToString, execBoolToString );

void UObject::execFloatToByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFloatToByte);
	FLOAT F=0.f;
	Stack.Step( Stack.Object, &F );
	*(BYTE*)Result = (BYTE)F;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_FloatToByte, execFloatToByte );

void UObject::execFloatToInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFloatToInt);
	FLOAT F=0.f;
	Stack.Step( Stack.Object, &F );
	//scion jg -- Use SSE optimized conversion on xbox
#ifdef _XBOX
	*(INT*)Result = appTrunc(F);
#else
	*(INT*)Result = (INT)F;
#endif
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_FloatToInt, execFloatToInt );

void UObject::execFloatToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFloatToBool);
	FLOAT F=0.f;
	Stack.Step( Stack.Object, &F );
	*(DWORD*)Result = F!=0.f ? 1 : 0;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_FloatToBool, execFloatToBool );

void UObject::execFloatToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFloatToString);

	P_GET_FLOAT(F);

	//scion superville: More precision in float strings
	*(FString*)Result = FString::Printf(TEXT("%.4f"),F);

	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_FloatToString, execFloatToString );

void UObject::execObjectToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execObjectToBool);
	UObject* Obj=NULL;
	Stack.Step( Stack.Object, &Obj );
	*(DWORD*)Result = Obj!=NULL;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ObjectToBool, execObjectToBool );

void UObject::execObjectToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execObjectToString);
	P_GET_OBJECT(UObject,Obj);
	*(FString*)Result = Obj ? Obj->GetPathName() : TEXT("None");
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_ObjectToString, execObjectToString );

void UObject::execNameToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNameToBool);
	FName N=NAME_None;
	Stack.Step( Stack.Object, &N );
	*(DWORD*)Result = N!=NAME_None ? 1 : 0;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_NameToBool, execNameToBool );

void UObject::execNameToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNameToString);
	P_GET_NAME(N);
	*(FString*)Result = *N;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_NameToString, execNameToString );

void UObject::execStringToByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToByte);
	P_GET_STR(Str);
	*(BYTE*)Result = appAtoi( *Str );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToByte, execStringToByte );

void UObject::execStringToInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToInt);
	P_GET_STR(Str);
	*(INT*)Result = appAtoi( *Str );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToInt, execStringToInt );

void UObject::execStringToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToBool);
	P_GET_STR(Str);
	if( appStricmp(*Str,TEXT("True") )==0 || appStricmp(*Str,GTrue)==0 )
	{
		*(INT*)Result = 1;
	}
	else if( appStricmp(*Str,TEXT("False"))==0 || appStricmp(*Str,GFalse)==0 )
	{
		*(INT*)Result = 0;
	}
	else
	{
		*(INT*)Result = appAtoi(*Str) ? 1 : 0;
	}
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToBool, execStringToBool );

void UObject::execStringToFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToFloat);
	P_GET_STR(Str);
	*(FLOAT*)Result = appAtof( *Str );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToFloat, execStringToFloat );

/////////////////////////////////////////
// Native bool operators and functions //
/////////////////////////////////////////

void UObject::execNot_PreBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNot_PreBool);

	P_GET_UBOOL(A);
	P_FINISH;

	*(DWORD*)Result = !A;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 129, execNot_PreBool );

void UObject::execEqualEqual_BoolBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_BoolBool);

	P_GET_UBOOL(A);
	P_GET_UBOOL(B);
	P_FINISH;

	*(DWORD*)Result = ((!A) == (!B));
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 242, execEqualEqual_BoolBool );

void UObject::execNotEqual_BoolBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_BoolBool);

	P_GET_UBOOL(A);
	P_GET_UBOOL(B);
	P_FINISH;

	*(DWORD*)Result = ((!A) != (!B));

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 243, execNotEqual_BoolBool );

void UObject::execAndAnd_BoolBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAndAnd_BoolBool);

	P_GET_UBOOL(A);
	P_GET_SKIP_OFFSET(W);

	if( A )
	{
		P_GET_UBOOL(B);
		*(DWORD*)Result = A && B;
		Stack.Code++; //DEBUGGER
	}
	else
	{
		*(DWORD*)Result = 0;
		Stack.Code += W;
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 130, execAndAnd_BoolBool );

void UObject::execXorXor_BoolBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execXorXor_BoolBool);

	P_GET_UBOOL(A);
	P_GET_UBOOL(B);
	P_FINISH;

	*(DWORD*)Result = !A ^ !B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 131, execXorXor_BoolBool );

void UObject::execOrOr_BoolBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execOrOr_BoolBool);
	P_GET_UBOOL(A);
	P_GET_SKIP_OFFSET(W);
	if( !A )
	{
		P_GET_UBOOL(B);
		*(DWORD*)Result = A || B;
		Stack.Code++; //DEBUGGER
	}
	else
	{
		*(DWORD*)Result = 1;
		Stack.Code += W;
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 132, execOrOr_BoolBool );

/////////////////////////////////////////
// Native byte operators and functions //
/////////////////////////////////////////

void UObject::execMultiplyEqual_ByteByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_ByteByte);

	P_GET_BYTE_REF(A);
	P_GET_BYTE(B);
	P_FINISH;

	*(BYTE*)Result = (*A *= B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 133, execMultiplyEqual_ByteByte );

void UObject::execDivideEqual_ByteByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivideEqual_ByteByte);

	P_GET_BYTE_REF(A);
	P_GET_BYTE(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(BYTE*)Result = B ? (*A /= B) : 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 134, execDivideEqual_ByteByte );

void UObject::execAddEqual_ByteByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddEqual_ByteByte);

	P_GET_BYTE_REF(A);
	P_GET_BYTE(B);
	P_FINISH;

	*(BYTE*)Result = (*A += B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 135, execAddEqual_ByteByte );

void UObject::execSubtractEqual_ByteByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_ByteByte);

	P_GET_BYTE_REF(A);
	P_GET_BYTE(B);
	P_FINISH;

	*(BYTE*)Result = (*A -= B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 136, execSubtractEqual_ByteByte );

void UObject::execAddAdd_PreByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddAdd_PreByte);

	P_GET_BYTE_REF(A);
	P_FINISH;

	*(BYTE*)Result = ++(*A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 137, execAddAdd_PreByte );

void UObject::execSubtractSubtract_PreByte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractSubtract_PreByte);

	P_GET_BYTE_REF(A);
	P_FINISH;

	*(BYTE*)Result = --(*A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 138, execSubtractSubtract_PreByte );

void UObject::execAddAdd_Byte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddAdd_Byte);

	P_GET_BYTE_REF(A);
	P_FINISH;

	*(BYTE*)Result = (*A)++;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 139, execAddAdd_Byte );

void UObject::execSubtractSubtract_Byte( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractSubtract_Byte);

	P_GET_BYTE_REF(A);
	P_FINISH;

	*(BYTE*)Result = (*A)--;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 140, execSubtractSubtract_Byte );

/////////////////////////////////
// Int operators and functions //
/////////////////////////////////

void UObject::execComplement_PreInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execComplement_PreInt);

	P_GET_INT(A);
	P_FINISH;

	*(INT*)Result = ~A;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 141, execComplement_PreInt );

void UObject::execGreaterGreaterGreater_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterGreaterGreater_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = ((DWORD)A) >> B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 196, execGreaterGreaterGreater_IntInt );

void UObject::execSubtract_PreInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_PreInt);

	P_GET_INT(A);
	P_FINISH;

	*(INT*)Result = -A;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 143, execSubtract_PreInt );

void UObject::execMultiply_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A * B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 144, execMultiply_IntInt );

void UObject::execDivide_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivide_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(INT*)Result = B ? A / B : 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 145, execDivide_IntInt );

void UObject::execAdd_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAdd_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A + B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 146, execAdd_IntInt );

void UObject::execSubtract_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A - B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 147, execSubtract_IntInt );

void UObject::execLessLess_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLessLess_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A << B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 148, execLessLess_IntInt );

void UObject::execGreaterGreater_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterGreater_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A >> B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 149, execGreaterGreater_IntInt );

void UObject::execLess_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLess_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A < B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 150, execLess_IntInt );

void UObject::execGreater_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreater_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A > B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 151, execGreater_IntInt );

void UObject::execLessEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLessEqual_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A <= B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 152, execLessEqual_IntInt );

void UObject::execGreaterEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterEqual_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A >= B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 153, execGreaterEqual_IntInt );

void UObject::execEqualEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A == B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 154, execEqualEqual_IntInt );

void UObject::execNotEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(DWORD*)Result = A != B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 155, execNotEqual_IntInt );

void UObject::execAnd_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAnd_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A & B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 156, execAnd_IntInt );

void UObject::execXor_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execXor_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A ^ B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 157, execXor_IntInt );

void UObject::execOr_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execOr_IntInt);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = A | B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 158, execOr_IntInt );

void UObject::execMultiplyEqual_IntFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_IntInt);

	P_GET_INT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(INT*)Result = *A = (INT)(*A * B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 159, execMultiplyEqual_IntFloat );

void UObject::execDivideEqual_IntFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivideEqual_IntInt);

	P_GET_INT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(INT*)Result = *A = (INT)(B ? *A/B : 0.f);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 160, execDivideEqual_IntFloat );

void UObject::execAddEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddEqual_IntInt);

	P_GET_INT_REF(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = (*A += B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 161, execAddEqual_IntInt );

void UObject::execSubtractEqual_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_IntInt);

	P_GET_INT_REF(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = (*A -= B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 162, execSubtractEqual_IntInt );

void UObject::execAddAdd_PreInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddAdd_PreInt);

	P_GET_INT_REF(A);
	P_FINISH;

	*(INT*)Result = ++(*A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 163, execAddAdd_PreInt );

void UObject::execSubtractSubtract_PreInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractSubtract_PreInt);

	P_GET_INT_REF(A);
	P_FINISH;

	*(INT*)Result = --(*A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 164, execSubtractSubtract_PreInt );

void UObject::execAddAdd_Int( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddAdd_Int);

	P_GET_INT_REF(A);
	P_FINISH;

	*(INT*)Result = (*A)++;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 165, execAddAdd_Int );

void UObject::execSubtractSubtract_Int( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractSubtract_Int);

	P_GET_INT_REF(A);
	P_FINISH;

	*(INT*)Result = (*A)--;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 166, execSubtractSubtract_Int );

void UObject::execRand( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRand);

	P_GET_INT(A);
	P_FINISH;

	*(INT*)Result = A>0 ? (appRand() % A) : 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 167, execRand );

void UObject::execMin( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMin);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = Min(A,B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 249, execMin );

void UObject::execMax( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMax);

	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = Max(A,B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 250, execMax );

void UObject::execClamp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execClamp);

	P_GET_INT(V);
	P_GET_INT(A);
	P_GET_INT(B);
	P_FINISH;

	*(INT*)Result = Clamp(V,A,B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 251, execClamp );

///////////////////////////////////
// Float operators and functions //
///////////////////////////////////

void UObject::execSubtract_PreFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_PreFloat);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = -A;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 169, execSubtract_PreFloat );

void UObject::execMultiplyMultiply_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyMultiply_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = appPow(A,B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 170, execMultiplyMultiply_FloatFloat );

void UObject::execMultiply_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = A * B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 171, execMultiply_FloatFloat );

void UObject::execDivide_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivide_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FLOAT*)Result = B ? A / B : 0.f;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 172, execDivide_FloatFloat );

void UObject::execPercent_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execPercent_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = appFmod(A,B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 173, execPercent_FloatFloat );

void UObject::execAdd_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAdd_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = A + B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 174, execAdd_FloatFloat );

void UObject::execSubtract_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = A - B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 175, execSubtract_FloatFloat );

void UObject::execLess_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLess_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A < B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 176, execLess_FloatFloat );

void UObject::execGreater_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreater_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A > B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 177, execGreater_FloatFloat );

void UObject::execLessEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLessEqual_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A <= B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 178, execLessEqual_FloatFloat );

void UObject::execGreaterEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterEqual_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A >= B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 179, execGreaterEqual_FloatFloat );

void UObject::execEqualEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A == B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 180, execEqualEqual_FloatFloat );

void UObject::execNotEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = A != B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 181, execNotEqual_FloatFloat );

void UObject::execComplementEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execComplementEqual_FloatFloat);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(DWORD*)Result = Abs(A - B) < (1.e-4);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 210, execComplementEqual_FloatFloat );

void UObject::execMultiplyEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_FloatFloat);

	P_GET_FLOAT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = (*A *= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 182, execMultiplyEqual_FloatFloat );

void UObject::execDivideEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivideEqual_FloatFloat);

	P_GET_FLOAT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FLOAT*)Result = *A = B ? (*A / B) : 0.f;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 183, execDivideEqual_FloatFloat );

void UObject::execAddEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddEqual_FloatFloat);

	P_GET_FLOAT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = (*A += B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 184, execAddEqual_FloatFloat );

void UObject::execSubtractEqual_FloatFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_FloatFloat);

	P_GET_FLOAT_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = (*A -= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 185, execSubtractEqual_FloatFloat );

void UObject::execAbs( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAbs);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = Abs(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 186, execAbs );

void UObject::execSin( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSin);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appSin(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 187, execSin );

void UObject::execAsin( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAsin);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appAsin(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, -1, execAsin );

void UObject::execCos( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execCos);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appCos(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 188, execCos );

void UObject::execAcos( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAcos);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appAcos(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, -1, execAcos );

void UObject::execTan( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execTan);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appTan(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 189, execTan );

void UObject::execAtan( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAtan);

	P_GET_FLOAT(A);
    P_GET_FLOAT(B); //amb
	P_FINISH;

    *(FLOAT*)Result = appAtan2(A,B); //amb: changed to atan2

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 190, execAtan );

void UObject::execExp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execExp);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appExp(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 191, execExp );

void UObject::execLoge( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLoge);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appLoge(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 192, execLoge );

void UObject::execSqrt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSqrt);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = appSqrt(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 193, execSqrt );

void UObject::execSquare( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSquare);

	P_GET_FLOAT(A);
	P_FINISH;

	*(FLOAT*)Result = Square(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 194, execSquare );

void UObject::execFRand( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFRand);

	P_FINISH;

	*(FLOAT*)Result = appFrand();

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 195, execFRand );

void UObject::execFMin( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFMin);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = Min(A,B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 244, execFMin );

void UObject::execFMax( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFMax);

	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = Max(A,B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 245, execFMax );

void UObject::execFClamp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFClamp);

	P_GET_FLOAT(V);
	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = Clamp(V,A,B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 246, execFClamp );

void UObject::execLerp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLerp);

	P_GET_FLOAT(V);
	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_GET_FLOAT_OPTX(MinDelta,0.f);
	P_FINISH;

	FLOAT Delta = V*(B-A);
	//scion rwd - if a min delta was specified
	if (MinDelta != 0.f &&
		Abs(Delta) < MinDelta)
	{
		Delta = MinDelta * Delta/Abs(Delta);
	}
	*(FLOAT*)Result = A + Delta;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 247, execLerp );

void UObject::execSmerp( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSmerp);

	P_GET_FLOAT(V);
	P_GET_FLOAT(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FLOAT*)Result = A + (3.f*V*V - 2.f*V*V*V)*(B-A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 248, execSmerp );

//mvc -- exposed floor/ceil/round funcs to script, see Object.uc
void UObject::execFCeil( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFCeiling);

	P_GET_FLOAT(A);
	P_FINISH;

	*(INT*)Result = appCeil(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, -1, execFCeil );
void UObject::execFFloor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFFloor);

	P_GET_FLOAT(A);
	P_FINISH;

	*(INT*)Result = appFloor(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, -1, execFFloor );
void UObject::execFRound( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFRound);

	P_GET_FLOAT(A);
	P_FINISH;

	*(INT*)Result = appRound(A);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, -1, execFRound );

void UObject::execRotationConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRotationConst);
	((FRotator*)Result)->Pitch = Stack.ReadInt();
	((FRotator*)Result)->Yaw   = Stack.ReadInt();
	((FRotator*)Result)->Roll  = Stack.ReadInt();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_RotationConst, execRotationConst );

void UObject::execVectorConst( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVectorConst);
#if __PSX2_EE__ || __GCN__
	appMemcpy(Result, Stack.Code, sizeof(FVector));
#else
	*(FVector*)Result = *(FVector*)Stack.Code;
#endif
	Stack.Code += sizeof(FVector);
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_VectorConst, execVectorConst );

//scion rwd - added missing rng() support
void UObject::execRangeConst(FFrame& Stack,RESULT_DECL)
{
	guardSlow(UObject::execRangeConst);
	((FRange*)Result)->Min = Stack.ReadFloat();
	((FRange*)Result)->Max = Stack.ReadFloat();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION(UObject,EX_RangeConst,execRangeConst);

//scion superville - add range to string covnersion
void UObject::execRangeToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRangeToString);
	P_GET_STRUCT(FRange, R);
	*(FString*)Result = FString::Printf( TEXT("[Min=%2.2f,Max=%2.2f]"), R.Min, R.Max );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_RangeToString, execRangeToString );


//scion bcl -- color interpolation
void UObject::execColorLerp(FFrame& Stack,RESULT_DECL)
{
	guardSlow(UObject::execRangeConst);

	P_GET_FLOAT(V);
	P_GET_COLOR(A);
	P_GET_COLOR(B);
	P_FINISH;

	FColor C;
	C.Lerp(A, B, V);
	*(FColor*)Result = C;

	unguardexecSlow;
} // execColorLerp
IMPLEMENT_FUNCTION(UObject, INDEX_NONE, execColorLerp);

/////////////////
// Conversions //
/////////////////

void UObject::execStringToName( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToName);
	P_GET_STR(Str);

	*(FName*)Result = FName( *Str, FNAME_Intrinsic );

	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToName, execStringToName );

void UObject::execStringToVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToVector);
	P_GET_STR(Str);

	const TCHAR* Stream = *Str;
	FVector Value(0,0,0);
	Value.X = appAtof(Stream);
	Stream = appStrstr(Stream,TEXT(","));
	if( Stream )
	{
		Value.Y = appAtof(++Stream);
		Stream = appStrstr(Stream,TEXT(","));
		if( Stream )
			Value.Z = appAtof(++Stream);
	}
	*(FVector*)Result = Value;

	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToVector, execStringToVector );

void UObject::execStringToRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execStringToRotator);
	P_GET_STR(Str);

	const TCHAR* Stream = *Str;
	FRotator Rotation(0,0,0);
	Rotation.Pitch = appAtoi(Stream);
	Stream = appStrstr(Stream,TEXT(","));
	if( Stream )
	{
		Rotation.Yaw = appAtoi(++Stream);
		Stream = appStrstr(Stream,TEXT(","));
		if( Stream )
			Rotation.Roll = appAtoi(++Stream);
	}
	*(FRotator*)Result = Rotation;

	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_StringToRotator, execStringToRotator );

void UObject::execVectorToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVectorToBool);
	FVector V(0,0,0);
	Stack.Step( Stack.Object, &V );
	*(DWORD*)Result = V.IsZero() ? 0 : 1;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_VectorToBool, execVectorToBool );

void UObject::execVectorToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVectorToString);
	P_GET_VECTOR(V);
	*(FString*)Result = FString::Printf( TEXT("[X=%2.2f,Y=%2.2f,Z=%2.2f]"), V.X, V.Y, V.Z );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_VectorToString, execVectorToString );

void UObject::execVectorToRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVectorToRotator);
	FVector V(0,0,0);
	Stack.Step( Stack.Object, &V );
	*(FRotator*)Result = V.Rotation();
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_VectorToRotator, execVectorToRotator );

void UObject::execRotatorToBool( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRotatorToBool);
	FRotator R(0,0,0);
	Stack.Step( Stack.Object, &R );
	*(DWORD*)Result = R.IsZero() ? 0 : 1;
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_RotatorToBool, execRotatorToBool );

void UObject::execRotatorToVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRotatorToVector);
	FRotator R(0,0,0);
	Stack.Step( Stack.Object, &R );
	*(FVector*)Result = R.Vector();
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_RotatorToVector, execRotatorToVector );

void UObject::execRotatorToString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRotatorToString);
	P_GET_ROTATOR(R);
	//scion superville: Allow print of negative numbers
//	*(FString*)Result = FString::Printf( TEXT("%i,%i,%i"), R.Pitch&65535, R.Yaw&65535, R.Roll&65535 );
	*(FString*)Result = FString::Printf( TEXT("[P=%i,Y=%i,R=%i]"), R.Pitch, R.Yaw, R.Roll );
	unguardexecSlow;
}
IMPLEMENT_CAST_FUNCTION( UObject, CST_RotatorToString, execRotatorToString );

////////////////////////////////////
// Vector operators and functions //
////////////////////////////////////

void UObject::execSubtract_PreVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_PreVector);

	P_GET_VECTOR(A);
	P_FINISH;

	*(FVector*)Result = -A;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 83, execSubtract_PreVector );

void UObject::execMultiply_VectorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_VectorFloat);

	P_GET_VECTOR(A);
	P_GET_FLOAT (B);
	P_FINISH;

	*(FVector*)Result = A*B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 84, execMultiply_VectorFloat );

void UObject::execMultiply_FloatVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_FloatVector);

	P_GET_FLOAT (A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A*B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 85, execMultiply_FloatVector );

void UObject::execMultiply_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A*B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 296, execMultiply_VectorVector );

void UObject::execDivide_VectorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivide_VectorFloat);

	P_GET_VECTOR(A);
	P_GET_FLOAT (B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FVector*)Result = B ? A / B : FVector(0.f,0.f,0.f);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 86, execDivide_VectorFloat );

void UObject::execAdd_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAdd_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A+B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 87, execAdd_VectorVector );

void UObject::execSubtract_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A-B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 88, execSubtract_VectorVector );

void UObject::execLessLess_VectorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLessLess_VectorRotator);

	P_GET_VECTOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FVector*)Result = A.TransformVectorBy(GMath.UnitCoords / B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 275, execLessLess_VectorRotator );

void UObject::execGreaterGreater_VectorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterGreater_VectorRotator);

	P_GET_VECTOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FVector*)Result = A.TransformVectorBy(GMath.UnitCoords * B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 276, execGreaterGreater_VectorRotator );

void UObject::execEqualEqual_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(DWORD*)Result = A.X==B.X && A.Y==B.Y && A.Z==B.Z;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 89, execEqualEqual_VectorVector );

void UObject::execNotEqual_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(DWORD*)Result = A.X!=B.X || A.Y!=B.Y || A.Z!=B.Z;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 90, execNotEqual_VectorVector );

void UObject::execDot_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDot_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FLOAT*)Result = A|B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 91, execDot_VectorVector );

void UObject::execCross_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execCross_VectorVector);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A^B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 92, execCross_VectorVector );

void UObject::execMultiplyEqual_VectorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_VectorFloat);

	P_GET_VECTOR_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FVector*)Result = (*A *= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 93, execMultiplyEqual_VectorFloat );

void UObject::execMultiplyEqual_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_VectorVector);

	P_GET_VECTOR_REF(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = (*A *= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 297, execMultiplyEqual_VectorVector );

void UObject::execDivideEqual_VectorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivideEqual_VectorFloat);

	P_GET_VECTOR_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FVector*)Result = *A = B ? (*A / B) : FVector(0.f,0.f,0.f);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 94, execDivideEqual_VectorFloat );

void UObject::execAddEqual_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddEqual_VectorVector);

	P_GET_VECTOR_REF(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = (*A += B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 95, execAddEqual_VectorVector );

void UObject::execSubtractEqual_VectorVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_VectorVector);

	P_GET_VECTOR_REF(A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = (*A -= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 96, execSubtractEqual_VectorVector );

void UObject::execVSize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVSize);

	P_GET_VECTOR(A);
	P_FINISH;

	*(FLOAT*)Result = A.Size();

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 97, execVSize );

void UObject::execVSizeSq( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVSizeSq);

	P_GET_VECTOR(A);
	P_FINISH;

	*(FLOAT*)Result = A.SizeSquared();

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 1002, execVSizeSq );


void UObject::execNormal( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNormal);

	P_GET_VECTOR(A);
	P_FINISH;

	*(FVector*)Result = A.SafeNormal();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 0x80 + 98, execNormal );

void UObject::execInvert( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execInvert);

	P_GET_VECTOR_REF(X);
	P_GET_VECTOR_REF(Y);
	P_GET_VECTOR_REF(Z);
	P_FINISH;

	FCoords Temp = FCoords( FVector(0,0,0), *X, *Y, *Z ).Inverse();
	*X           = Temp.XAxis;
	*Y           = Temp.YAxis;
	*Z           = Temp.ZAxis;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 0x80 + 99, execInvert );

void UObject::execVRand( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execVRand);
	P_FINISH;
	*((FVector*)Result) = VRand();
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 0x80 + 124, execVRand );

void UObject::execRotRand( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRotRand);
	P_GET_UBOOL_OPTX(bRoll, 0);
	P_FINISH;

	FRotator RRot;
	RRot.Yaw = ((2 * appRand()) % 65535);
	RRot.Pitch = ((2 * appRand()) % 65535);
	if ( bRoll )
		RRot.Roll = ((2 * appRand()) % 65535);
	else
		RRot.Roll = 0;
	*((FRotator*)Result) = RRot;
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 320, execRotRand );

void UObject::execMirrorVectorByNormal( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMirrorVectorByNormal);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	B = B.SafeNormal();
	*(FVector*)Result = A - 2.f * B * (B | A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 300, execMirrorVectorByNormal );

//////////////////////////////////////
// Rotation operators and functions //
//////////////////////////////////////

void UObject::execEqualEqual_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_RotatorRotator);

	P_GET_ROTATOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(DWORD*)Result = A.Pitch==B.Pitch && A.Yaw==B.Yaw && A.Roll==B.Roll;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 14, execEqualEqual_RotatorRotator );

void UObject::execNotEqual_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_RotatorRotator);

	P_GET_ROTATOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(DWORD*)Result = A.Pitch!=B.Pitch || A.Yaw!=B.Yaw || A.Roll!=B.Roll;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 0x80 + 75, execNotEqual_RotatorRotator );

void UObject::execMultiply_RotatorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_RotatorFloat);

	P_GET_ROTATOR(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FRotator*)Result = A * B;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 287, execMultiply_RotatorFloat );

void UObject::execMultiply_FloatRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiply_FloatRotator);

	P_GET_FLOAT(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FRotator*)Result = B * A;

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 288, execMultiply_FloatRotator );

void UObject::execDivide_RotatorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivide_RotatorFloat);

	P_GET_ROTATOR(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FRotator*)Result = A * (B ? (1.f / B) : 0.f);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 289, execDivide_RotatorFloat );

void UObject::execMultiplyEqual_RotatorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMultiplyEqual_RotatorFloat);

	P_GET_ROTATOR_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	*(FRotator*)Result = (*A *= B);

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 290, execMultiplyEqual_RotatorFloat );

void UObject::execDivideEqual_RotatorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDivideEqual_RotatorFloat);

	P_GET_ROTATOR_REF(A);
	P_GET_FLOAT(B);
	P_FINISH;

	//scion jg -- Check for divide by zero
	if (B == 0)
	{
		if (GLineNumber != -1)
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero on line %d"),
				GLineNumber);
		}
		else
		{
			Stack.Logf(NAME_ScriptWarning,TEXT("Divide by zero"));
		}
	}

	*(FRotator*)Result = (*A *= (B ? (1.f / B) : 0.f));

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, 291, execDivideEqual_RotatorFloat );

void UObject::execAdd_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAdd_RotatorRotator);

	P_GET_ROTATOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FRotator*)Result = A + B;

	unguardSlow;
}
IMPLEMENT_FUNCTION( UObject, 316, execAdd_RotatorRotator );

void UObject::execSubtract_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtract_RotatorRotator);

	P_GET_ROTATOR(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FRotator*)Result = A - B;

	unguardSlow;
}
IMPLEMENT_FUNCTION( UObject, 317, execSubtract_RotatorRotator );

void UObject::execAddEqual_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAddEqual_RotatorRotator);

	P_GET_ROTATOR_REF(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FRotator*)Result = (*A += B);

	unguardSlow;
}
IMPLEMENT_FUNCTION( UObject, 318, execAddEqual_RotatorRotator );

void UObject::execSubtractEqual_RotatorRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_RotatorRotator);

	P_GET_ROTATOR_REF(A);
	P_GET_ROTATOR(B);
	P_FINISH;

	*(FRotator*)Result = (*A -= B);

	unguardSlow;
}
IMPLEMENT_FUNCTION( UObject, 319, execSubtractEqual_RotatorRotator );

void UObject::execGetAxes( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGetAxes);

	P_GET_ROTATOR(A);
	P_GET_VECTOR_REF(X);
	P_GET_VECTOR_REF(Y);
	P_GET_VECTOR_REF(Z);
	P_FINISH;

	FCoords Coords = GMath.UnitCoords / A;
	*X = Coords.XAxis;
	*Y = Coords.YAxis;
	*Z = Coords.ZAxis;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 0x80 + 101, execGetAxes );

void UObject::execGetUnAxes( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGetUnAxes);

	P_GET_ROTATOR(A);
	P_GET_VECTOR_REF(X);
	P_GET_VECTOR_REF(Y);
	P_GET_VECTOR_REF(Z);
	P_FINISH;

	FCoords Coords = GMath.UnitCoords * A;
	*X = Coords.XAxis;
	*Y = Coords.YAxis;
	*Z = Coords.ZAxis;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 0x80 + 102, execGetUnAxes );

void UObject::execOrthoRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execOrthoRotation);

	P_GET_VECTOR(X);
	P_GET_VECTOR(Y);
	P_GET_VECTOR(Z);
	P_FINISH;

	FCoords Coords( FVector(0,0,0), X, Y, Z );
	*(FRotator*)Result = Coords.OrthoRotation();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execOrthoRotation );

void UObject::execNormalize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNormalize);

	P_GET_ROTATOR(Rot);
	P_FINISH;

	//scion superville: Accessor function so that we can use normalize code in C++ without hassle
	*(FRotator*)Result = Rot.Normalize();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execNormalize );


void UObject::execDenormalize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDenormalize);

	P_GET_ROTATOR(Rot);
	P_FINISH;

	//scion superville: Accessor function so that we can use denormalize code in C++ without hassle
	*(FRotator*)Result = Rot.Denormalize();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execDenormalize );

void UObject::execClockwiseFrom_IntInt( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execClockwiseFrom_IntInt);

	P_GET_INT(IntA);
	P_GET_INT(IntB);
	P_FINISH;

	IntA = IntA & 0xFFFF;
	IntB = IntB & 0xFFFF;

	*(DWORD*)Result = ( Abs(IntA - IntB) > 32768 ) ? ( IntA < IntB ) : ( IntA > IntB );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execClockwiseFrom_IntInt );

////////////////////////////////////
// InterpCurve functions          //
////////////////////////////////////

void UObject::execInterpCurveEval( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execInterpCurveEval);

	P_GET_STRUCT(FInterpCurveInit, curve); // We use a FInterpCurveInit, because we want to initialise it before copying into it.
	P_GET_FLOAT(input);
	P_FINISH;

	*(FLOAT*)Result = curve.Eval(input);

	unguard;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execInterpCurveEval);

void UObject::execInterpCurveGetInputDomain( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execInterpCurveGetInputDomain);

	P_GET_STRUCT(FInterpCurveInit, curve);
	P_GET_FLOAT_REF(min);
	P_GET_FLOAT_REF(max);
	P_FINISH;

	if(curve.Points.Num() < 2)
	{
		*min = 0.f;
		*max = 0.f;
		return;
	}

	*min = curve.Points(0).InVal;
	*max = curve.Points(0).InVal;

	for(INT i=1; i<curve.Points.Num(); i++)
	{
		*min = Min(*min, curve.Points(i).InVal);
		*max = Max(*max, curve.Points(i).InVal);
	}

	unguard;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execInterpCurveGetInputDomain);

void UObject::execInterpCurveGetOutputRange( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execInterpCurveGetOutputRange);

	P_GET_STRUCT(FInterpCurveInit, curve);
	P_GET_FLOAT_REF(min);
	P_GET_FLOAT_REF(max);
	P_FINISH;

	if(curve.Points.Num() < 2)
	{
		*min = 0.f;
		*max = 0.f;
		return;
	}

	*min = curve.Points(0).OutVal;
	*max = curve.Points(0).OutVal;

	for(INT i=1; i<curve.Points.Num(); i++)
	{
		*min = Min(*min, curve.Points(i).OutVal);
		*max = Max(*max, curve.Points(i).OutVal);
	}

	unguard;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execInterpCurveGetOutputRange);

////////////////////////////////////
// Quaternion functions           //
////////////////////////////////////

void UObject::execQuatProduct( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatProduct);

	P_GET_STRUCT(FQuat, A);
	P_GET_STRUCT(FQuat, B);
	P_FINISH;

	*(FQuat*)Result = A * B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatProduct);

void UObject::execQuatInvert( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatInvert);

	P_GET_STRUCT(FQuat, A);
	P_FINISH;

	FQuat invA(-A.X, -A.Y, -A.Z, A.W);
	*(FQuat*)Result = invA;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatInvert);

void UObject::execQuatRotateVector( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatRotateVector);

	P_GET_STRUCT(FQuat, A);
	P_GET_VECTOR(B);
	P_FINISH;

	*(FVector*)Result = A.RotateVector(B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatRotateVector);

// Generate the 'smallest' (geodesic) rotation between these two vectors.
void UObject::execQuatFindBetween( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatFindBetween);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_FINISH;

	FVector cross = A ^ B;
	FLOAT crossMag = cross.Size();

	// If these vectors are basically parallel - just return identity quaternion (ie no rotation).
	if(crossMag < KINDA_SMALL_NUMBER)
	{
		*(FQuat*)Result = FQuat(0, 0, 0, 1);
		return;
	}

	FLOAT angle = appAsin(crossMag);

	FLOAT dot = A | B;
	if(dot < 0.f)
		angle = PI - angle;

	FLOAT sinHalfAng = appSin(0.5f * angle);
	FLOAT cosHalfAng = appCos(0.5f * angle);
	FVector axis = cross / crossMag;

	*(FQuat*)Result = FQuat(
		sinHalfAng * axis.X,
		sinHalfAng * axis.Y,
		sinHalfAng * axis.Z,
		cosHalfAng );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatFindBetween);

void UObject::execQuatFromAxisAndAngle( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatFromAxisAndAngle);

	P_GET_VECTOR(Axis);
	P_GET_FLOAT(Angle);
	P_FINISH;

	FLOAT sinHalfAng = appSin(0.5f * Angle);
	FLOAT cosHalfAng = appCos(0.5f * Angle);
	FVector normAxis = Axis.SafeNormal();

	*(FQuat*)Result = FQuat(
		sinHalfAng * normAxis.X,
		sinHalfAng * normAxis.Y,
		sinHalfAng * normAxis.Z,
		cosHalfAng );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatFromAxisAndAngle);

void UObject::execQuatFromRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatFromRotator);

	P_GET_ROTATOR(A);
	P_FINISH;

	FCoords C = GMath.UnitCoords / A;

	*(FQuat*)Result = FCoordsQuaternion(C);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatFromRotator);

void UObject::execQuatToRotator( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execQuatToRotator);

	P_GET_STRUCT(FQuat, A);
	P_FINISH;

	FCoords C = FQuaternionCoords(A);

	*(FRotator*)Result = C.OrthoRotation();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execQuatToRotator);

//epic moelfke: destroy an unused return value from a function
void UObject::execDestroyReturnValue(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UObject::execDestroyReturnValue);

	// get the return value property
	UProperty* Property = (UProperty*)Stack.ReadObject();
	// allocate a temporary buffer for the return value
	INT BufferSize = Property->ArrayDim * Property->ElementSize;
	BYTE* Buffer = (BYTE*)appAlloca(BufferSize);
	appMemzero(Buffer, BufferSize);
	// call the function, storing the unused return value in our temporary buffer
	Stack.Step(Stack.Object, Buffer);
	// destroy the return value
	Property->DestroyValue(Buffer);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION(UObject, EX_DestroyReturnValue, execDestroyReturnValue);

////////////////////////////////////
// Str operators and functions //
////////////////////////////////////

void UObject::execConcat_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execConcat_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(FString*)Result = (A+B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 112, execConcat_StringString );

void UObject::execConcatEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execConcatEqual_StringString);

	P_GET_STR_REF(A);
	P_GET_STR(B);
	P_FINISH;

	*(FString*)Result = ( *(A) += B );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 322, execConcatEqual_StringString );

void UObject::execAt_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAt_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(FString*)Result = (A+TEXT(" ")+B);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 168, execAt_StringString );

void UObject::execAtEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAtEqual_StringString);

	P_GET_STR_REF(A);
	P_GET_STR(B);
	P_FINISH;

	*(FString*)Result = (*(A) += FString::Printf(TEXT(" %s"), *B));

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 323, execAtEqual_StringString );

void UObject::execSubtractEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSubtractEqual_StringString);

	P_GET_STR_REF(A);
	P_GET_STR(B);
	P_FINISH;

	AT.Empty();
	INT pos = A->InStr(B);
	while (pos != -1)
	{
		AT += A->Left(pos);
		*A = A->Mid(pos + B.Len());
		pos = A->InStr(B);
	}

	if (AT != TEXT("")) *A = (AT + *A);
	*(FString*)Result = *A;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 324, execSubtractEqual_StringString );

void UObject::execLess_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLess_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)<0;;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 115, execLess_StringString );

void UObject::execGreater_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreater_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)>0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 116, execGreater_StringString );

void UObject::execLessEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLessEqual_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)<=0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 120, execLessEqual_StringString );

void UObject::execGreaterEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGreaterEqual_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)>=0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 121, execGreaterEqual_StringString );

void UObject::execEqualEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)==0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 122, execEqualEqual_StringString );

void UObject::execNotEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStrcmp(*A,*B)!=0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 123, execNotEqual_StringString );

void UObject::execComplementEqual_StringString( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execComplementEqual_StringString);

	P_GET_STR(A);
	P_GET_STR(B);
	P_FINISH;

	*(DWORD*)Result = appStricmp(*A,*B)==0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 124, execComplementEqual_StringString );

void UObject::execLen( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLen);

	P_GET_STR(S);
	P_FINISH;

	*(INT*)Result = S.Len();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 125, execLen );

void UObject::execReplaceSubstr( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLen);

	P_GET_STR(S);
	P_GET_STR(T);
	P_GET_STR(V);
	P_FINISH;
	*(FString*)Result = S.Replace(T, V);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execReplaceSubstr );

//epic moelfke: added Locs(), converts string to all lowercase
void UObject::execLocs(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UObject::execLocs);

	P_GET_STR(A);
	P_FINISH;

	*(FString*)Result = A.Locs();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION(UObject, INDEX_NONE, execLocs);

void UObject::execInStr( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execInStr);

	P_GET_STR(S);
	P_GET_STR(A);
	P_FINISH;
	*(INT*)Result = S.InStr(A);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 126, execInStr );

void UObject::execMid( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execMid);

	P_GET_STR(A);
	P_GET_INT(I);
	P_GET_INT_OPTX(C,65535);
	P_FINISH;

	*(FString*)Result = A.Mid(I,C);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 127, execMid );

void UObject::execLeft( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLeft);

	P_GET_STR(A);
	P_GET_INT(N);
	P_FINISH;

	*(FString*)Result = A.Left(N);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 128, execLeft );

void UObject::execRight( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execRight);

	P_GET_STR(A);
	P_GET_INT(N);
	P_FINISH;

	*(FString*)Result = A.Right(N);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 234, execRight );

void UObject::execCaps( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execCaps);

	P_GET_STR(A);
	P_FINISH;

	*(FString*)Result = A.Caps();

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 235, execCaps );

void UObject::execChr( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execChr);

	P_GET_INT(i);
	P_FINISH;

	TCHAR Temp[2];
	Temp[0] = i;
	Temp[1] = 0;
	*(FString*)Result = Temp;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 236, execChr );

void UObject::execAsc( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execAsc);

	P_GET_STR(S);
	P_FINISH;

	*(INT*)Result = **S;	

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 237, execAsc );

/////////////////////////////////////////
// Native name operators and functions //
/////////////////////////////////////////

void UObject::execEqualEqual_NameName( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_NameName);

	P_GET_NAME(A);
	P_GET_NAME(B);
	P_FINISH;

	*(DWORD*)Result = A == B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 254, execEqualEqual_NameName );

void UObject::execNotEqual_NameName( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_NameName);

	P_GET_NAME(A);
	P_GET_NAME(B);
	P_FINISH;

	*(DWORD*)Result = A != B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 255, execNotEqual_NameName );

////////////////////////////////////
// Object operators and functions //
////////////////////////////////////

void UObject::execEqualEqual_ObjectObject( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEqualEqual_ObjectObject);

	P_GET_OBJECT(UObject,A);
	P_GET_OBJECT(UObject,B);
	P_FINISH;

	*(DWORD*)Result = A == B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 114, execEqualEqual_ObjectObject );

void UObject::execNotEqual_ObjectObject( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNotEqual_ObjectObject);

	P_GET_OBJECT(UObject,A);
	P_GET_OBJECT(UObject,B);
	P_FINISH;

	*(DWORD*)Result = A != B;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 119, execNotEqual_ObjectObject );

/////////////////////////////
// Log and error functions //
/////////////////////////////

void UObject::execLog( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLog);

	P_GET_STR(S);
	P_GET_NAME_OPTX(N,NAME_ScriptLog);
	P_FINISH;

	debugf( (EName)N.GetIndex(), TEXT("%s"), *S );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 231, execLog );

void UObject::execWarn( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execWarn);

	P_GET_STR(S);
	P_FINISH;

	Stack.Logf( TEXT("%s"), *S );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 232, execWarn );

void UObject::execLocalize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execLocalize);

	P_GET_STR(SectionName);
	P_GET_STR(KeyName);
	P_GET_STR(PackageName);
	P_FINISH;

	*(FString*)Result = Localize( *SectionName, *KeyName, *PackageName );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execLocalize );

//////////////////
// High natives //
//////////////////

#define HIGH_NATIVE(n) \
void UObject::execHighNative##n( FFrame& Stack, RESULT_DECL ) \
{ \
	guardSlow(UObject::execHighNative##n); \
	BYTE B = *Stack.Code++; \
	(this->*GNatives[ n*0x100 + B ])( Stack, Result ); \
	unguardexecSlow; \
} \
IMPLEMENT_FUNCTION( UObject, 0x60 + n, execHighNative##n );

HIGH_NATIVE(0);
HIGH_NATIVE(1);
HIGH_NATIVE(2);
HIGH_NATIVE(3);
HIGH_NATIVE(4);
HIGH_NATIVE(5);
HIGH_NATIVE(6);
HIGH_NATIVE(7);
HIGH_NATIVE(8);
HIGH_NATIVE(9);
HIGH_NATIVE(10);
HIGH_NATIVE(11);
HIGH_NATIVE(12);
HIGH_NATIVE(13);
HIGH_NATIVE(14);
HIGH_NATIVE(15);
#undef HIGH_NATIVE

/* scion ======================================================================
 * UObject::execNew
 * Author: Epic/jg
 *
 * Creates a new object. New is only usable with poolable objects. They also
 * should be transient, but that is not enforced just recommended.
 *
 * input:	Outer -- The outer (package) to create the object in
 *			Name -- The new name for the object
 *			Flags -- The class flags to use
 *
 * ============================================================================
 */
void UObject::execNew( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execNew);
	// Get parameters.
	P_GET_OBJECT_OPTX(UObject,Outer,NULL);
	P_GET_STR_OPTX(Name,TEXT(""));
	P_GET_INT_OPTX(Flags,0);
	P_GET_OBJECT_OPTX(UClass,Cls,NULL);

	// Make sure the object gets a package
	if (Outer == NULL)
	{
		Outer = GetTransientPackage();
	}

#ifndef FINAL_RELEASE
	// Validate parameters.
	if( Flags & ~RF_ScriptMask )
	{
		Stack.Logf( TEXT("new: Flags %08X not allowed"), Flags & ~RF_ScriptMask );
	}
	// Do a runtime check too for release error logging
	if (Cls && ((Cls->ClassFlags & CLASS_Poolable) == 0))
	{
		debugf(NAME_Warning,
			TEXT("Use of new() with a class (%s) that is not poolable"),
			Cls->GetName());
	}
#endif
	// Construct new object.
	*(UObject**)Result = StaticConstructObject(Cls,Outer,
		Name.Len() ? FName(*Name) : NAME_None,Flags & RF_ScriptMask,NULL,&Stack);
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, EX_New, execNew );

/////////////////////////////
// Class related functions //
/////////////////////////////

void UObject::execClassIsChildOf( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execClassIsChildOf);

	P_GET_OBJECT(UClass,K);
	P_GET_OBJECT(UClass,C);
	P_FINISH;

	*(DWORD*)Result = (C && K) ? K->IsChildOf(C) : 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 258, execClassIsChildOf );

///////////////////////////////
// State and label functions //
///////////////////////////////

void UObject::execGotoState( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGotoState);

	// Get parameters.
	FName CurrentStateName = (StateFrame && StateFrame->StateNode!=Class) ? StateFrame->StateNode->GetFName() : FName(NAME_None);
	P_GET_NAME_OPTX( S, CurrentStateName );
	P_GET_NAME_OPTX( L, NAME_None );
	P_GET_UBOOL_OPTX( bReset, 0 );
	P_FINISH;

	// Go to the state.
	//scion jg -- Changed to allow the state to be reset. So you can goto the
	// state you are already in. It does not call the end state chain, but does
	// call the BeginState() method
  	EGotoState Result = GOTOSTATE_Success;
  	if( S!=CurrentStateName || bReset )
	{
  		Result = GotoState( S, bReset );
	}

	// Handle success.
	if( Result==GOTOSTATE_Success )
	{
		// Now go to the label.
		if( !GotoLabel( L==NAME_None ? FName(NAME_Begin) : L ) && L!=NAME_None )
			Stack.Logf( TEXT("GotoState (%s %s): Label not found"), *S, *L );
	}
	else if( Result==GOTOSTATE_NotFound )
	{
		// Warning.
		if( S!=NAME_None && S!=NAME_Auto )
			Stack.Logf( TEXT("GotoState (%s %s): State not found"), *S, *L );
#ifndef _XBOX
		else if ( S==NAME_None && GDebugger )
			GDebugger->DebugInfo(this, &Stack,DI_PrevStackState,0,0);
#endif
	}
	else
	{
		// Safely preempted by another GotoState.
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 113, execGotoState );

void UObject::execEnable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execEnable);

	P_GET_NAME(N);
	if( N.GetIndex()>=NAME_PROBEMIN && N.GetIndex()<NAME_PROBEMAX && StateFrame )
	{
		QWORD BaseProbeMask = (GetStateFrame()->StateNode->ProbeMask | GetClass()->ProbeMask) & GetStateFrame()->StateNode->IgnoreMask;
		GetStateFrame()->ProbeMask |= (BaseProbeMask & ((QWORD)1<<(N.GetIndex()-NAME_PROBEMIN)));
	}
	else Stack.Logf( TEXT("Enable: '%s' is not a probe function"), *N );
	P_FINISH;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 117, execEnable );

void UObject::execDisable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDiable);

	P_GET_NAME(N);
	P_FINISH;

	if( N.GetIndex()>=NAME_PROBEMIN && N.GetIndex()<NAME_PROBEMAX && StateFrame )
		GetStateFrame()->ProbeMask &= ~((QWORD)1<<(N.GetIndex()-NAME_PROBEMIN));
	else
		Stack.Logf( TEXT("Enable: '%s' is not a probe function"), *N );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 118, execDisable );

///////////////////
// Property text //
///////////////////

void UObject::execGetPropertyText( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGetPropertyText);

	P_GET_STR(PropName);
	P_FINISH;

	UProperty* Property=FindField<UProperty>( Class, *PropName );
	if( Property && (Property->GetFlags() & RF_Public) )
	{
		TCHAR Temp[1024]=TEXT("");//!!
		Property->ExportText( 0, Temp, (BYTE*)this, (BYTE*)this, PPF_Localized );
		*(FString*)Result = Temp;
	}
	else *(FString*)Result = TEXT("");

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execGetPropertyText );

void UObject::execSetPropertyText( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execSetPropertyText);

	P_GET_STR(PropName);
	P_GET_STR(PropValue);
	P_FINISH;

	UProperty* Property=FindField<UProperty>( Class, *PropName );
	if
	(	(Property)
	&&	(Property->GetFlags() & RF_Public)
	&&	!(Property->PropertyFlags & CPF_Const) )
		Property->ImportText( *PropValue, (BYTE*)this + Property->Offset, PPF_Localized );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execSetPropertyText );

/*-----------------------------------------------------------------------------
	uc2 port: script SaveConfig() / StaticSaveConfig() on PC (UC2ScriptSaveConfig.h).

	Epic: "rjp - calling SaveConfig() has mucho side-effects, so behavior should be identical on both PC
	and XBOX" and, from the line above it, "jg -- removed as it violates TCRs" (Xbox certification: no
	unprompted disk writes; the Xbox keeps settings in the profile). On PC both natives are live again when
	[Engine.Engine] ScriptSaveConfig is True (the default); False is Epic's no-op. An Xbox build (_XBOX)
	keeps the no-op whatever the ini says.

	One more rule, for the one script call on a LIVE game object (DeathMatch.AdjustSkill: SaveConfig() on the
	match's gametype instance, meant to keep GameDifficulty): an instance of a GameInfo holds the match's
	rules, which the URL options overrode (GoalScore, TimeLimit, MaxPlayers, NumBots...), so saving it writes a
	match into Gametypes.ini, where the gametype's DEFAULTS live. It is not saved (logged once). The class
	default object of a GameInfo (StaticSaveConfig) is the gametype's own defaults and is saved like any other.
-----------------------------------------------------------------------------*/

UBOOL UC2ScriptSaveConfigEnabled()
{
	static INT State = -1;
	if( State < 0 )
	{
		UBOOL Value = 1;
#ifdef _XBOX
		Value = 0;
#else
		if( GConfig )
			GConfig->GetBool( TEXT("Engine.Engine"), TEXT("ScriptSaveConfig"), Value );
#endif
		State = Value ? 1 : 0;
		debugf( NAME_Init, TEXT("Script SaveConfig: %s ([Engine.Engine] ScriptSaveConfig)"),
			State ? TEXT("on: SaveConfig() and StaticSaveConfig() write the ini") : TEXT("off: they do nothing, as on the Xbox") );
	}
	return State==1;
}

// An object that is not a class default object, of a class derived from Engine.GameInfo.
static UBOOL UC2IsGameRulesInstance( UObject* Object )
{
	if( Object->GetIndex()==INDEX_NONE )
		return 0;
	for( UClass* C=Object->GetClass(); C; C=C->GetSuperClass() )
		if( appStricmp( C->GetPathName(), TEXT("Engine.GameInfo") )==0 )
			return 1;
	return 0;
}

void UObject::execSaveConfig( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execSaveConfig);
	P_FINISH;
//#ifndef _XBOX//jg -- removed as it violates TCRs
// rjp - calling SaveConfig() has mucho side-effects, so behavior should be identical on both PC and XBOX
// uc2: PC saves (ScriptSaveConfig); the Xbox's no-op is ScriptSaveConfig=False
	if( UC2ScriptSaveConfigEnabled() )
	{
		static UBOOL Told = 0;
		if( UC2IsGameRulesInstance(this) )
		{
			if( !Told )
			{
				Told = 1;
				debugf( NAME_Log, TEXT("Script SaveConfig: %s not saved (an instance of a GameInfo holds the match's rules, set by the URL; Gametypes.ini keeps the gametype's defaults)"), GetFullName() );
			}
		}
		else
			UC2SaveConfigFromScript( this, 0 );
	}
	unguard;
}
IMPLEMENT_FUNCTION( UObject, 536, execSaveConfig);

void UObject::execStaticSaveConfig( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execStaticSaveConfig);
	P_FINISH;
//#ifndef _XBOX//jg -- removed as it violates TCRs
// rjp - calling SaveConfig() has mucho side-effects, so behavior should be identical on both PC and XBOX
// uc2: PC saves (ScriptSaveConfig); the Xbox's no-op is ScriptSaveConfig=False
	if( UC2ScriptSaveConfigEnabled() )
		UC2SaveConfigFromScript( Class->GetDefaultObject(), 1 );
	unguard;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execStaticSaveConfig);

void UObject::execResetConfig( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execResetConfig);
	P_FINISH;
//#ifndef _XBOX//jg -- removed as it violates TCRs
#if 0	// rjp - calling ResetConfig() has mucho side-effects, so behavior should be identical on both PC and XBOX
	ResetConfig(GetClass());
#endif
	unguard;
}
IMPLEMENT_FUNCTION( UObject, 543, execResetConfig);

void UObject::execGetEnum( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execGetEnum);

	P_GET_OBJECT(UObject,E);
	P_GET_INT(i);
	P_FINISH;

	*(FName*)Result = NAME_None;
	if( Cast<UEnum>(E) && i>=0 && i<Cast<UEnum>(E)->Names.Num() )
		*(FName*)Result = Cast<UEnum>(E)->Names(i);

	unguard;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execGetEnum);

#if LIN_LOAD
//jg -- Temp hack for tracking DLOs post linear load
UBOOL GLogDLOs = FALSE;
TArray<FString>* GDLOArray = NULL;
#endif

void UObject::execDynamicLoadObject( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDynamicLoadObject);

	P_GET_STR(Name);
	P_GET_OBJECT(UClass,Class);
	P_GET_UBOOL_OPTX(bMayFail,0);
	P_FINISH;

	if (Name.Len() > 0)
	{
#if LIN_LOAD && !defined(FINAL_RELEASE)
		//jg -- Log things that are DLOed so we can fix
		if (GLogDLOs && StaticFindObject(UObject::StaticClass(),ANY_PACKAGE,*Name) == NULL)
		{
			debugf(TEXT("DLO post linear load %s"),*Name );
			if (GDLOArray == NULL)
			{
				GDLOArray = NEW TArray<FString>();
			}
			const FString& AddMe = FString(FString(Class->GetName()) + FString(TEXT(" ")) + Name);
			// Don't add the same string multiple times
			if (GDLOArray->FindItemIndex(AddMe) == INDEX_NONE)
			{
				// Add to our global list so we can dump them all at once
				new(*GDLOArray)FString(AddMe);
			}
		}
#endif

		//scion rwd - tracking down bad dlo's
		UObject *Obj = StaticLoadObject( Class, NULL, *Name, NULL, LOAD_NoWarn | (bMayFail?LOAD_Quiet:0), NULL );
		if (Obj == NULL)
		{
			if (!bMayFail)
			{
				debugf(TEXT("Failed DLO Class=%s Name=%s from script function %s"), Class->GetFullName(), *Name, Stack.Node->GetFullName());
			}
		}
		*(UObject**)Result = Obj;
	}
	else
	{
		*(UObject**)Result = NULL;
	}

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execDynamicLoadObject );

void UObject::execFindObject( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execFindObject);

	P_GET_STR(Name);
	P_GET_OBJECT(UClass,Class);
	P_FINISH;

	*(UObject**)Result = StaticFindObject( Class, ANY_PACKAGE, *Name );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execFindObject );

/**
 * Faster version of DynamicLoadObject that doesn't do ResolveName and
 * returns the existing class if present
 */
void UObject::execDynamicLoadObjectFast(FFrame& Stack,RESULT_DECL)
{
	guardSlow(UObject::execDynamicLoadObjectFast);
	P_GET_STR(Package);
	P_GET_STR(Name);
	P_GET_OBJECT(UClass,Class);
	P_GET_UBOOL_OPTX(bMayFail,0);
	P_FINISH;
	// Don't try to load bogus names
	if (Name.Len() > 0 && Package.Len() > 0)
	{
		// Try the outer first
		UObject* Outer = StaticFindObjectFast(UPackage::StaticClass(),NULL,FName(*Package));

		// Now find the requested object. If the object is type UClass, then
		// we know that it isn't in a sub-package so the outer is correct. If
		// it's not a class, then most likely it's in a group so use the slower
		// search to find it
		UObject* Object = (Class == UClass::StaticClass()) ?
			// Fast search if the outer is fully known
			StaticFindObjectFast(Class,Outer,FName(*Name)) :
			// Slower search when it isn't
			StaticFindObject(Class,ANY_PACKAGE,*Name);
		if (Object == NULL)
		{
#if LIN_LOAD && !defined(FINAL_RELEASE)
			//jg -- Log things that are DLOed so we can fix
			if (GLogDLOs)
			{
				debugf(TEXT("DLO post linear load %s"),*Name );
				if (GDLOArray == NULL)
				{
					GDLOArray = NEW TArray<FString>();
				}
				const FString& AddMe = FString(FString(Class->GetName()) + FString(TEXT(" ")) + Name);
				// Don't add the same string multiple times
				if (GDLOArray->FindItemIndex(AddMe) == INDEX_NONE)
				{
					// Add to our global list so we can dump them all at once
					new(*GDLOArray)FString(AddMe);
				}
			}
#endif
			Object = StaticLoadObject(Class,Outer,
				*FString::Printf(TEXT("%s.%s"),*Package,*Name),
				NULL, LOAD_NoWarn | (bMayFail ? LOAD_Quiet : 0), NULL);
#ifndef FINAL_RELEASE
			if (Object == NULL && !bMayFail)
			{
				debugf(TEXT("Failed DLO Class=%s Name=%s from script function %s"), Class->GetFullName(), *Name, Stack.Node->GetFullName());
			}
#endif
		}
		*(UObject**)Result = Object;
	}
	else
	{
		*(UObject**)Result = NULL;
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execDynamicLoadObjectFast );

void UObject::execIsInState( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIsInState);

	P_GET_NAME(StateName);
	P_FINISH;

	if( StateFrame )
		for( UState* Test=StateFrame->StateNode; Test; Test=Test->GetSuperState() )
			if( Test->GetFName()==StateName )
				{*(DWORD*)Result=1; return;}
	*(DWORD*)Result = 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 281, execIsInState );

void UObject::execGetStateName( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execGetStateName);
	P_FINISH;
	*(FName*)Result = (StateFrame && StateFrame->StateNode) ? StateFrame->StateNode->GetFName() : FName(NAME_None);
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UObject, 284, execGetStateName );

void UObject::execIsA( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execIsA);

	P_GET_NAME(ClassName);
	P_FINISH;

	UClass* TempClass;
	for( TempClass=GetClass(); TempClass; TempClass=TempClass->GetSuperClass() )
		if( TempClass->GetFName() == ClassName )
			break;
	*(DWORD*)Result = (TempClass!=NULL);

	unguardexecSlow;
}
IMPLEMENT_FUNCTION(UObject,303,execIsA);

/*-----------------------------------------------------------------------------
	Native iterator functions.
-----------------------------------------------------------------------------*/

void UObject::execIterator( FFrame& Stack, RESULT_DECL )
{}
IMPLEMENT_FUNCTION( UObject, EX_Iterator, execIterator );

/*-----------------------------------------------------------------------------
	Native registry.
-----------------------------------------------------------------------------*/

#pragma DISABLE_OPTIMIZATION
//
// Register a native function.
// Warning: Called at startup time, before engine initialization.
//
BYTE CORE_API GRegisterNative( INT iNative, const Native& Func )
{
	static int Initialized = 0;
	if( !Initialized )
	{
		Initialized = 1;
		for( int i=0; i<ARRAY_COUNT(GNatives); i++ )
			GNatives[i] = &UObject::execUndefined;
	}
	if( iNative != INDEX_NONE )
	{
		if( iNative<0 || iNative>ARRAY_COUNT(GNatives) || GNatives[iNative]!=&UObject::execUndefined)
			GNativeDuplicate = iNative;
		GNatives[iNative] = Func;
	}
	return 0;
}
#pragma ENABLE_OPTIMIZATION

BYTE CORE_API GRegisterCast( INT CastCode, const Native& Func )
{
	static int Initialized = 0;
	if( !Initialized )
	{
		Initialized = 1;
		for( int i=0; i<ARRAY_COUNT(GCasts); i++ )
			GCasts[i] = &UObject::execUndefined;
	}
	if( CastCode != INDEX_NONE )
	{
		if( CastCode<0 || CastCode>ARRAY_COUNT(GCasts) || GCasts[CastCode]!=&UObject::execUndefined)
			GCastDuplicate = CastCode;
		GCasts[CastCode] = Func;
	}
	return 0;
}

/*-----------------------------------------------------------------------------
	Script processing function.
-----------------------------------------------------------------------------*/

#if PROFILE
/**
 * Helper class for call graph profiling. Scoped to correctly deal with multiple return
 * paths.
 */
struct FScopedScriptStats
{
	/** 
	 * Constructor, emiting function pointer to stream.
	 *
	 * @param InFunction	Function about to be called.
	 */
	FScopedScriptStats( UFunction* InFunction )
	{
		if( GScriptCallGraph )
		{
			StartCycles = appCycles();
			Function	= InFunction;
			GScriptCallGraph->EmitFunction( Function );
		}
	}

	/**
	 * Destructor, emiting cycles spent in function call.	
	 */
	~FScopedScriptStats()
	{
		if( GScriptCallGraph )
		{
			DWORD Cycles = appCycles() - StartCycles;
			GScriptCallGraph->EmitCycles( Cycles );
		}
	}

private:
	/** Function associated with callss */
	UFunction*	Function;
	/** Cycle count before function was being called. */
	DWORD		StartCycles;
};
#endif

//
// Call a function.
//
void UObject::CallFunction( FFrame& Stack, RESULT_DECL, UFunction* Function )
{
	guard(UObject::CallFunction);
#if PROFILE
	FScopedScriptStats ScriptStats( Function );
#endif
	// Found it.
	UBOOL SkipIt = 0;
	if( Function->iNative )
	{
		// Call native final function.
		(this->*Function->Func)( Stack, Result );
	}
	else if( Function->FunctionFlags & FUNC_Native )
	{
		// Call native networkable function.
		BYTE Buffer[1024];
		if( !ProcessRemoteFunction( Function, Buffer, &Stack ) )
		{
			// Call regular native function.
			(this->*Function->Func)( Stack, Result );
		}
		else
		{
			// Eat up the remaining parameters in the stream.
			SkipIt = 1;
			goto Temporary;
		}
	}
	else if( !(Function->FunctionFlags & FUNC_Defined) ) {
		//scion superville: If the function is NOT native (handled by above if clause)
		//					and NOT defined, skip it completely
		SkipIt = 1;
		goto Temporary;  //goto makes me feel dirty
	}
	else
	{
		// Make new stack frame in the current context.
		Temporary:
		
		BYTE* Frame = (BYTE*)appAlloca(Function->PropertiesSize);
		appMemzero( Frame, Function->PropertiesSize );
		// rwd - script trace
		FFrame NewStack( this, Function, 0, Frame, &Stack );
		//epic moelfke: changed how out params work here so they're really pass by ref
		//note that I left the out params being counted as part of the UFunction's PropertiesSize so some memory
		//is allocated for them that is never used (except for a special case that could be worked around, see below)
		//should be fixed sometime but that change was a bit too scary for me to mess with right now
		FOutParmRec** LastOut = &NewStack.OutParms;
		UProperty* Property;
		for (Property = (UProperty*)Function->Children; *Stack.Code != EX_EndFunctionParms; Property=(UProperty*)Property->Next )
		{
			GPropAddr = NULL;
			GPropObject = NULL;
			if (Property->PropertyFlags & CPF_OutParm)
			{
				Stack.Step(Stack.Object, NULL);
				FOutParmRec* Out = (FOutParmRec*)appAlloca(sizeof(FOutParmRec));
				// warning: GPropAddr could be NULL for optional out parameters
				// using the extra memory allocated for the out param here so there's always a valid address
				// if out params were no longer part of the function's PropertySize we'd have to use appAlloca() here
				// (probably could combine it with the appAlloca() for the FOutParmRec so there's no extra call overhead)
				Out->PropAddr = GPropAddr ? GPropAddr : Frame + Property->Offset;
				Out->Property = Property;
				if (*LastOut)
				{
					(*LastOut)->NextOutParm = Out;
					LastOut = &(*LastOut)->NextOutParm;
				}
				else
				{
					*LastOut = Out;
				}
				//epic moelfke: added support for const out parameters
				if (GPropObject && GProperty && (GProperty->PropertyFlags & CPF_Net) && !(Property->PropertyFlags & CPF_Const))
				{
					GPropObject->NetDirty(GProperty);
				}
			}
			else
			{
				Stack.Step(Stack.Object, NewStack.Locals + Property->Offset);
			}
		}
		Stack.Code++;
		// see if there were any optional out parms at the end that weren't included in the function call
		for (; Property && (Property->PropertyFlags & CPF_OptionalParm); Property = (UProperty*)Property->Next)
		{
			if (Property->PropertyFlags & CPF_OutParm)
			{
				FOutParmRec* Out = (FOutParmRec*)appAlloca(sizeof(FOutParmRec));
				Out->PropAddr = Frame + Property->Offset; // see above for the comment on this
				Out->Property = Property;
				if (*LastOut)
				{
					(*LastOut)->NextOutParm = Out;
					LastOut = &(*LastOut)->NextOutParm;
				}
				else
				{
					*LastOut = Out;
				}
			}
		}
#ifdef _DEBUG
		// set the next pointer of the last item to NULL so we'll properly assert if something goes wrong
		if (*LastOut)
		{
			(*LastOut)->NextOutParm = NULL;
		}
#endif

#ifndef FINAL_RELEASE
		//DEBUGGER
		// This is necessary until I find a better solution, otherwise,
		// when the DebugInfo gets read out of the bytecode, it'll be called
		// AFTER the function returns, which is not useful. This is one of the
		// few places I check for debug information.
		if ( *Stack.Code == EX_DebugInfo )
			Stack.Step( Stack.Object, NULL );
#endif

		// Execute the code.
		if( !SkipIt )
			ProcessInternal( NewStack, Result );

		// Destruct properties on the stack.
		//epic moelfke: don't bother to destruct non-optional out params since we know we didn't do anything with that memory
		for( UProperty* Destruct=Function->ConstructorLink; Destruct; Destruct=Destruct->ConstructorLinkNext )
		{
			if (!(Destruct->PropertyFlags & CPF_OutParm) || (Destruct->PropertyFlags & CPF_OptionalParm))
			{
				Destruct->DestroyValue(NewStack.Locals + Destruct->Offset);
			}
		}
	}
	unguardf((TEXT("function [%s]"),Function!=NULL?Function->GetFullName():TEXT("NULL")));
}

//
// Internal function call processing.
//!!might not write anything to Result if singular or proper type isn't returned.
//
void UObject::ProcessInternal( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::ProcessInternal);

	DWORD SingularFlag = ((UFunction*)Stack.Node)->FunctionFlags & FUNC_Singular;
	if
	(	!ProcessRemoteFunction( (UFunction*)Stack.Node, Stack.Locals, NULL )
	&&	IsProbing( Stack.Node->GetFName() )
	&&	!(ObjectFlags & SingularFlag) )
	{
		ObjectFlags |= SingularFlag;
		BYTE Buffer[1024];//!!hardcoded size
		appMemzero( Buffer, sizeof(FString) );//!!
#if DO_GUARD
		if( ++Recurse > RECURSE_LIMIT )
		{
#ifndef _XBOX
			if ( GDebugger && GDebugger->NotifyInfiniteLoop() )
				Recurse = 0;
			else
#endif
			{
				Stack.Logf( NAME_Critical, TEXT("Infinite script recursion (%i calls) detected"), RECURSE_LIMIT );
			}
		}
#endif

		while( *Stack.Code != EX_Return )
			Stack.Step( Stack.Object, Buffer );
		Stack.Code++;
		Stack.Step( Stack.Object, Result );

#ifndef _XBOX
		//DEBUGGER: Necessary for the call stack. Grab an optional 'PREVSTACK' debug info.
		if ( *Stack.Code == EX_DebugInfo )
			Stack.Step( Stack.Object, Result );
#endif
		
		ObjectFlags &= ~SingularFlag;
#if DO_GUARD
		--Recurse;
#endif
	}
	unguardSlow;
}

//
// Script processing functions.
//
void UObject::ProcessEvent( UFunction* Function, void* Parms, void* UnusedResult )
{
	guard(UObject::ProcessEvent);

	// Reject.
	//epic moelfke: skip undefined non-native functions
	if
	(	!GIsScriptable
	||	Function->iNative
	||  (!(Function->FunctionFlags & (FUNC_Native | FUNC_Defined))) 
	||	!IsProbing(Function->GetFName())
	||	IsPendingKill()
	||	((Function->FunctionFlags & FUNC_Native) && ProcessRemoteFunction(Function, Parms, NULL)) )
		return;
	checkSlow(Function->ParmsSize==0 || Parms!=NULL);

#if PROFILE
	{
	FScopedScriptStats ScriptStats( Function );
#endif

	// Start timer.
	// SCRIPTTIME FLOAT StartTime = GScriptCycles;
	if( ++GScriptEntryTag == 1 )
	{
		GScriptCycles -= appCycles();
	}

	// Create a new local execution stack.
	FFrame NewStack( this, Function, 0, appAlloca(Function->PropertiesSize) );
	appMemcpy( NewStack.Locals, Parms, Function->ParmsSize );
	appMemzero( NewStack.Locals+Function->ParmsSize, Function->PropertiesSize-Function->ParmsSize );

	//epic moelfke: changed how out params work here so they're really pass by ref
	//note that I left the out params being counted as part of the UFunction's PropertiesSize so
	//some memory is allocated for them that is never used
	//should be fixed sometime but that change was a bit too scary for me to mess with right now
	if (Function->FunctionFlags & FUNC_HasOutParms)
	{
		FOutParmRec** LastOut = &NewStack.OutParms;
		for (UProperty* Property = (UProperty*)Function->Children; Property && (Property->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm; Property = (UProperty*)Property->Next)
		{
			if (Property->PropertyFlags & CPF_OutParm)
			{
				FOutParmRec* Out = (FOutParmRec*)appAlloca(sizeof(FOutParmRec));
				Out->PropAddr = (BYTE*)Parms + Property->Offset;
				Out->Property = Property;
				if (*LastOut)
				{
					(*LastOut)->NextOutParm = Out;
					LastOut = &(*LastOut)->NextOutParm;
				}
				else
				{
					*LastOut = Out;
				}
			}
		}
		if (*LastOut)
		{
			(*LastOut)->NextOutParm = NULL;
		}
	}

	// Call native function or UObject::ProcessInternal.
	//epic moelfke: changed to use Parms directly here since NewStack.Locals is not copied back anymore
	(this->*Function->Func)(NewStack, (BYTE*)Parms + Function->ReturnValueOffset);

	// Destroy local variables except function parameters.!! see also UObject::ScriptConsoleExec
	//epic moelfke: also copy back constructed value parms here so the correct copy is destroyed when the event function returns
	for( UProperty* P=Function->ConstructorLink; P; P=P->ConstructorLinkNext )
	{
		if( P->Offset >= Function->ParmsSize )
		{
			P->DestroyValue( NewStack.Locals + P->Offset );
		}
		else if (!(P->PropertyFlags & CPF_OutParm))
		{
			appMemcpy((BYTE*)Parms + P->Offset, NewStack.Locals + P->Offset, P->ArrayDim * P->ElementSize);
		}
	}

	// Stop timer.
	if( --GScriptEntryTag == 0 )
	{
		GScriptCycles += appCycles();
	/* SCRIPTTIME
		FLOAT PT = (GScriptCycles - StartTime) * GSecondsPerCycle * 1000.0f;
		if ( PT > 0.1f )
			debugf(TEXT("*** %s processevent %s processing time %f"),GetName(), Function->GetName(),PT);
		else
			debugf(TEXT("    %s processevent %s processing time %f TOTAL %f"),GetName(), Function->GetName(),PT, GScriptCycles * GSecondsPerCycle * 1000.0f);
	*/
	}

#if PROFILE
	}
#endif

	unguardf(( TEXT("(%s, %s)"), GetFullName(), Function->GetFullName() ));
}

void UObject::ProcessDelegate( FName DelegateName, FScriptDelegate* Delegate, void* Parms, void* UnusedResult )
{
	guard(UObject::ProcessDelegate);

	UFunction* DelegateFunction = FindFunctionChecked(DelegateName);
	if( Delegate->Object && (DelegateFunction->FunctionFlags & FUNC_Delegate) )
	{
		if ( Delegate->Object->IsPendingKill() )
		{
			Delegate->Object = NULL;
			Delegate->FunctionName = NAME_None;
		}
		else
		{
			Delegate->Object->ProcessEvent( Delegate->Object->FindFunctionChecked(Delegate->FunctionName), Parms, UnusedResult );
			return;
		}
	}
	
	ProcessEvent(DelegateFunction, Parms, UnusedResult);
	unguard;
}

//
// Execute the state code of the object.
//
void UObject::ProcessState( FLOAT DeltaSeconds )
{}

//
// Process a remote function; returns 1 if remote, 0 if local.
//
UBOOL UObject::ProcessRemoteFunction( UFunction* Function, void* Parms, FFrame* Stack )
{
	return 0;
}


//scion superville
void UObject::execDot2D( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UObject::execDot2D);

	P_GET_VECTOR(A);
	P_GET_VECTOR(B);
	P_GET_FLOAT_REF( Lat );
	P_GET_FLOAT_REF( Vert );
	P_FINISH;

	A.Dot2D( B, *Lat, *Vert );

	unguardexecSlow;
}	
IMPLEMENT_FUNCTION( UObject, INDEX_NONE, execDot2D );

//scion rwd
void UObject::execScriptTrace(FFrame &Stack, RESULT_DECL)
{
	P_FINISH;
#ifndef _XBOX
	TArray<FFrame*>	Frames;
	FFrame *Frame = &Stack;
	while (Frame != NULL)
	{
		Frames.AddItem(Frame);
		Frame = Frame->PrevFrame;
	}
	debugf(TEXT("Script trace for %s..."),GetName());
	TCHAR Spacer[64];
	appStrcpy(Spacer,TEXT("  "));
	for (INT i = Frames.Num()-1; i >= 0; i--)
	{
		debugf(TEXT("%s - %s"),Spacer,Frames(i)->Node->GetFullName());
		appStrcat(Spacer,TEXT("  "));
	}
#endif
	debugf(TEXT("...all done"));
}
IMPLEMENT_FUNCTION(UObject, INDEX_NONE, execScriptTrace);

//scion rwd
void UObject::execGetFuncName(FFrame &Stack,RESULT_DECL)
{
    P_FINISH;
    *(FString*)Result = Stack.Node->GetName();
}
IMPLEMENT_FUNCTION(UObject, INDEX_NONE, execGetFuncName);

//scion rwd
void UObject::execCubicHermiteInterp(FFrame &Stack,RESULT_DECL)
{
	P_GET_FLOAT(T);
	P_GET_VECTOR(Pt0);
	P_GET_VECTOR(Pt1);
	P_GET_VECTOR(M0);
	P_GET_VECTOR(M1);
	P_FINISH;
	*(FVector*)Result = GMath.CubicHermiteInterp(T,Pt0,Pt1,M0,M1);
}
IMPLEMENT_FUNCTION(UObject, INDEX_NONE, execCubicHermiteInterp);

//scion rwd - delegate manipulation functions
UDelegateProperty *FindDelegateProperty(UObject *Obj,FName &PropName)
{
	if (Obj != NULL && PropName != NAME_None)
	{
		// iterate through all properties in the supplied obj
		for (UProperty* It = Obj->GetClass()->PropertyLink; It; It = It->PropertyLinkNext)
		{
			// see if this matches the name and is a delegate property
			if (It->GetFName() == PropName && It->IsA(UDelegateProperty::StaticClass()))
			{
				// return this property
				return (UDelegateProperty*)It;
			}
		}
	}
	// unable to find matching property
	return NULL;
}

void UObject::execGetDelegateInfo(FFrame &Stack,RESULT_DECL)
{
	guard(GetDelegateInfo);
	// read params off the stack
	P_GET_NAME(PropName);
	P_GET_NAME_REF(FuncName);
	P_GET_OBJECT_REF(UObject,FuncObject);
	P_FINISH;
	// build the real delegate name since they are mangled
	FName RealPropName = FName(*FString::Printf(TEXT("__%s__Delegate"),*PropName));
	// grab the property
	UDelegateProperty *Prop = FindDelegateProperty(this,RealPropName);
	// if we found the property,
	if (Prop != NULL)
	{
		// get the delegate info struct
		FScriptDelegate *Delegate = (FScriptDelegate*)((BYTE*)this + Prop->Offset);
		// get the current function name
		*FuncName =	Delegate->FunctionName;
		// and get the current function object
		*FuncObject = Delegate->Object != NULL ? Delegate->Object : this;
	}
	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execGetDelegateInfo);

void UObject::execSetDelegateInfo(FFrame &Stack,RESULT_DECL)
{
	guard(SetDelegateInfo);
	// read params off the stack
	P_GET_NAME(PropName);
	P_GET_NAME(FuncName);
	P_GET_OBJECT(UObject,FuncObject);
	P_FINISH;
	
	// make sure we have a valid object unless we're resetting the delegate to its default function
	if (FuncObject == NULL && FuncName != NAME_None)
	{
		// default to this object
		FuncObject = this;
	}
	// build the real delegate name since they are mangled
	FName RealPropName = FName(*FString::Printf(TEXT("__%s__Delegate"),*PropName));
	// grab the property
	UDelegateProperty *Prop = FindDelegateProperty(this,RealPropName);
	// if we found the prop,
	if (Prop != NULL)
	{
		// get the delegate info
		FScriptDelegate* Delegate = (FScriptDelegate*)((BYTE*)this + Prop->Offset);
		if (FuncObject)
		{
#ifndef FINAL_RELEASE
			// try to find the named function
			UFunction *Func = FuncObject->FindFunction(FuncName);
			// if we found the function
			if (Func != NULL)
#endif
			{
				// and set the new delegate
				Delegate->FunctionName = FuncName;
				Delegate->Object = FuncObject;
			}
#ifndef FINAL_RELEASE
			else
			{
				debugf(TEXT("Failed to find function %s"),*FuncName);
			}
#endif
		}
		else
		{
			// reset the delegate to the default function
			Delegate->FunctionName = NAME_None;
			Delegate->Object = NULL;
		}
	}
	else
	{
		debugf(TEXT("Failed to find delegate property %s"),*PropName);
	}

	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execSetDelegateInfo);

void UObject::execCopyObject(FFrame &Stack,RESULT_DECL)
{
	guard(UObject::execCopyObject);
	P_GET_OBJECT(UObject,srcObj);
	P_GET_OBJECT(UObject,destObj);
	P_FINISH;
	*(UBOOL*)Result = 0;
	if (srcObj != NULL && destObj != NULL)
	{
		InitProperties((BYTE*)destObj,srcObj->GetClass()->GetPropertiesSize(),srcObj->GetClass(),
					(BYTE*)srcObj,srcObj->GetClass()->GetPropertiesSize(),destObj,srcObj->GetOuter());
		*(UBOOL*)Result = 1;
	}
	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execCopyObject);

static INT Compare( FRegistryObjectInfo& RegA, FRegistryObjectInfo& RegB )
{
	return appStricmp( *RegA.Object, *RegB.Object );
}
void UObject::execGetRegistryObjects( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execGetRegistryObjects);

	P_GET_STR(MetaClassName);
	P_GET_TARRAY_REF(Classes,FString);
	P_GET_TARRAY_REF(Descriptions,FString);
	P_FINISH;

	Classes->Empty();
	Descriptions->Empty();

	if ( MetaClassName == TEXT("") )
	{
		debugf(NAME_Warning, TEXT("No metaclass specified for GetRegistryObjects!"));
		return;
	}

	UClass* MetaClass = LoadObject<UClass>(NULL,*MetaClassName,NULL,0,NULL);
	if ( MetaClass == NULL )
	{
		debugf(NAME_Warning, TEXT("Failed loading class '%s' in GetRegistryObjects()"), *MetaClassName);
		return;
	}

	TArray<FRegistryObjectInfo> Entries;
	GetRegistryObjects( Entries, UClass::StaticClass(), MetaClass, 0 );

	if ( Entries.Num() == 0 )
		return;

	if ( Entries.Num() > 1 )
		Sort( &Entries(0), Entries.Num() );

	for ( INT i = 0; i < Entries.Num(); i++ )
	{
		new(*Classes) FString(Entries(i).Object);
		new(*Descriptions) FString(Entries(i).Description);
	}

	unguardobj;
}
IMPLEMENT_FUNCTION(UObject,-1,execGetRegistryObjects);

/* epic ======================================================================
 * ::execClassIsAbstract
 * Author: moelfke
 *
 * Returns true if the specified class is abstract
 *
 * ============================================================================
 */
void UObject::execClassIsAbstract( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execClassIsAbstract);

	P_GET_OBJECT(UClass, TestClass);
	P_FINISH;

	*(UBOOL*)Result = (TestClass && (TestClass->ClassFlags & CLASS_Abstract));

	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execClassIsAbstract);

void UObject::execGetConfigValue( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execGetConfigValue);

	P_GET_STR(FileName);
	P_GET_STR(SectionName);
	P_GET_STR(PropertyName);
	P_GET_STR_REF(PropertyValue);
	P_FINISH;

	*(UBOOL*)Result = 0;
	if ( FileName == TEXT("") )
	{
		debugf(TEXT("GetConfigValue: No filename specified!"));
		return;
	}

	if ( SectionName == TEXT("") )
	{
		debugf(TEXT("GetConfigValue: No SectionName specified!"));
		return;
	}

	if ( PropertyName == TEXT("") )
	{
		debugf(TEXT("GetConfigValue: No PropertyName specified!"));
		return;
	}

	*(UBOOL*)Result = GConfig->GetString(*SectionName, *PropertyName, *PropertyValue, *FileName);
	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,INDEX_NONE,execGetConfigValue);

/* epic ======================================================================
 * ::GetSuperClass
 * Author: moelfke
 *
 * Returns the superclass of this object's class
 *
 * NOTE: When calling this on a class remember to call it as a static, i.e.
 * class'Whatever'.static.GetSuperClass(), otherwise class'Class' will be
 * returned which is probably not the desired result
 *
 * ============================================================================
 */
void UObject::execGetSuperClass(FFrame& Stack, RESULT_DECL)
{
	guard(UObject::execGetSuperClass);

	P_FINISH;

	*(UClass**)Result = GetClass()->GetSuperClass();

	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execGetSuperClass);

//scion ===========================================================
//	SCION Function: execProjectOnTo
//	Author: superville
//
//	Description: Projects on vector (X) onto another (Y) and returns
//		the projected vector without modifying either of the arguments
//
//	Last Modified: 03/06/04 (superville)
// ================================================================
void UObject::execProjectOnTo(FFrame &Stack, RESULT_DECL)
{
	P_GET_VECTOR( X );
	P_GET_VECTOR( Y );
	P_FINISH;

	*(FVector*)Result = X.ProjectOnTo( Y );
}
IMPLEMENT_FUNCTION(UObject,1500,execProjectOnTo);

//scion ===========================================================
//	SCION Function: execIsZero
//	Author: superville
//
//	Description: Returns true if all components of the vector are zero
//
//	Last Modified: 03/06/04 (superville)
// ================================================================
void UObject::execIsZero(FFrame &Stack, RESULT_DECL)
{
	P_GET_VECTOR(A);
	P_FINISH;

	*(UBOOL*)Result = (A.X == 0 && A.Y == 0 && A.Z == 0);
}
IMPLEMENT_FUNCTION(UObject,1501,execIsZero);

/* scion ======================================================================
 * UObject::execdelete
 * Author: jg
 *
 * Deletes an object that was allocated previously using new(). You may only
 * new/delete objects with the poolable flag set on them. You also cannot delete
 * actors. That's what Actor::DestroyActor() is for.
 *
 * input:	pDeleteMe -- The object to delete
 *
 * ============================================================================
 */
void UObject::execdelete(FFrame &Stack,RESULT_DECL)
{
	guard(UObject::execdelete);

	P_GET_OBJECT(UObject,pDeleteMe);
	P_FINISH;
	// Let bad callers know what's going on
	checkSlow(pDeleteMe != NULL && pDeleteMe->IsValid());
	checkSlow(pDeleteMe->GetClass()->ClassFlags & CLASS_Poolable);
#ifndef FINAL_RELEASE
	// Do a runtime check too for release error logging
	if ((pDeleteMe->GetClass()->ClassFlags & CLASS_Poolable) == 0)
	{
		debugf(TEXT("Attempt to delete a non-poolable object: %s"),
			pDeleteMe->GetFullName());
	}
#ifndef _XBOX
#if 0
	// This is an expensive search through all actors to help track down bugs
	// where actors are holding refs to deleted objects
	CheckDanglingRefs(pDeleteMe);
#endif
#endif
#endif
	// Let it clean up correctly (call destroy, etc)
	pDeleteMe->ConditionalDestroy();
	// Go ahead and delete the object, which if it is pooled will result in it
	// returning the available pool
	delete pDeleteMe;

	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execdelete);

static UBOOL bReverseSort=0;
static INT Compare( FString& StringA, FString& StringB )
{
	return appStricmp(*StringA, *StringB) * (bReverseSort ? -1 : 1);
}

void UObject::execSortStringArray( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execSortStringArray);
	
	P_GET_TARRAY_REF(StuffToSort,FString);
	P_GET_UBOOL_OPTX(bReverse,0);
	P_FINISH;

	bReverseSort = bReverse;
	Sort(&(*StuffToSort)(0), StuffToSort->Num());

	unguardexec;
}
IMPLEMENT_FUNCTION(UObject,-1,execSortStringArray);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

