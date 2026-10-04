#ifndef _PUSHBUFFER_H
#define _PUSHBUFFER_H

class FArchive;

// Static mesh pushbuffer names are StaticMeshName<section#>_PB
#define GEN_SM_PUSHBUFFER_NAME TEXT("%s_%d_PB")
// Skeletal mesh pushbuffer names are SkeletalMeshName<lod#><section#>_PB
#define GEN_SKEL_PUSHBUFFER_NAME TEXT("%s_%d_%d_PB")

/* scion ======================================================================
 * Author: jg
 *
 * This class is an abstract base class for the D3D push buffer class. It allows
 * access to specific push buffer members without having any knowledge of D3D.
 *
 * ============================================================================
 */
class ENGINE_API FPushBuffer
{
public:
	virtual ~FPushBuffer() {}
	// Determines how large the pushbuffer is
	virtual DWORD GetSize(void) = 0;
	// Determines how much memory the pushbuffer is using (runascopy)
	virtual DWORD GetAllocatedSize(void) = 0;
	// Gets access to the push buffer data member for writing into it
	virtual void* GetPushBufferData(void) = 0;
	// Indicates whether this is a run as copy or not
	virtual UBOOL IsRunAsCopy(void) = 0;
	// Gets the raw D3DPushBuffer interface
	virtual void* GetPushBuffer(void) = 0;
	// Determines if the push buffer is being used or not
	virtual UBOOL IsBusy(void) = 0;
	// serialize the class
	virtual void Serialize( FArchive& Ar ) = 0;
	// Whether this is in a XPR or not
	virtual UBOOL IsInXPR(void) = 0;
};

// Global function for doing fix ups on pushbuffers
// NULL means not found
FPushBuffer* GetPushbufferFromXPR(const TCHAR* PBName);

// XPR Pushbuffer Format
typedef struct 
{
    DWORD Common;
    DWORD Size;
    DWORD AllocatedSize;
	DWORD Offset;
} XPRPushBuffer;
#endif