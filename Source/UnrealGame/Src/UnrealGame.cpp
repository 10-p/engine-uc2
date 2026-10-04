#include "UnrealGame.h"

IMPLEMENT_PACKAGE(UnrealGame);

// Register things.
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) UNREALGAME_API FName UNREALGAME_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "UnrealGameClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// Import natives
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "UnrealGameClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY

IMPLEMENT_CLASS(AUnrealPlayer);
IMPLEMENT_CLASS(AUnrealPawn);
