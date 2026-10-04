/*=============================================================================
The Package Builder

Revision history:
* Created by Joel Van Eenwyk

=============================================================================*/

struct FUsedMaterialInfo
{
	INT Actors;
	INT BSP;
	INT StaticMeshes;
	INT StaticMeshSkins;
	INT Terrains;
	INT Sprites;
	INT EmitterTextures;

	FUsedMaterialInfo()
		:	Actors(0)
		,	BSP(0)
		,	StaticMeshes(0)
		,	StaticMeshSkins(0)
		,	Terrains(0)
		,	Sprites(0)
		,	EmitterTextures(0)
	{}
};

class WDlgPackageBuilder : public WDialog
{
	DECLARE_WINDOWCLASS(WDlgPackageBuilder,WDialog,UnrealEd)

	// Variables.
	WButton NewButton, CancelButton;

	WEdit PackageTextureEdit;
	WEdit PackageStaticMeshEdit;

	WCheckBox chkCreateTexture;
	WCheckBox chkCreateStaticMesh;
	WCheckBox chkReplaceTextureCheck;
	WCheckBox chkReplaceStaticMeshCheck;

	WListBox lstTexture;
	WListBox lstStaticMesh;

	WCheckBox OriginalCheck;

	FString TexturePackage;
	FString StaticMeshPackage;

	FDelegate OnNewPackage;

	WDlgProgressBuilder *DlgProgressBar;

	UMaterial**		MaterialList;
	UMaterial**		NewMaterialList;
	int				numMaterials;

	// Constructor.
	WDlgPackageBuilder( UObject* InContext, WWindow* InOwnerWindow, FDelegate InOnNewPackage )
		:	WDialog		( TEXT("New Material"), IDDIALOG_PACKAGE_BUILDER, InOwnerWindow )
		,	NewButton	( this, IDPB_CREATEPACKAGE, FDelegate(this,(TDelegate)OnCreate) )
		,	CancelButton	( this, IDCANCEL,		FDelegate(this,(TDelegate)EndDialogFalse) )
		,	PackageTextureEdit			( this, IDEC_TEXTURE )
		,	PackageStaticMeshEdit		( this, IDEC_STATICMESH )
		,	chkReplaceTextureCheck		( this, IDC_REPLACETEXTURES )
		,	chkReplaceStaticMeshCheck	( this, IDC_REPLACESTATICMESHES )
		,	chkCreateTexture			( this, IDC_CREATETEXTURE, FDelegate(this,(TDelegate)OnTextureCheck) )
		,	chkCreateStaticMesh			( this, IDC_CREATESTATICMESH, FDelegate(this,(TDelegate)OnStaticCheck) )
		,	lstTexture					( this, IDC_TEXTURE_LIST )
		,	lstStaticMesh				( this, IDC_STATICMESH_LIST )
		,	OriginalCheck				( this, IDRB_ORIGINAL )
		,	OnNewPackage	( InOnNewPackage )
	{
	}

	// WDialog interface.
	void OnInitDialog()
	{
		guard(WDlgPackageBuilder::OnInitDialog);
		WDialog::OnInitDialog();

		NewMaterialList = new(GMem,16384)UMaterial*;

		GetUsedList(numMaterials, MaterialList);

		UpdateTextureList();
		UpdateStaticMeshList();

		SendMessageX( lstTexture, LB_SELITEMRANGE, (WPARAM)1, MAKELPARAM(0, lstTexture.GetCount()) );
		SendMessageX( lstStaticMesh, LB_SELITEMRANGE, (WPARAM)1, MAKELPARAM(0, lstStaticMesh.GetCount()) );

		if (lstTexture.FindItem(TEXT("Engine")) != -1)
			SendMessageX( lstTexture, LB_SETSEL, (WPARAM)FALSE, lstTexture.FindItem(TEXT("Engine")) );

		if (lstTexture.FindItem(TEXT("Gameplay")) != -1)
			SendMessageX( lstTexture, LB_SETSEL, (WPARAM)FALSE, lstTexture.FindItem(TEXT("Gameplay")) );
		
		PackageTextureEdit.SetText( TEXT("TexturePackage") );
		PackageStaticMeshEdit.SetText( TEXT("StaticMeshPackage") );
		chkReplaceTextureCheck.SetCheck(1);
		chkReplaceStaticMeshCheck.SetCheck(1);
		chkCreateTexture.SetCheck(1);
		chkCreateStaticMesh.SetCheck(1);

		OriginalCheck.SetCheck( BST_CHECKED );

		::SetFocus( PackageTextureEdit.hWnd );

		DlgProgressBar = new WDlgProgressBuilder( NULL, this );

		unguard;
	}
	void OnDestroy()
	{
		guard(WDlgPackageBuilder::OnDestroy);
		WDialog::OnDestroy();

		unguard;
	}
	void DoModeless()
	{
		guard(WDlgPackageBuilder::DoModeless);
		_Windows.AddItem( this );
		hWnd = CreateDialogParamA( hInstance, MAKEINTRESOURCEA(IDDIALOG_PACKAGE_BUILDER), OwnerWindow?OwnerWindow->hWnd:NULL, (DLGPROC)StaticDlgProc, (LPARAM)this);
		if( !hWnd )
			appGetLastError();
		Show(1);

		unguard;
	}
	BOOL GetDataFromUser()
	{
		guard(WDlgNewTexture::GetDataFromUser);

		TexturePackage = PackageTextureEdit.GetText();
		StaticMeshPackage = PackageStaticMeshEdit.GetText();

		if( !TexturePackage.Len() || !StaticMeshPackage.Len() )
		{
			appMsgf( 0, TEXT("Invalid input.") );
			return FALSE;
		}

		return TRUE;
		unguard;
	}
	void OnTextureCheck()
	{
		guard(WDlgPackageBuilder::OnTextureCheck);

		if (chkCreateTexture.IsChecked())
		{
			EnableWindow(PackageTextureEdit.hWnd, true);
			EnableWindow(chkReplaceTextureCheck.hWnd, true);
		}
		else
		{
			EnableWindow(PackageTextureEdit.hWnd, false);
			EnableWindow(chkReplaceTextureCheck.hWnd, false);
		}

		unguard;
	}
		void OnStaticCheck()
	{
		guard(WDlgPackageBuilder::OnStaticCheck);

		if (chkCreateStaticMesh.IsChecked())
		{
			EnableWindow(PackageStaticMeshEdit.hWnd, true);
			EnableWindow(chkReplaceStaticMeshCheck.hWnd, true);
		}
		else
		{
			EnableWindow(PackageStaticMeshEdit.hWnd, false);
			EnableWindow(chkReplaceStaticMeshCheck.hWnd, false);
		}

		unguard;
	}
	void OnCreate()
	{
		guard(WDlgPackageBuilder::OnCreate);

		DlgProgressBar->DoModeless(1);
		UpdateWindow(DlgProgressBar->hWnd);

		if (GetDataFromUser())
		{
			GetUsedList(numMaterials, MaterialList);

			if (chkCreateTexture.IsChecked())
			{
				CreateInUseTextures(TexturePackage);

				if (chkReplaceTextureCheck.IsChecked())
					ReplaceTextures();
			}

			DlgProgressBar->progressMaterial.SetProgress(100, 100);

			if (chkCreateStaticMesh.IsChecked())
			{
				CreateInUseStaticMeshes(*StaticMeshPackage, chkReplaceStaticMeshCheck.IsChecked());

				// replace the textures used on the static meshes for the new ones
				if (chkCreateTexture.IsChecked() && chkReplaceTextureCheck.IsChecked())
					ReplaceStaticMeshTextures();
			}

			DlgProgressBar->progressStaticMesh.SetProgress(100, 100);

			GBrowserMaster->RefreshAll();
		}

		ShowWindow(DlgProgressBar->hWnd, SW_HIDE);

		EndDialog(0);

		unguard;
	}
	bool checkTextureList(FString Package)
	{
		guard(WDlgPackageBuilder::checkTextureList);

		return (lstTexture.GetSelected(lstTexture.FindItem(*Package)) > 0);

		unguard;
	}
	bool checkStaticMeshList(FString Package)
	{
		guard(WDlgPackageBuilder::checkStaticMeshList);

		return (lstStaticMesh.GetSelected(lstStaticMesh.FindItem(*Package)) > 0);

		unguard;
	}
	void UpdateTextureList()
	{
		guard(WDlgPackageBuilder::UpdateTextureList);

		for (int i = 0; i < numMaterials; i++)
		{
			UMaterial* Material = MaterialList[i];

			FString Package;

			if( !Cast<UPackage>(Material->GetOuter()->GetOuter()) )
				Package = Material->GetOuter()->GetName();
			else
				Package = Material->GetOuter()->GetOuter()->GetName();

			if (lstTexture.FindItem(*Package) == -1)
				lstTexture.AddItem(*Package);
		}

		unguard;
	}
	void UpdateStaticMeshList()
	{
		guard(WDlgPackageBuilder::UpdateStaticMeshList);

		// ACTORS
		for( INT x = 0 ; x < GUnrealEd->Level->Actors.Num() ; ++x )
		{
			AActor* Actor  = Cast<AActor>( GUnrealEd->Level->Actors(x) );

			if (Actor)
			{
				AStaticMeshActor* Obj = Cast<AStaticMeshActor>( Actor );

				if (Obj)
				{
					UStaticMesh* StaticMesh = Obj->StaticMesh;

					if ( StaticMesh )
					{
						FString Package;

						if( !Cast<UPackage>(StaticMesh->GetOuter()->GetOuter()) )
							Package = StaticMesh->GetOuter()->GetName();
						else
							Package = StaticMesh->GetOuter()->GetOuter()->GetName();

						if (lstStaticMesh.FindItem(*Package) == -1)
							lstStaticMesh.AddItem(*Package);
					}
				}
			}
		}

		unguard;
	}
	UStaticMesh *CreateNewStaticMesh(UStaticMesh *OldStaticMesh, FName PkgName, FName GroupName, FName Name)
	{
		guard(WDlgPackageBuilder::CreateNewStaticMesh);

		TArray<FStaticMeshTriangle>	Triangles;
		TArray<FStaticMeshMaterial>	Materials;

		GUnrealEd->Trans->Begin(TEXT("STATICMESH FROM SELECTION"));
		GUnrealEd->Level->Modify();
		GUnrealEd->FinishAllSnaps(GUnrealEd->Level);

		UPackage *Pkg = GUnrealEd->CreatePackage(NULL,*PkgName);
		Pkg->bDirty = 1;

		if( GroupName != NAME_None )
			Pkg = GUnrealEd->CreatePackage(Pkg,*GroupName);

		check(OldStaticMesh);

		if(!OldStaticMesh->RawTriangles.Num())
			OldStaticMesh->RawTriangles.Load();

		// Create the UStaticMesh object.
		UStaticMesh*	StaticMesh = new(Pkg,Name,RF_Public|RF_Standalone) UStaticMesh;

		StaticMesh->RawTriangles.Add(OldStaticMesh->RawTriangles.Num());
		appMemcpy(&StaticMesh->RawTriangles(0),&OldStaticMesh->RawTriangles(0),OldStaticMesh->RawTriangles.Num() * sizeof(FStaticMeshTriangle));

		StaticMesh->Materials.Add(OldStaticMesh->Materials.Num());
		appMemcpy(&StaticMesh->Materials(0),&OldStaticMesh->Materials(0),OldStaticMesh->Materials.Num() * sizeof(FStaticMeshMaterial));

		//merge
		/*
		StaticMesh->MaxSmoothingAngles.Add(OldStaticMesh->MaxSmoothingAngles.Num());
		appMemcpy(&StaticMesh->MaxSmoothingAngles(0),&OldStaticMesh->MaxSmoothingAngles(0),OldStaticMesh->MaxSmoothingAngles.Num() * sizeof(FLOAT));

		// if there are any collision triangles, then create a collision model from them for this static mesh
		if (OldStaticMesh->CollisionModel)
		{
			StaticMesh->CollisionModel = new(StaticMesh->GetOuter()) UModel(NULL,1);

			for(int i = 0; i < OldStaticMesh->CollisionModel->Polys->Element.Num(); i++)
			{
				FPoly*	Poly = new(StaticMesh->CollisionModel->Polys->Element) FPoly();
				Poly->Init();
				for (int iv = 0; iv < OldStaticMesh->CollisionModel->Polys->Element(i).NumVertices; iv++)
				{
					Poly->Vertex[iv] = OldStaticMesh->CollisionModel->Polys->Element(i).Vertex[iv];
				}

				Poly->iLink = INDEX_NONE;
				Poly->NumVertices = iv;
				Poly->CalcNormal(1);
				Poly->Finalize(0);
			}

			StaticMesh->CollisionModel->BuildBound();

			GEditor->bspBuild(StaticMesh->CollisionModel,BSP_Good,15,70,1,0);
			GEditor->bspRefresh(StaticMesh->CollisionModel,1);
			GEditor->bspBuildBounds(StaticMesh->CollisionModel);
		}

		//--------------------------------
		// KARMA
		//--------------------------------

		// If we dont already have physics props, construct them here.
		if(OldStaticMesh->KPhysicsProps && !StaticMesh->KPhysicsProps)
		{
			StaticMesh->KPhysicsProps = ConstructObject<UKMeshProps>(UKMeshProps::StaticClass(), StaticMesh);

			StaticMesh->KPhysicsProps->COMOffset = OldStaticMesh->KPhysicsProps->COMOffset;
			appMemcpy(&StaticMesh->KPhysicsProps->InertiaTensor,&OldStaticMesh->KPhysicsProps->InertiaTensor,6 * sizeof(FLOAT));

			StaticMesh->KPhysicsProps->AggGeom.SphereElems.Add(OldStaticMesh->KPhysicsProps->AggGeom.SphereElems.Num());
			appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.SphereElems(0),&OldStaticMesh->KPhysicsProps->AggGeom.SphereElems(0),OldStaticMesh->KPhysicsProps->AggGeom.SphereElems.Num() * sizeof(FKSphereElem));

			StaticMesh->KPhysicsProps->AggGeom.BoxElems.Add(OldStaticMesh->KPhysicsProps->AggGeom.BoxElems.Num());
			appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.BoxElems(0),&OldStaticMesh->KPhysicsProps->AggGeom.BoxElems(0),OldStaticMesh->KPhysicsProps->AggGeom.BoxElems.Num() * sizeof(FKBoxElem));

			StaticMesh->KPhysicsProps->AggGeom.CylinderElems.Add(OldStaticMesh->KPhysicsProps->AggGeom.CylinderElems.Num());
			appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.CylinderElems(0),&OldStaticMesh->KPhysicsProps->AggGeom.CylinderElems(0),OldStaticMesh->KPhysicsProps->AggGeom.CylinderElems.Num() * sizeof(FKCylinderElem));

			StaticMesh->KPhysicsProps->AggGeom.ConvexElems.AddZeroed(OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems.Num());

			for (int i = 0; i < StaticMesh->KPhysicsProps->AggGeom.ConvexElems.Num(); i++)
			{
				appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).TM,&OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).TM,sizeof(FMatrix));

				StaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).VertexData.Add(OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).VertexData.Num());
				appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).VertexData(0),&OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).VertexData(0),OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).VertexData.Num() * sizeof(FVector));

				StaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).PlaneData.Add(OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).PlaneData.Num());
				appMemcpy(&StaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).PlaneData(0),&OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).PlaneData(0),OldStaticMesh->KPhysicsProps->AggGeom.ConvexElems(i).PlaneData.Num() * sizeof(FPlane));
			}
		}

		// copy over the karma collision object
		StaticMesh->KCollisionGeom.Add(OldStaticMesh->KCollisionGeom.Num());
		appMemcpy(&StaticMesh->KCollisionGeom(0),&OldStaticMesh->KCollisionGeom(0),OldStaticMesh->KCollisionGeom.Num() * sizeof(McdGeometryID));

		StaticMesh->KCollisionGeomScale3D = OldStaticMesh->KCollisionGeomScale3D;

		if (StaticMesh->KPhysicsProps)
			KUpdateMassProps(StaticMesh->KPhysicsProps);
		*/
		//--------------------------------

		StaticMesh->UseSimpleBoxCollision = OldStaticMesh->UseSimpleBoxCollision;
		StaticMesh->UseSimpleKarmaCollision = OldStaticMesh->UseSimpleKarmaCollision;
		StaticMesh->UseSimpleLineCollision = OldStaticMesh->UseSimpleLineCollision;
		StaticMesh->UseVertexColor = OldStaticMesh->UseVertexColor;

		StaticMesh->Build();

		//merge
		/*
		// Assign a valid content authentication key. 
		StaticMesh->AuthenticationKey = StaticMesh->CreateAuthenticationKey();
		*/

		GUnrealEd->Trans->End();
		GUnrealEd->RedrawLevel(GUnrealEd->Level);

		return StaticMesh;

		unguard;
	}
	void CreateInUseStaticMeshes(FName PkgName, BOOL bReplace)
	{
		guard(WDlgPackageBuilder::CreateInUseStaticMeshes);

		GWarn->BeginSlowTask( TEXT("Importing Animation File"), 1);

		// ACTORS
		for( INT x = 0 ; x < GUnrealEd->Level->Actors.Num() ; ++x )
		{
			AActor* Actor  = Cast<AActor>( GUnrealEd->Level->Actors(x) );

			DlgProgressBar->progressFinal.SetProgress((float)x / (float)GUnrealEd->Level->Actors.Num() * 25 + 50, 100);
			DlgProgressBar->progressStaticMesh.SetProgress((float)x / (float)GUnrealEd->Level->Actors.Num() * 50, 100);

			if (Actor)
			{
				AStaticMeshActor* Obj = Cast<AStaticMeshActor>( Actor );
				//merge
				//AKActor* KObj = Cast<AKActor>( Actor );

				if (Obj /*//merge || KObj*/)
				{
					UStaticMesh* StaticMesh;
					
					//merge if (Obj)
						StaticMesh = Obj->StaticMesh;
					//merge else
						//merge StaticMesh = KObj->StaticMesh;

					if ( StaticMesh )
					{
						FString Package;
						FString Group;

						if( !Cast<UPackage>(StaticMesh->GetOuter()->GetOuter()) )
						{
							Group = TEXT("");
							Package = StaticMesh->GetOuter()->GetName();
						}
						else
						{			
							Group = StaticMesh->GetOuter()->GetName();
							Package = StaticMesh->GetOuter()->GetOuter()->GetName();
						}

						if (checkStaticMeshList(Package))
						{		
							/*
							GUnrealEd->CurrentStaticMesh = StaticMesh;

							FString PkgString = *PkgName;
							FString StaticMeshName = StaticMesh->GetName();

							GUnrealEd->Exec( *FString::Printf( TEXT("STATICMESH FROM SELECTION PACKAGE=%s GROUP=%s NAME=%s"),
							*PkgString,
							TEXT(""),
							*StaticMeshName )
							);*/

							UStaticMesh *NewStaticMesh;
							
							if (OriginalCheck.IsChecked())
								NewStaticMesh = CreateNewStaticMesh(StaticMesh, PkgName, *Group, StaticMesh->GetName());
							else
                                NewStaticMesh = CreateNewStaticMesh(StaticMesh, PkgName, *Package, StaticMesh->GetName());

							if (NewStaticMesh && bReplace)
							{
								//mergeif (Obj)
									Obj->StaticMesh = NewStaticMesh;
								//merge else
									//merge KObj->StaticMesh = NewStaticMesh;
							}
						}
					}
				}
			}
		}

		GWarn->EndSlowTask();

		unguard;
	}
	void ReplaceStaticMeshTextures()
	{
		guard(WDlgPackageBuilder::ReplaceTextures);

		UMaterial *pTexture1, *pTexture2;

		for (int i = 0; i < numMaterials; i++)
		{
			DlgProgressBar->progressFinal.SetProgress((float)i / (float)numMaterials * 25 + 75, 100);
			DlgProgressBar->progressStaticMesh.SetProgress((float)i / (float)numMaterials * 50 + 50, 100);

			pTexture1 = MaterialList[i];
			pTexture2 = NewMaterialList[i];

			for( FObjectIterator It; It; ++It )
			{
				UObject* Obj = *It;
				AActor* Actor = Cast<AActor>( Obj );

				if( !Obj->IsIn( GEditor->Level->GetOuter() ) )
					continue;

				if( Actor )
				{
					// STATIC MESH
					//
					// If the static mesh is using pTexture1 as part of it's original material set, we need to add an override to use pTexture2 instead

					if( Actor->DrawType == DT_StaticMesh && Actor->StaticMesh )
					{
						UStaticMesh* sm = Actor->StaticMesh;

						for( INT m = 0 ; m < sm->Materials.Num() ; ++m )
						{
							if( sm->Materials(m).Material == pTexture1 )
							{
								sm->Materials(m).Material = pTexture2;
							}
						}
					}
				}
			}

			OnNewPackage();
		}

		unguard;
	}
	void GetUsedList(int &n, UMaterial **&List)
	{
		guard(WDlgPackageBuilder::GetUsedList);

		FMemMark Mark(GMem);
		enum {MAX=16384};

		List = new(GMem,MAX)UMaterial*;

		TMap<UMaterial*,FUsedMaterialInfo> UsedMaterials;

		// ===
		// Build a list of textures which are used in the current map.
		// ===
		UsedMaterials.Empty();

		// ACTORS
		for( INT x = 0 ; x < GUnrealEd->Level->Actors.Num() ; ++x )
		{
			AActor* Actor  = Cast<AActor>( GUnrealEd->Level->Actors(x) );

			if( Actor )
			{
				// SPRITES
				if( Actor->Texture )
				{
					FUsedMaterialInfo* Info = UsedMaterials.Find(Actor->Texture);
					if( !Info )
						Info = &UsedMaterials.Set(Actor->Texture, FUsedMaterialInfo());
					Info->Sprites++;
				}

				// ACTORS
				if( !Cast<AStaticMeshActor>(Actor) )
				{
					for( INT y = 0 ; y < Actor->Skins.Num() ; ++y )
						if( Actor->Skins(y) )
						{
							FUsedMaterialInfo* Info = UsedMaterials.Find(Actor->Skins(y));
							if( !Info )
								Info = &UsedMaterials.Set(Actor->Skins(y), FUsedMaterialInfo());
							Info->Actors++;
						}
				}

				// BRUSHES
				if( GTBOptions->IUFilter&IUF_Brushes )
				{
					ABrush* Brush = Cast<ABrush>( Actor );
					if( Brush && Brush->Brush )
						for( INT y = 0 ; y < Brush->Brush->Polys->Element.Num() ; ++y )
						{
							FPoly* Poly = &(Brush->Brush->Polys->Element(y));
							if( Poly->Material )
							{
								FUsedMaterialInfo* Info = UsedMaterials.Find(Poly->Material);
								if( !Info )
									Info = &UsedMaterials.Set(Poly->Material, FUsedMaterialInfo());
								Info->BSP++;
							}
						}
				}

				// STATIC MESHES
				if( GTBOptions->IUFilter&IUF_StaticMeshes )
				{
					AStaticMeshActor* SM = Cast<AStaticMeshActor>( Actor );
					if( SM && SM->StaticMesh )
					{
						for( INT y = 0 ; y < SM->StaticMesh->Materials.Num() ; ++y )
							if( SM->StaticMesh->Materials(y).Material )
							{
								UMaterial* Material = SM->StaticMesh->Materials(y).Material;

								// Check to see if this material is overridden in the staticmeshactor.  If so, use that material instead.

								if( y < SM->Skins.Num() && SM->Skins(y) )
									Material = SM->Skins(y);

								FUsedMaterialInfo* Info = UsedMaterials.Find(Material);
								if( !Info )
									Info = &UsedMaterials.Set(Material, FUsedMaterialInfo());
								Info->StaticMeshes++;
							}
					}
				}

				// TERRAIN
				if( GTBOptions->IUFilter&IUF_Terrain )
				{
					ATerrainInfo* TI = Cast<ATerrainInfo>( Actor );
					if( TI )
					{
						for( INT y = 0 ; y < ARRAY_COUNT(TI->Layers) ; ++y )
						{
							if( TI->Layers[y].AlphaMap )
							{
								FUsedMaterialInfo* Info = UsedMaterials.Find(TI->Layers[y].AlphaMap);
								if( !Info )
									Info = &UsedMaterials.Set(TI->Layers[y].AlphaMap, FUsedMaterialInfo());
								Info->Terrains++;
							}
							if( TI->Layers[y].Texture )
							{
								FUsedMaterialInfo* Info = UsedMaterials.Find(TI->Layers[y].Texture);
								if( !Info )
									Info = &UsedMaterials.Set(TI->Layers[y].Texture, FUsedMaterialInfo());
								Info->Terrains++;
							}
						}
					}
				}

				// EMITTERS
				AEmitter* Emitter = Cast<AEmitter>( Actor );
				if( Emitter )
				{
					for( INT y = 0 ; y < Emitter->Emitters.Num() ; ++y )
					{
						UParticleEmitter* PE = Emitter->Emitters(y);
						if( PE->Texture )
						{
							FUsedMaterialInfo* Info = UsedMaterials.Find(PE->Texture);
							if( !Info )
								Info = &UsedMaterials.Set(PE->Texture, FUsedMaterialInfo());
							Info->EmitterTextures++;
						}
					}
				}
			}

			// end of for loop
		}

		// Copy the used materials into the real array
		n = 0;

		for( TMap<UMaterial*,FUsedMaterialInfo>::TIterator It(UsedMaterials);It;++It )
		{
			List[n] = It.Key();

			FString Package;

			if( !Cast<UPackage>(List[n]->GetOuter()->GetOuter()) )
				Package = List[n]->GetOuter()->GetName();
			else
				Package = List[n]->GetOuter()->GetOuter()->GetName();

			if (checkTextureList(Package) || lstTexture.GetCount() == 0)
				n++;
		}

		unguard;
	}
	void CreateInUseTextures(FString NewPackage)
	{
		guard(WDlgPackageBuilder::CreateInUseTextures);

		for (int i = 0; i < numMaterials; i++)
		{
			UMaterial* Material = MaterialList[i];

			DlgProgressBar->progressFinal.SetProgress((float)i / (float)numMaterials * 25, 100);
			DlgProgressBar->progressMaterial.SetProgress((float)i / (float)numMaterials * 50, 100);

			if (Material)
			{
				FString Package;
				FString Group;

				// get the group and package name

				if( !Cast<UPackage>(Material->GetOuter()->GetOuter()) )
				{
					Group = TEXT("");
					Package = Material->GetOuter()->GetName();
				}
				else
				{			
					Group = Material->GetOuter()->GetName();
					Package = Material->GetOuter()->GetOuter()->GetName();
				}

				// update the texture browser so that the
				// texture is cached

				TCHAR l_chCmd[1024];

				GTBOptions->NameFilter = Material->GetName();

				appSprintf( l_chCmd, TEXT("CAMERA UPDATE FLAGS=%d MISC2=%d REN=%d NAME=TextureBrowser PACKAGE=\"%s\" GROUP=\"%s\""),
					SHOW_StandardView | SHOW_ChildWindow | SHOW_NoFallbackMaterials | 0,
					0,
					REN_TexBrowser,
					*Package,
					TEXT("(All)") );

				GUnrealEd->Exec( l_chCmd );

				//MessageBox(NULL, *FString::Printf( TEXT("%s\n%s\n%s"), *NewPackage, *Package, Material->GetName() ), TEXT("ERROR"), 0);

				GUnrealEd->CurrentMaterial = Material;

				// duplicate the material to the new package
				if (OriginalCheck.IsChecked())
				{
					GUnrealEd->Exec( *FString::Printf( TEXT("TEXTURE DUPLICATE PACKAGE=%s GROUP=%s NAME=%s"),
					*NewPackage, *Group, *FString::Printf( TEXT("%s"), Material->GetName() ) ) );
				}
				else
				{
					GUnrealEd->Exec( *FString::Printf( TEXT("TEXTURE DUPLICATE PACKAGE=%s GROUP=%s NAME=%s"),
					*NewPackage, *Package, *FString::Printf( TEXT("%s"), Material->GetName() ) ) );
				}

				NewMaterialList[i] = GUnrealEd->CurrentMaterial;
			}

			OnNewPackage();
		}

		GTBOptions->NameFilter = TEXT("");

		unguard;
	}

	void ReplaceTextures()
	{
		guard(WDlgPackageBuilder::ReplaceTextures);

		UMaterial *pTexture1, *pTexture2;

		for (int i = 0; i < numMaterials; i++)
		{
			DlgProgressBar->progressFinal.SetProgress((float)i / (float)numMaterials * 25 + 25, 100);
			DlgProgressBar->progressMaterial.SetProgress((float)i / (float)numMaterials * 50 + 50, 100);

			pTexture1 = MaterialList[i];
			pTexture2 = NewMaterialList[i];

			//MessageBox(NULL, *FString::Printf( TEXT("%s\n%s"), pTexture1->GetFullName(), pTexture2->GetFullName() ), TEXT("ERROR"), 0);

			for( FObjectIterator It; It; ++It )
			{
				UObject* Obj = *It;
				AActor* Actor = Cast<AActor>( Obj );

				if( !Obj->IsIn( GEditor->Level->GetOuter() ) )
					continue;

				if( Actor )
				{
					// BSP

					UModel* M = Actor->IsA(ALevelInfo::StaticClass()) ? GUnrealEd->Level->Model : Actor->Brush;
					if( M )
					{
						//!!MAT
						M->Surfs.ModifyAllItems();
						for( TArray<FBspSurf>::TIterator ItS(M->Surfs); ItS; ++ItS )
							if( ItS->Material==pTexture1 )
								ItS->Material = pTexture2;
						if( M->Polys )
						{
							M->Polys->Element.ModifyAllItems();
							for( TArray<FPoly>::TIterator ItP(M->Polys->Element); ItP; ++ItP )
								if( ItP->Material==pTexture1 )
									ItP->Material = pTexture2;
						}
					}

					// SKINS ARRAY

					for( int x = 0 ; x < Actor->Skins.Num() ; ++x )
					{
						Actor->Modify();
						if( Actor->Skins(x) == pTexture1 )
							Actor->Skins(x) = pTexture2;
					}

					// EMITTERS

					if (Obj->IsA(AEmitter::StaticClass()) && pTexture1->IsA(UTexture::StaticClass()) && pTexture2->IsA(UTexture::StaticClass()))
					{
						AEmitter *emitter = (AEmitter *)Obj;
						UTexture *texture1 = (UTexture *)pTexture1;
						UTexture *texture2 = (UTexture *)pTexture2;

						for (int x = 0; x < emitter->Emitters.Num(); x++)
						{
							if (emitter->Emitters(x)->Texture == texture1)
								emitter->Emitters(x)->Texture = texture2;
						}
					}

					// SPRITES/MISC

					if( Actor->Texture == pTexture1 )
					{
						Actor->Modify();
						Actor->Texture = pTexture2;
					}

					// TERRAIN

					ATerrainInfo* TI = Cast<ATerrainInfo>( Actor );
					if( TI )
					{
						TI->Modify();
						for( INT x = 0 ; x < ARRAY_COUNT(TI->Layers) ; ++x )
							if( TI->Layers[x].Texture == pTexture1 )
								TI->Layers[x].Texture = pTexture2;
					}
				}
				else if ( Obj->IsA( UStaticMesh::StaticClass() ) )
				{
					INT i;
					UStaticMesh* StaticMesh = (UStaticMesh*) Obj;
					bool Rebuild = false;

					StaticMesh->RawTriangles.Load();

					for( i = 0; i < StaticMesh->RawTriangles.Num(); i++ )
					{
						UMaterial *CurrentMaterial = StaticMesh->RawTriangles(i).LegacyMaterial;

						if( CurrentMaterial == pTexture1 )
						{
							StaticMesh->RawTriangles(i).LegacyMaterial = pTexture2;
							Rebuild = true;
						}
					}
				}
			}

			OnNewPackage();
		}

		unguard;
	}
};

/*-----------------------------------------------------------------------------
The End.
-----------------------------------------------------------------------------*/