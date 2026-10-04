// Copyright © 1999 the e-comm group, inc.
//
// CScopeLock.h: This class is used to auto lock a critical section. It
// releases the critical section when it goes out of scope.
//

#ifndef _ITE_SCOPE_LOCK_H
#define _ITE_SCOPE_LOCK_H

class CScopeLock  
{
	// This is the critical section to perform locks on
	CComAutoCriticalSection* m_pcs;
	// Indicates whether the cs is locked or not
	bool m_bIsLocked;

public:
	CScopeLock();
	// This is the only valid constructor
	CScopeLock(CComAutoCriticalSection* pcs);
	// Releases the critical section if locked
	virtual ~CScopeLock();

// Operations
	// Locks the critical section
	void Lock(void);
	// Releases the critical section
	void Unlock(void);
};

#endif
