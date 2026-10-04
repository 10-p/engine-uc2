// Copyright © 1999 the e-comm group, inc.
//
// CScopeLock.h: This class is used to auto lock a critical section. It
// releases the critical section when it goes out of scope.
//

#include "stdafx.h"
#include "resource.h"
#include "CScopeLock.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

/***************************************************************************
* Function:	CScopeLock::CScopeLock()
* Author:	Joe Graf
* Date:		6/12/99
* Args:		<void>
* Returns:	<void>
* Purpose:	This is the default constructor. Not valid
***************************************************************************/

CScopeLock::CScopeLock()
{
	// This is not a valid constrcutor
	_ASSERTE(false);
}

/***************************************************************************
* Function:	CScopeLock::CScopeLock()
* Author:	Joe Graf
* Date:		6/12/99
* Args:		<LPCRITICAL_SECTION> pcs - the pointer to the cs to lock
* Returns:	<void>
* Purpose:	This is the second constructor. It locks the critical section by
*			default.
***************************************************************************/

CScopeLock::CScopeLock(CComAutoCriticalSection* pcs) : m_pcs(pcs),
	m_bIsLocked(true)
{
	// Do the initial lock
	Lock();
}

/***************************************************************************
* Function:	CScopeLock::~CScopeLock()
* Author:	Joe Graf
* Date:		6/12/99
* Args:		<void>
* Returns:	<void>
* Purpose:	This is the default constructor. It releases the critical
*			section if it has been acquired.
***************************************************************************/

CScopeLock::~CScopeLock()
{
	// Only release if we have it locked
	if( m_bIsLocked == true )
	{
		// Unlock the cs
		Unlock();
	}
}

/***************************************************************************
* Function:	CScopeLock::Lock()
* Author:	Joe Graf
* Date:		6/12/99
* Args:		<void>
* Returns:	<void>
* Purpose:	Locks the critical section
***************************************************************************/

void CScopeLock::Lock(void)
{
	// Validate the pointer
	_ASSERTE(AfxIsValidAddress(m_pcs,sizeof(CComAutoCriticalSection)));
	// Do the lock
	m_pcs->Lock();
}

/***************************************************************************
* Function:	CScopeLock::Unlock()
* Author:	Joe Graf
* Date:		6/12/99
* Args:		<void>
* Returns:	<void>
* Purpose:	Releases the critical section
***************************************************************************/

void CScopeLock::Unlock(void)
{
	// Validate the pointer
	_ASSERTE(AfxIsValidAddress(m_pcs,sizeof(CComAutoCriticalSection)));
	// Let go of the cs
	m_pcs->Unlock();
	// Set the locked to false
	m_bIsLocked = false;
}

