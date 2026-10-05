
#include "EnginePrivate.h"
#include "UnLinker.h"
#include "UnScriptPatcher.h"
#include "ScriptPatches.h"

/*----------------------------------------------------------------------------
	FLinkerPatchData.
----------------------------------------------------------------------------*/
/**
 * Determines whether an import/export has been created.
 * 
 * @param	Linker	the linker to check
 * @param	Index	the index of the import/export to check
 * @param	Slack	number of entries away from the source index to allow missing
 *					entries
 *
 * @return	whether the entry is ready to be created
 */
UBOOL FLinkerPatchData::ValidateLinkerIndex( ULinkerLoad* Linker, INT Index, INT Slack )
{
	guard(ValidateLinkerIndex);

	if ( Index > 0 )
	{
		Index--;
		if ( Index < ExportCount )
		{
			FObjectExport& Export = Linker->ExportMap(Index);
			if ( Export._Object == NULL )
				return FALSE;
		}
		else if ( Index - Slack >= ExportCount )
			return FALSE;
	}
	else if ( Index < 0 )
	{
		Index = -Index - 1;
		if ( !Linker->ImportMap.IsValidIndex(Index) )
			return FALSE;

		FObjectImport& Import = Linker->ImportMap(Index);
		if ( Import.XObject == NULL )
		{
			if ( Import.SourceLinker == NULL || Import.SourceIndex == INDEX_NONE )
				return FALSE;
		}
	}

	return TRUE;

	unguard;
}

/**
 * Determines whether this export is ready to be loaded.
 * 
 * @param	Export	the export to validate
 * @param	Loader	the linker for this export
 * @param	Slack	the total number of new exports being added.  If the export is
 *					not ready because one of its dependencies isn't loaded, but that
 *					dependency is located in the linker's ExportMap with Slack of the
 *					linker's ExportMap.Num(), the missing dependency is ignored.
 *
 * @return	TRUE if the export could be safely loaded
 */
UBOOL FLinkerPatchData::VerifyExport( FObjectExport& Export, ULinkerLoad* Loader, INT Slack )
{
	guard(VerifyExport);

	return
		ValidateLinkerIndex(Loader, Export.ClassIndex, Slack) &&
		ValidateLinkerIndex(Loader, Export.PackageIndex, Slack) &&
		ValidateLinkerIndex(Loader, Export.SuperIndex, Slack);

	unguard;
}

/**
 * Ensures that all dependencies of the new exports are loaded.  For any dependencies that
 * don't exist, creates the object via IndexToObject
 * 
 */
void FLinkerPatchData::PreloadExports(ULinkerLoad* Linker)
{
	guard(FLinkerPatchData::PreloadExports);

	UObject::BeginLoad();

	for ( INT i = 0; i < Exports.Num(); i++ )
	{
		FObjectExport& Export = Exports(i);

		// make sure the this export's class is loaded
		if ( !ValidateLinkerIndex(Linker, Export.ClassIndex, Exports.Num()) )
		{
			Linker->IndexToObject(Export.ClassIndex);
		}
		// make sure this export's outer exists
		if ( !ValidateLinkerIndex(Linker, Export.PackageIndex, Exports.Num()) )
		{
			Linker->IndexToObject(Export.PackageIndex);
		}
		// make sure this export's parent exists, if appropriate
		if ( !ValidateLinkerIndex(Linker, Export.SuperIndex, Exports.Num()) )
		{
			Linker->IndexToObject(Export.SuperIndex);
		}
	}

	UObject::EndLoad();

	unguard;
}

/**
 * Integrates any package-level changes in this patch into the package's linker.  These include 
 * new imports, new exports, new names, but NOT modified bytecode (that's specific to each class).
 * This function may be called more than once for the same linker, if previous attempts at patching
 * weren't successful.
 * 
 * @param	Loader					the linker to be patched
 * @param	Ar						the memory reader archive to use for serializing any new objects we create
 * @param	bCreateDependencies		determines whether we're allowed to create objects for export dependencies
 *									(for example, if the class, parent, or outer for an export contained within
 *									this patch doesn't yet exist, determines whether we're allowed to create the
 *									missing objects)
 *
 * @return	TRUE if the patch was successfully applied, FALSE if any of the new exports for this patch couldn't be created
 */
UBOOL FLinkerPatchData::Apply( ULinkerLoad* Loader, FPatchReader& Ar, UBOOL bCreateDependencies )
{
	guard(FLinkerPatchData::Apply);

	// add any new names to the linker's name table
	for ( INT i = 0; i < Names.Num(); i++ )
	{
		Loader->AppendName(Names(i));
		Names.Remove(i--);
	}

	// add any new imports to the linker's import table
	for ( INT i = 0; i < Imports.Num(); i++ )
	{
		Loader->AppendImport(Imports(i));
		Imports.Remove(i--);
	}

	if ( Exports.Num() )
	{
		check(Objects.Num() == Exports.Num());
		if ( bCreateDependencies )
		{
			PreloadExports(Loader);
		}

		for ( INT i = 0; i < Exports.Num(); i++ )
		{
			FObjectExport& Export = Exports(i);

			// if this export couldn't be created successfully, bail out so we don't interfere with
			// linear loading
			if ( !VerifyExport(Export, Loader, Exports.Num()) )
				return FALSE;

			FPatchData* Data = Objects(i);

			// set this export's offset to the current location of the memory-reader
			Export.SerialOffset = Ar.TotalSize();
			
			// load the memory reader with the UObject data corresponding to this export
			Ar.AddPatch(*Data);

			// set the export's size
			Export.SerialSize = Ar.TotalSize() - Export.SerialOffset;
		}

		// add the exports to the linker, and create objects for the exports
		TArray<UObject*> NewExports = Loader->AppendExports(Exports,ExportCount);

		for ( INT i = 0; i < NewExports.Num(); i++ )
		{
			UObject* Object = NewExports(i);
			check(Object);

			// we may need to perform a little fixup
			UField* Field = Cast<UField>(Object);
			if ( Field != NULL )
			{
				// if this export represents a UField
				UStruct* Parent = Cast<UStruct>(Object->GetOuter());
				if ( Parent != NULL )
				{
					// and its outer is a UStruct, we need to add it the struct's Children linked list
					UBOOL bNeedsLink=TRUE;
					for ( TFieldIterator<UField> It(Parent); It && It.GetStruct() == Parent; ++It )
					{
						if ( *It == Field )
						{
							// if this object already exists in the struct's Children list, then the
							// struct hasn't been garbage collected since the last time we applied this patch;
							// we don't need to do anything in this case
							bNeedsLink = FALSE;
							break;
						}
					}

					if ( bNeedsLink )
					{
						// insert the object into the linked list
						Field->Next = Parent->Children;
						Parent->Children = Field;

						UFunction* Function = Cast<UFunction>(Field,CLASS_IsAUFunction);
						if ( Function != NULL )
						{
							// if this object is a function, add it to its owner's function lookup map
							UState* StateParent = Cast<UState>(Parent);
							StateParent->FuncMap.Set(Function->GetFName(),Function);
						}
						else
						{
							UProperty* Property = Cast<UProperty>(Field,CLASS_IsAUProperty);
							if ( Property != NULL )
							{
								// this rebuilds the optimized property linked lists in the owning struct, 
								// updates the struct's PropertiesSize, and realigns the struct given its
								// new size
								FArchive DummyAr;
								Parent->Link(DummyAr, TRUE);
							}
						}
					}
				}
			}
		}
	}

	return TRUE;
	unguard;
}

/*----------------------------------------------------------------------------
	FScriptPatcher.
----------------------------------------------------------------------------*/
FScriptPatcher::FScriptPatcher()
{
	guard(FScriptPatcher::FScriptPatcher);

	BuildPatchList(PackageUpdates);

	unguard;
}

FScriptPatcher::~FScriptPatcher()
{
	guard(FScriptPatcher::~FScriptPatcher);

	for ( INT i = PackageUpdates.Num() - 1; i >= 0; i-- )
		delete PackageUpdates(i);

	unguard;
}


void FScriptPatcher::ApplyPatches( UBOOL bCreateDependencies )
{
	guard(FScriptPatcher::ApplyPatches);

	for ( INT i = 0; i < PackageUpdates.Num(); i++ )
	{
		FLinkerPatchData* Update = PackageUpdates(i);

		// find the package associated with this patch - only need to patch it if the package is loaded
		UPackage* Package = FindObject<UPackage>(NULL, *Update->PackageName);
		if ( Package != NULL )
		{
			// grab the linker for this package
			UObject::BeginLoad();
			ULinkerLoad* Loader = UObject::GetPackageLinker(Package,NULL,LOAD_NoWarn|LOAD_NoVerify,NULL,NULL);
			check(Loader);
			UObject::EndLoad();

			FPatchReader Ar;
			// uc2 port: hold the load count across Apply() as well. Apply -> ULinkerLoad::AppendImport ->
			// VerifyImport may have to find or create the linker of the new import's package
			// (GetPackageLinker, which checks GObjBeginLoadCount). That happens when this linker was made
			// just above with LOAD_NoVerify, i.e. for a package that exists only as the empty UPackage a network
			// client creates for each USES line (UNetPendingLevel::NotifyReceivedText); Epic's shipped (FINAL_RELEASE)
			// build had check() compiled out. The count stays held through the bytecode swap below, so objects
			// that AppendExports created are still serialized from the patch reader by the one EndLoad, as before.
			UObject::BeginLoad();
			if ( Update->Apply(Loader,Ar, bCreateDependencies) )
			{
				// for any patches that contain updated bytecode, here is where we replace the existing bytecode

				// swap the linker's source-reader to our memory reader, so that all any objects created during
				// serialization of the new bytecode will be loaded from our memory reader, instead of disk
				FArchive* OldArchive = Loader->Loader;
				Loader->Loader = &Ar;
				for ( INT ScriptIdx = 0; ScriptIdx < Update->ScriptPatches.Num(); ScriptIdx++ )
				{
					FPatchData* Patch = Update->ScriptPatches(ScriptIdx);
					UStruct* Struct = FindObject<UStruct>(ANY_PACKAGE,*Patch->DataName);
					if ( Struct != NULL )
					{
						check(Struct);
						check(Struct->GetLinker() == Loader);

						FPatchReader PatchAr(*Patch);
						PatchAr.SetLoader(Loader);

						Struct->Script.Empty(Patch->Size);
						Struct->Script.Add(Patch->Size);

						INT iCode = 0;
						while ( iCode < Patch->Size )
							Struct->SerializeExpr(iCode,PatchAr);
					}
				}

				// load any objects that were created during SerializeExpr()
				UObject::EndLoad();

				// restore the linker's previous reader
				Loader->Loader = OldArchive;
			}
			else
			{
				UObject::EndLoad();
				// if the patch couldn't be applied, leave it in the PackageUpdate array for now;
				// we'll retry the patch after the map has been fully loaded
				continue;
			}
		}

		delete Update;
		PackageUpdates.Remove(i--);
	}

	unguard;
}
