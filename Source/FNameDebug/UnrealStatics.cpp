#include "stdafx.h"

// Static FName variables.
UBOOL				FName::Initialized = 0;
FNameEntry*			FName::NameHash[4096];
TArray<FNameEntry*>	FName::Names;
TArray<INT>         FName::Available;
