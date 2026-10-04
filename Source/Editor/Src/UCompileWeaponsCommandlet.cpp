#include "EditorPrivate.h"

class UCompileWeaponsCommandlet : public UCommandlet
{
	DECLARE_CLASS(UCompileWeaponsCommandlet,UCommandlet,CLASS_Transient,Editor);

	void StaticConstructor()
	{
		LogToStdout     = 1;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;
	}

	UBOOL IsA( UClass* Test, FString Class )
	{
		UClass* TempClass = NULL;
		for( TempClass = Test; TempClass; TempClass = TempClass->GetSuperClass() )
		{
			if( appStricmp( TempClass->GetName(), *Class ) == 0 )
			{
				break;
			}
		}

		return (TempClass != NULL);
	}

	FLOAT GetAnimDuration( FMeshAnimSeq* InAnim )
	{
		check( InAnim );
		return InAnim->NumFrames / InAnim->Rate;
	}

	void ProcessAnim(UMeshAnimation* Anim, TMultiMap<FName,FString>* WpnSection, INT& CurrentNotifierIdx, TArray<FString>* NotifierList, AWeapon* DefaultWpn)
	{
		static FName Key( TEXT("AnimList") );

		if( Anim )
		{
			// Loop through each animation
			for( INT AnimIdx = 0; AnimIdx < Anim->AnimSeqs.Num(); AnimIdx++ )
			{
				// and add the animation info
				FMeshAnimSeq AnimSeq = Anim->AnimSeqs(AnimIdx);

				INT		NotifierCount	= 0;
				FLOAT	Duration		= GetAnimDuration( &AnimSeq );
                
				CurrentNotifierIdx		= NotifierList->Num();

				// Put notifier info into NotifierList
				for( INT NotifierIdx = 0; NotifierIdx < AnimSeq.Notifys.Num(); NotifierIdx++ )
				{
					FMeshAnimNotify		Notifier = AnimSeq.Notifys(NotifierIdx);
					UAnimNotify_Script*	Obj		 = Cast<UAnimNotify_Script>(Notifier.NotifyObject);

					if( Obj )
					{
						// check if the specified notify function exists
						if (!DefaultWpn->FindFunction(Obj->NotifyName))
						{
							warnf(NAME_Warning, TEXT("Failed to find notify function %s in class %s"), *Obj->NotifyName, DefaultWpn->GetClass()->GetPathName());
						}

						NotifierCount++;

						FString Buffer = FString::Printf( TEXT("(Anim=%s,Function=%s,Time=%f)"),
							*AnimSeq.Name,
							*Obj->NotifyName,
							Notifier.Time * Duration );

						INT Idx = NotifierList->AddZeroed();
						(*NotifierList)(Idx) = Buffer;
					}
				}

				FString Buffer = FString::Printf( TEXT("(Anim=%s,Duration=%f,NotifierIdx=%i,NotifierCount=%d)"), 
					*AnimSeq.Name, 
					Duration,
					NotifierCount ? CurrentNotifierIdx : -1,
					NotifierCount );

				WpnSection->AddUnique( *Key, *Buffer );
			}
		}
	}


	INT Main(const TCHAR *Parms)
	{
		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
		GEditor->InitEditor();

		// load all packages in WeaponPackages
		debugf(TEXT("Loading weapon packages..."));

		// Get config section where we should find packages to load
		TArray<FString> WeaponPackages;
		TMultiMap<FName,FString> *Section = GConfig->GetSectionPrivate( TEXT("Editor.UCompileWeaponsCommandlet"), 0, 1, TEXT("Avaris.ini") );
		
		check(Section && "Failed to find weapon packages, did you specify the correct ini?");

		// Get the list of packages to load
		Section->MultiFind( FName(TEXT("WeaponPackages")), WeaponPackages );

		// Load each package in the array
		for( INT PkgIdx = 0; PkgIdx < WeaponPackages.Num(); PkgIdx++ )
		{
			LoadPackage( NULL, *WeaponPackages(PkgIdx), LOAD_NoWarn );
		}

		debugf(TEXT("Analyzing weapons..."));

		// Find all UCWeapon classes
		TArray<UClass*>	WeaponClasses;
		for( TObjectIterator<UClass> classIt; classIt; ++classIt )
		{
			if( IsA( *classIt, FString("UCWeapon") ) )
			{
				WeaponClasses.AddItem( *classIt );
			}
		}

		// For each weapon found
		for( INT WpnIdx = 0; WpnIdx < WeaponClasses.Num(); WpnIdx++ )
		{
			// Get the default actor
			UClass*		WpnClass	 = WeaponClasses(WpnIdx);
			AWeapon*	DefaultActor = (AWeapon*)WpnClass->GetDefaultsSafe();

			// If default actor is not a melee weapon
			if( DefaultActor && !DefaultActor->bMeleeWeapon )
			{
				// Get the section in Weapons.ini for this weapon
				FString SectionName = FString::Printf( TEXT("%s.%s"), WpnClass->GetOuter()->GetName(), WpnClass->GetName() );
				TMultiMap<FName,FString> *WpnSection = GConfig->GetSectionPrivate( *SectionName, 1, 1, TEXT("Weapons.ini") );

				check( WpnSection && "Failed to find weapon section" );

				debugf(TEXT("Compiling %s..."), WeaponClasses(WpnIdx)->GetName() );

				// Clear the current anim list settings
				TArray<FString> AnimConfig;
				FName Key( TEXT("AnimList") );
				FName NotifierKey( TEXT("NotifierList") );
				WpnSection->Remove( Key );
				WpnSection->Remove( NotifierKey );

				// Load the weapon mesh
				USkeletalMesh* Mesh = 
					DefaultActor->MeshName.Len() == 0 ? NULL :
						(USkeletalMesh*)StaticLoadObject( USkeletalMesh::StaticClass(), NULL, *DefaultActor->MeshName, NULL, LOAD_NoWarn, NULL );

				if( Mesh )
				{
					// For the default animation set
					UMeshAnimation* Anim = Mesh->DefaultAnim;
					TArray<FString> NotifierList;
					INT				CurrentNotifierIdx = 0;

					ProcessAnim(Anim, WpnSection, CurrentNotifierIdx, &NotifierList, DefaultActor);

					// Loop through each additional link set
					for( INT LinkIdx = 0; LinkIdx < DefaultActor->AnimationSets.Num(); LinkIdx++ )
					{
						FString SetName = DefaultActor->AnimationSets(LinkIdx);
						Anim = (UMeshAnimation*)StaticLoadObject( UMeshAnimation::StaticClass(), NULL, *SetName, NULL, LOAD_NoWarn, NULL );

						ProcessAnim(Anim, WpnSection, CurrentNotifierIdx, &NotifierList, DefaultActor);
					}

					// Go through notifier list and add the strings to the section
					for( INT ListIdx = 0; ListIdx < NotifierList.Num(); ListIdx++ )
					{
						WpnSection->AddUnique( NotifierKey, *NotifierList(ListIdx) );
					}
				}
			}
		}

		GConfig->Flush( 0 );

		debugf(TEXT("All done"));
		GIsRequestingExit = 1;
		return 0;
	}

};
IMPLEMENT_CLASS(UCompileWeaponsCommandlet);