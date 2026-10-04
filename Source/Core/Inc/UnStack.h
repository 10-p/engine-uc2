/*=============================================================================
	UnStack.h: UnrealScript execution stack definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

class UStruct;

// rjp -- temporary fix for bug in VisualAssistX parser
// VISUAL_ASSIST_HACK is apparently a define built into the VAX VS.NET add-on
#ifdef VISUAL_ASSIST_HACK
	#define unguardexec() unguardexec
	#define unguardexecSlow() unguardexecSlow
#endif	// VISUAL_ASSIST_HACK

/*-----------------------------------------------------------------------------
	Constants & types.
-----------------------------------------------------------------------------*/

// Sizes.
enum {MAX_STRING_CONST_SIZE		= 1024              };
enum {MAX_CONST_SIZE			= 16 *sizeof(TCHAR) };

//
// UnrealScript intrinsic return value declaration.
//
#define RESULT_DECL void*const Result


//
// guardexec mechanism for script debugging.
//
#define unguardexecSlow  unguardfSlow(( TEXT("(%s @ %s : %04X)"), Stack.Object->GetFullName(), Stack.Node->GetFullName(), Stack.Code - &Stack.Node->Script(0) ))
#define unguardexec      unguardf    (( TEXT("(%s @ %s : %04X)"), Stack.Object->GetFullName(), Stack.Node->GetFullName(), Stack.Code - &Stack.Node->Script(0) ))

//
// State flags.
//
enum EStateFlags
{
	// State flags.
	STATE_Editable		= 0x00000001,	// State should be user-selectable in UnrealEd.
	STATE_Auto			= 0x00000002,	// State is automatic (the default state).
	STATE_Simulated     = 0x00000004,   // State executes on client side.
};

//
// Function flags.
//
enum EFunctionFlags
{
	// Function flags.
	FUNC_Final			= 0x00000001,	// Function is final (prebindable, non-overridable function).
	FUNC_Defined		= 0x00000002,	// Function has been defined (not just declared).
	FUNC_Iterator		= 0x00000004,	// Function is an iterator.
	FUNC_Latent		    = 0x00000008,	// Function is a latent state function.
	FUNC_PreOperator	= 0x00000010,	// Unary operator is a prefix operator.
	FUNC_Singular       = 0x00000020,   // Function cannot be reentered.
	FUNC_Net            = 0x00000040,   // Function is network-replicated.
	FUNC_NetReliable    = 0x00000080,   // Function should be sent reliably on the network.
	FUNC_Simulated		= 0x00000100,	// Function executed on the client side.
	FUNC_Exec		    = 0x00000200,	// Executable from command line.
	FUNC_Native			= 0x00000400,	// Native function.
	FUNC_Event          = 0x00000800,   // Event function.
	FUNC_Operator       = 0x00001000,   // Operator function.
	FUNC_Static         = 0x00002000,   // Static function.
	FUNC_NoExport       = 0x00004000,   // Don't export intrinsic function to C++.
	FUNC_Const          = 0x00008000,   // Function doesn't modify this object.
	FUNC_Invariant      = 0x00010000,   // Return value is purely dependent on parameters; no state dependencies or internal state changes.
	FUNC_Public			= 0x00020000,	// Function is accessible in all classes (if overridden, parameters much remain unchanged).
	FUNC_Private		= 0x00040000,	// Function is accessible only in the class it is defined in (cannot be overriden, but function name may be reused in subclasses.  IOW: if overridden, parameters don't need to match, and Super.Func() cannot be accessed since it's private.)
	FUNC_Protected		= 0x00080000,	// Function is accessible only in the class it is defined in and subclasses (if overridden, parameters much remain unchanged).
	FUNC_Delegate		= 0x00100000,	// Function is actually a delegate.
//scion bcl - conditional compilation
	FUNC_DebugOnly		= 0x00200000,   // Function compiles in only in debug builds
    //scion rwd - mongo's multicast replication
    FUNC_Multicast      = 0x00400000,
	//epic moelfke: record if function has any out parms
	FUNC_HasOutParms	= 0x00800000,
	// Combinations of flags.
	FUNC_FuncInherit        = FUNC_Exec | FUNC_Event,
	FUNC_FuncOverrideMatch	= FUNC_Exec | FUNC_Final | FUNC_Latent | FUNC_PreOperator | FUNC_Iterator | FUNC_Static | FUNC_Public | FUNC_Protected,
	FUNC_NetFuncFlags       = FUNC_Net | FUNC_NetReliable,
};


enum ERuntimeUCFlags
{
	RUC_ArrayLengthSet		= 0x01,	//setting the length of an array, not an element of the array
};

//
// Evaluatable expression item types.
//
enum EExprToken
{
	// Variable references.
	EX_LocalVariable		= 0x00,	// A local variable.
	EX_InstanceVariable		= 0x01,	// An object variable.
	EX_DefaultVariable		= 0x02,	// Default variable for a concrete object.

	// Tokens.
	EX_Return				= 0x04,	// Return from function.
	EX_Switch				= 0x05,	// Switch.
	EX_Jump					= 0x06,	// Goto a local address in code.
	EX_JumpIfNot			= 0x07,	// Goto if not expression.
	EX_Stop					= 0x08,	// Stop executing state code.
	EX_Assert				= 0x09,	// Assertion.
	EX_Case					= 0x0A,	// Case.
	EX_Nothing				= 0x0B,	// No operation.
	EX_LabelTable			= 0x0C,	// Table of labels.
	EX_GotoLabel			= 0x0D,	// Goto a label.
	EX_DestroyReturnValue   = 0x0E, //epic moelfke: destroy an unused return value from a function
	EX_Let					= 0x0F,	// Assign an arbitrary size value to a variable.
	EX_DynArrayElement      = 0x10, // Dynamic array element.!!
	EX_New                  = 0x11, // New object allocation.
	EX_ClassContext         = 0x12, // Class default metaobject context.
	EX_MetaCast             = 0x13, // Metaclass cast.
	EX_LetBool				= 0x14, // Let boolean variable.
	EX_LineNumber			= 0x15, // Set current source code line number in stack frame.
	EX_EndFunctionParms		= 0x16,	// End of function call parameters.
	EX_Self					= 0x17,	// Self object.
	EX_Skip					= 0x18,	// Skippable expression.
	EX_Context				= 0x19,	// Call a function through an object context.
	EX_ArrayElement			= 0x1A,	// Array element.
	EX_VirtualFunction		= 0x1B,	// A function call with parameters.
	EX_FinalFunction		= 0x1C,	// A prebound function call with parameters.
	EX_IntConst				= 0x1D,	// Int constant.
	EX_FloatConst			= 0x1E,	// Floating point constant.
	EX_StringConst			= 0x1F,	// String constant.
	EX_ObjectConst		    = 0x20,	// An object constant.
	EX_NameConst			= 0x21,	// A name constant.
	EX_RotationConst		= 0x22,	// A rotation constant.
	EX_VectorConst			= 0x23,	// A vector constant.
	EX_ByteConst			= 0x24,	// A byte constant.
	EX_IntZero				= 0x25,	// Zero.
	EX_IntOne				= 0x26,	// One.
	EX_True					= 0x27,	// Bool True.
	EX_False				= 0x28,	// Bool False.
	EX_NativeParm           = 0x29, // Native function parameter offset.
	EX_NoObject				= 0x2A,	// NoObject.
	EX_IntConstByte			= 0x2C,	// Int constant that requires 1 byte.
	EX_BoolVariable			= 0x2D,	// A bool variable which requires a bitmask.
	EX_DynamicCast			= 0x2E,	// Safe dynamic class casting.
	EX_Iterator             = 0x2F, // Begin an iterator operation.
	EX_IteratorPop          = 0x30, // Pop an iterator level.
	EX_IteratorNext         = 0x31, // Go to next iteration.
	EX_StructCmpEq          = 0x32,	// Struct binary compare-for-equal.
	EX_StructCmpNe          = 0x33,	// Struct binary compare-for-unequal.
	EX_UnicodeStringConst   = 0x34, // Unicode string constant.
	EX_RangeConst			= 0x35,	// A range constant.
	EX_StructMember         = 0x36, // Struct member.
	EX_DynArrayLength		= 0x37,	// A dynamic array length for setting/getting
	EX_GlobalFunction		= 0x38, // Call non-state version of a function.
	EX_PrimitiveCast		= 0x39,	// A casting operator for primitives which reads the type as the subsequent byte
	EX_DynArrayInsert		= 0x40,	// Inserts into a dynamic array
	EX_DynArrayRemove		= 0x41,	// Removes from a dynamic array
	EX_DebugInfo			= 0x42,	//DEBUGGER Debug information
	EX_DelegateFunction		= 0x43, // Call to a delegate function
	EX_DelegateProperty		= 0x44, // Delegate expression
	EX_LetDelegate			= 0x45, // Assignment to a delegate

	EX_DynArrayShuffle		= 0x46, //scion rwd - array shuffle

#if 1 //scion rwd - merged in tertiary operator support from Lucas Alonso of digitalo
	EX_Conditional			= 0x47,
#endif

	EX_FinalDelegate		= 0x48,
	//epic moelfke: special bytecode for out params so they can be seperated to really be pass by ref
	EX_LocalOutVariable		= 0x49,
	//epic moelfke: append an array to another array of the same type
	EX_DynArrayAppendArray	= 0x50,

	// Natives.
	EX_ExtendedNative		= 0x60,
	EX_FirstNative			= 0x70,
	EX_Max					= 0x1000,
};


enum ECastToken
{
	CST_RotatorToVector		= 0x39,
	CST_ByteToInt			= 0x3A,
	CST_ByteToBool			= 0x3B,
	CST_ByteToFloat			= 0x3C,
	CST_IntToByte			= 0x3D,
	CST_IntToBool			= 0x3E,
	CST_IntToFloat			= 0x3F,
	CST_BoolToByte			= 0x40,
	CST_BoolToInt			= 0x41,
	CST_BoolToFloat			= 0x42,
	CST_FloatToByte			= 0x43,
	CST_FloatToInt			= 0x44,
	CST_FloatToBool			= 0x45,
	//
	CST_ObjectToBool		= 0x47,
	CST_NameToBool			= 0x48,
	CST_StringToByte		= 0x49,
	CST_StringToInt			= 0x4A,
	CST_StringToBool		= 0x4B,
	CST_StringToFloat		= 0x4C,
	CST_StringToVector		= 0x4D,
	CST_StringToRotator		= 0x4E,
	CST_VectorToBool		= 0x4F,
	CST_VectorToRotator		= 0x50,
	CST_RotatorToBool		= 0x51,
	CST_ByteToString		= 0x52,
	CST_IntToString			= 0x53,
	CST_BoolToString		= 0x54,
	CST_FloatToString		= 0x55,
	CST_ObjectToString		= 0x56,
	CST_NameToString		= 0x57,
	CST_VectorToString		= 0x58,
	CST_RotatorToString		= 0x59,
	CST_StringToName		= 0x5A,
	CST_RangeToString		= 0x5B,
	CST_Max					= 0xFF,
};


//
// Latent functions.
//
enum EPollSlowFuncs
{
	EPOLL_Sleep			      = 384,
	EPOLL_FinishAnim	      = 385,
	EPOLL_FinishInterpolation = 302,
};

/*-----------------------------------------------------------------------------
	Execution stack helpers.
-----------------------------------------------------------------------------*/

//
// Information remembered about an Out parameter.
//
struct FOutParmRec
{
	UProperty* Property;
	BYTE*      PropAddr;
	//epic moelfke: made this a linked list instead of a hardcoded static array in UObject::CallFunction()
	FOutParmRec* NextOutParm;
};

//
// Information about script execution at one stack level.
//
struct CORE_API FFrame : public FOutputDevice
{	
	// Variables.
	UStruct*	Node;
	UObject*	Object;
	BYTE*		Code;
	BYTE*		Locals;
#ifndef _XBOX
	INT			LineNum;

	// rwd - script trace
	FFrame *PrevFrame;
#endif
	//epic moelfke: seperate out params so they can really be pass by ref
	FOutParmRec* OutParms;

	// Constructors.
	FFrame( UObject* InObject );
	FFrame( UObject* InObject, UStruct* InNode, INT CodeOffset, void* InLocals, FFrame *InPrevFrame = NULL ); // rwd - script trace

	// Functions.
	void Step( UObject* Context, RESULT_DECL );
	void Serialize( const TCHAR* V, enum EName Event );
	INT ReadInt();
	UObject* ReadObject();
	FLOAT ReadFloat();
	INT ReadWord();
	FName ReadName();
	void MaybeDumpTraceInfo( const UObject* Context, INT B ) const;
	FName ReadNamePassive() const;
	UObject* ReadObjectPassive() const;
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif

//
// Information about script execution at the main stack level.
// This part of an actor's script state is saveable at any time.
//
struct CORE_API FStateFrame : public FFrame
{
	// Variables.
	UState* StateNode;
	QWORD   ProbeMask;
	_WORD	LatentAction;

	// Functions.
	FStateFrame( UObject* InObject );
	const TCHAR* Describe();
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif

/*-----------------------------------------------------------------------------
	Script execution helpers.
-----------------------------------------------------------------------------*/

//
// Base class for UnrealScript iterator lists.
//
struct FIteratorList
{
	FIteratorList* Next;
	FIteratorList() {}
	FIteratorList( FIteratorList* InNext ) : Next( InNext ) {}
	FIteratorList* GetNext() { return (FIteratorList*)Next; }
};

/*-----------------------------------------------------------------------------
	Call graph profiling.
-----------------------------------------------------------------------------*/

#if PROFILE
/**
 * A small subset of TArray<DWORD>. This is required as we can't have 16 MByte
 * TArray's with the default memory allocator (FMallocDL). The code assumes
 * AddZeroed only gets called once right after Empty to initialize the array.
 */
struct FLargeDWORDArray
{
	/**
	 * Construtor, initializing member variables.	
	 */
	FLargeDWORDArray()
	:	ArrayNum(0),
		ArrayMax(0),
		Data(NULL)
	{}

	/**
	 * Destructor, freeing allocated memory.	
	 */
	~FLargeDWORDArray()
	{
		free( Data );
	}

	/** 
	 * Resets array though does not free memory as AddZeroed is going to be
	 * called right afterwards and we don't implement Slack.
	 *
	 * @param	Slack	unused
	 */
	void Empty( DWORD Slack = 0 )
	{
		ArrayNum = 0;
		ArrayMax = 0;
	}

	/**
	 * Adds Count zero initialized items to the array.	
	 *
	 * @param Count	number of items to extend array by.
	 */
	void AddZeroed( DWORD Count )
	{
		ArrayMax = Count;
		Data = (DWORD*) realloc( Data, ArrayMax * sizeof(DWORD) );
		appMemzero( Data, ArrayMax * sizeof(DWORD) );
	}
	
	/**
	 * Returns the number of items in array.
	 *
	 * @return number of items in array
	 */
	inline DWORD Num()
	{
		return ArrayNum;
	}

	/**
	 * Adds a single array item and reallocates array if necessary.
	 *
	 * @param Item	Item to add
	 */
	inline void AddItem( const DWORD Item )
	{
		if( ArrayNum == ArrayMax )
		{
			ArrayMax = ArrayMax * 3 / 2 + 8;
			Data = (DWORD*) realloc( Data, ArrayMax * sizeof(DWORD) );
		}
		Data[ArrayNum++] = Item;
	}

	/**
	 * Array accessor operator 
	 *
	 * @param	i	index of item to return a reference to
	 * @return		reference to Item with index i
	 */
	inline DWORD& operator()( INT i )
	{
		return Data[i];
	}

	/** Max number of elements memory is allocated for */
	DWORD	ArrayMax;
	/** Current number of elements in array */
	DWORD	ArrayNum;
	/** Pointer to data */
	DWORD*	Data;
};

/**
 * Non- intrusive script call graph profiler, collecting function pointer and cycle count
 * per function for later analysis in a standalone tool.
 */
struct FScriptCallGraph
{
	/**
	 * Constructor
	 *
	 * @param InSoftMemoryLimit		Max number of bytes used by call graph data (soft limit).
	 */
	FScriptCallGraph( DWORD InSoftMemoryLimit );

	/**
	 * Resets data collection and memory use.
	 *
	 * @param InSoftMemoryLimit		Max number of bytes used by call graph data (soft limit).
	 */
	void Reset( DWORD InSoftMemoryLimit = 0 );
	
	/**
	 * Serializes the profiling data to the passed in archive.	
	 *
	 * @param Ar	Archive to serialize data to.
	 */
	void Serialize( FArchive& Ar );
	
	/**
	 * Emits the end of frame marker. Needs to be called when there are no script functions
	 * on the stack. Also responsible for reseting call graph data if memory limit is reached.
	 */
	void Tick();

	/**
	 * Emits function call token to stream.	
	 *
	 * @param Function	Function pointer to emit.
	 */
	inline void EmitFunction( UFunction* Function )
	{
		Data.AddItem( (DWORD) Function );
	}

	/**
	 * Emits cycle count token.
	 *
	 * @param Cycles	Cycle count to emit.
	 */
	inline void EmitCycles( DWORD Cycles )
	{
		DWORD Value = (Cycles & ~3) | 1;
		Data.AddItem( Value );
	}

	/**
	 * Emits frame end marker.	
	 */
	inline void EmitFrameEnd()
	{
		DWORD Value = (GTicks << 2) | 3;
		Data.AddItem( Value );
	}

	/**
	 * Emits end of stream marker.	
	 */
	inline void EmitEnd()
	{
		DWORD Value = 0;
		Data.AddItem( Value );
	}

	/**
	 * Figures out whether the passed in value is a frame end marker.
	 *
	 * return TRUE if the passed in value is a frame end marker, FALSE otherwise
	 */
	inline UBOOL IsFrameEndMarker( DWORD Value )
	{
		return (Value & 3) == 3;
	}

	/**
	 * Figures out whether the passed in value is the end marker.
	 * 
	 * return TRUE if the passed in value is the end marker, FALSE otherwise
	 */
	inline UBOOL IsEndMarker( DWORD Value )
	{
		return Value == 0;
	}

	/** Array of call graph profiling tokens */
	FLargeDWORDArray	Data;
	/** Soft memory limit call graph profiling data is allowed to reach before it resets itself */
	DWORD				SoftMemoryLimit;
};
/** Global script call graph profiler */
extern FScriptCallGraph* GScriptCallGraph;
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

