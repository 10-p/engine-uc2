INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
void PreNetReceive();
void PostNetReceive();
virtual UBOOL IsAWeaponAttachment() { return true; }

