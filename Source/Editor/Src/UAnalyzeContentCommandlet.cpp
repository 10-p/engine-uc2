/*=============================================================================
	UAnalyzeContentCommandlet.cpp: Analyze game content
	Copyright 2000 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel

=============================================================================*/

#include "EditorPrivate.h"
//#include <stdlib.h>

/*-----------------------------------------------------------------------------
	UAnalyzeContentCommandlet
-----------------------------------------------------------------------------*/

struct FTextureStats
{
	TArray<FString> Textures;
	TArray<FString> TexturesPAL8;
	TArray<FString> TexturesDXT1;
	TArray<FString> TexturesDXT3;
	TArray<FString> TexturesDXT5;
	TArray<FString> TexturesRGBA;
	TArray<FString> TexturesNoMips;

	DWORD	Bytes;
	DWORD	BytesPAL8;
	DWORD	BytesDXT1;
	DWORD	BytesDXT3;
	DWORD	BytesDXT5;
	DWORD	BytesRGBA;
	DWORD	BytesNoMips;

	DWORD	Amount;
	DWORD	AmountPAL8;
	DWORD	AmountDXT1;
	DWORD	AmountDXT3;
	DWORD	AmountDXT5;
	DWORD	AmountRGBA;
	DWORD	AmountNoMips;

	DWORD	MaxUSize;
	DWORD	MaxUSizePAL8;
	DWORD	MaxUSizeDXT1;
	DWORD	MaxUSizeDXT3;
	DWORD	MaxUSizeDXT5;
	DWORD	MaxUSizeRGBA;
	DWORD	MaxUSizeNoMips;

	DWORD	MaxVSize;
	DWORD	MaxVSizePAL8;
	DWORD	MaxVSizeDXT1;
	DWORD	MaxVSizeDXT3;
	DWORD	MaxVSizeDXT5;
	DWORD	MaxVSizeRGBA;
	DWORD	MaxVSizeNoMips;

	DWORD	MinUSize;
	DWORD	MinUSizePAL8;
	DWORD	MinUSizeDXT1;
	DWORD	MinUSizeDXT3;
	DWORD	MinUSizeDXT5;
	DWORD	MinUSizeRGBA;
	DWORD	MinUSizeNoMips;

	DWORD	MinVSize;
	DWORD	MinVSizePAL8;
	DWORD	MinVSizeDXT1;
	DWORD	MinVSizeDXT3;
	DWORD	MinVSizeDXT5;
	DWORD	MinVSizeRGBA;
	DWORD	MinVSizeNoMips;

	DWORD	AccumUSize;
	DWORD	AccumUSizePAL8;
	DWORD	AccumUSizeDXT1;
	DWORD	AccumUSizeDXT3;
	DWORD	AccumUSizeDXT5;
	DWORD	AccumUSizeRGBA;
	DWORD	AccumUSizeNoMips;

	DWORD	AccumVSize;
	DWORD	AccumVSizePAL8;
	DWORD	AccumVSizeDXT1;
	DWORD	AccumVSizeDXT3;
	DWORD	AccumVSizeDXT5;
	DWORD	AccumVSizeRGBA;
	DWORD	AccumVSizeNoMips;
};

struct FStaticMeshStats
{
	TArray<FString> StaticMeshes;

	DWORD	Sections;
	DWORD	Triangles;

	DWORD	Amount;
	DWORD dwTotalUsed;

    //scion rwd - static mesh instance checking
    DWORD   InstanceCnt;
    DWORD   InstanceSize;
};

struct FSoundStats
{
	TArray<FString> Sounds;

	DWORD Bytes;
	DWORD Bytes8bit;
	DWORD Bytes16bit;

	DWORD Amount;
};

class UAnalyzeContentCommandlet : public UCommandlet
{
	DECLARE_CLASS(UAnalyzeContentCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UAnalyzeContentCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}
	INT Main( const TCHAR* Parms )
	{
		guard(UAnalyzeContentCommandlet::Main);
		FString WildCard;
		if( !ParseToken(Parms,WildCard,0) )
			appErrorf(TEXT("Package search mask not specified."));

		GLazyLoad = 0;

		UClass* TextureClass = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("Texture") );
		FTextureStats TextureStats;
		appMemzero( &TextureStats, sizeof(TextureStats));

		UClass* SoundClass = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("Sound") );
		FSoundStats SoundStats;
		appMemzero( &SoundStats, sizeof(SoundStats));

		UClass* StaticMeshClass = FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("StaticMesh") );
		FStaticMeshStats StaticMeshStats;
		appMemzero( &StaticMeshStats, sizeof(StaticMeshStats));

		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 0 );
#if 0
		for (INT i=0; i<FilesFound.Num(); i++)
#else
		INT i=0;
#endif
		{
			FString Pkg = FilesFound(i);
			warnf( TEXT("\nAnalyzing package %s...\n"), *Pkg );

			UObject* Package = NULL;
			Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);
		
			if (Package != NULL)
			{
				for( TObjectIterator<UObject> It; It; ++It )
				{
					if( It->IsA(TextureClass) )//&& It->IsIn(Package) )
					{
						UTexture* Texture	= (UTexture*) *It;

						if( !Texture->Mips.Num() )
						{
							warnf( TEXT("No mips found for texture %s"), Texture->GetPathName() );
							continue;
						}

						DWORD NumMips	= Texture->Mips.Num();
						DWORD Size		= Texture->Mips(0).USize * Texture->Mips(0).VSize;
						DWORD USize		= Texture->Mips(0).USize;
						DWORD VSize		= Texture->Mips(0).VSize;

						new(TextureStats.Textures)FString(FString::Printf(TEXT("%ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));

						if( NumMips == 1 )
						{
							//TextureStats.BytesRGBA		+= Size * 4;
							TextureStats.AmountNoMips	   += 1;						
							TextureStats.MaxUSizeNoMips		= Max( TextureStats.MaxUSizeNoMips, USize );
							TextureStats.MaxVSizeNoMips		= Max( TextureStats.MaxVSizeNoMips, VSize );
							TextureStats.MinUSizeNoMips		= Min( TextureStats.MinUSizeNoMips, USize );
							TextureStats.MinVSizeNoMips		= Min( TextureStats.MinVSizeNoMips, VSize );
							TextureStats.AccumUSizeNoMips  += USize;
							TextureStats.AccumVSizeNoMips  += VSize;
							new(TextureStats.TexturesNoMips)FString(FString::Printf(TEXT("%ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
						}

						switch( Texture->Format )
						{
						case TEXF_RGBA8:
							TextureStats.Bytes			+= Size * 4;
							TextureStats.BytesRGBA		+= Size * 4;
							TextureStats.AmountRGBA		+= 1;						
							TextureStats.MaxUSizeRGBA	= Max( TextureStats.MaxUSizeRGBA, USize );
							TextureStats.MaxVSizeRGBA	= Max( TextureStats.MaxVSizeRGBA, VSize );
							TextureStats.MinUSizeRGBA	= Min( TextureStats.MinUSizeRGBA, USize );
							TextureStats.MinVSizeRGBA	= Min( TextureStats.MinVSizeRGBA, VSize );
							TextureStats.AccumUSizeRGBA += USize;
							TextureStats.AccumVSizeRGBA += VSize;
							new(TextureStats.TexturesRGBA)FString(FString::Printf(TEXT("RGBA %ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
							break;
						case TEXF_P8:
							TextureStats.Bytes			+= Size * 4;
							TextureStats.BytesPAL8		+= Size * 4;
							TextureStats.AmountPAL8		+= 1;						
							TextureStats.MaxUSizePAL8	= Max( TextureStats.MaxUSizePAL8, USize );
							TextureStats.MaxVSizePAL8	= Max( TextureStats.MaxVSizePAL8, VSize );
							TextureStats.MinUSizePAL8	= Min( TextureStats.MinUSizePAL8, USize );
							TextureStats.MinVSizePAL8	= Min( TextureStats.MinVSizePAL8, VSize );
							TextureStats.AccumUSizePAL8 += USize;
							TextureStats.AccumVSizePAL8 += VSize;
							new(TextureStats.TexturesPAL8)FString(FString::Printf(TEXT("PAL8 %ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
							break;
						case TEXF_DXT1:
							TextureStats.Bytes			+= Size / 2;
							TextureStats.BytesDXT1		+= Size / 2;
							TextureStats.AmountDXT1		+= 1;						
							TextureStats.MaxUSizeDXT1	= Max( TextureStats.MaxUSizeDXT1, USize );
							TextureStats.MaxVSizeDXT1	= Max( TextureStats.MaxVSizeDXT1, VSize );
							TextureStats.MinUSizeDXT1	= Min( TextureStats.MinUSizeDXT1, USize );
							TextureStats.MinVSizeDXT1	= Min( TextureStats.MinVSizeDXT1, VSize );
							TextureStats.AccumUSizeDXT1 += USize;
							TextureStats.AccumVSizeDXT1 += VSize;
							new(TextureStats.TexturesDXT1)FString(FString::Printf(TEXT("DXT1 %ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
							break;
						case TEXF_DXT3:
							TextureStats.Bytes			+= Size;
							TextureStats.BytesDXT3		+= Size;
							TextureStats.AmountDXT3		+= 1;						
							TextureStats.MaxUSizeDXT3	= Max( TextureStats.MaxUSizeDXT3, USize );
							TextureStats.MaxVSizeDXT3	= Max( TextureStats.MaxVSizeDXT3, VSize );
							TextureStats.MinUSizeDXT3	= Min( TextureStats.MinUSizeDXT3, USize );
							TextureStats.MinVSizeDXT3	= Min( TextureStats.MinVSizeDXT3, VSize );
							TextureStats.AccumUSizeDXT3 += USize;
							TextureStats.AccumVSizeDXT3 += VSize;
							new(TextureStats.TexturesDXT3)FString(FString::Printf(TEXT("DXT3 %ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
							break;
						case TEXF_DXT5:
							TextureStats.Bytes			+= Size;
							TextureStats.BytesDXT5		+= Size;
							TextureStats.AmountDXT5		+= 1;						
							TextureStats.MaxUSizeDXT5	= Max( TextureStats.MaxUSizeDXT5, USize );
							TextureStats.MaxVSizeDXT5	= Max( TextureStats.MaxVSizeDXT5, VSize );
							TextureStats.MinUSizeDXT5	= Min( TextureStats.MinUSizeDXT5, USize );
							TextureStats.MinVSizeDXT5	= Min( TextureStats.MinVSizeDXT5, VSize );
							TextureStats.AccumUSizeDXT5 += USize;
							TextureStats.AccumVSizeDXT5 += VSize;
							new(TextureStats.TexturesDXT5)FString(FString::Printf(TEXT("DXT5 %ix%i [%i] %s"), USize, VSize, NumMips, Texture->GetPathName()));
							break;
						default:
							break;
						}

						TextureStats.Amount		+= 1;						
						TextureStats.MaxUSize	= Max( TextureStats.MaxUSize, USize );
						TextureStats.MaxVSize	= Max( TextureStats.MaxVSize, VSize );
						TextureStats.MinUSize	= Min( TextureStats.MinUSize, USize );
						TextureStats.MinVSize	= Min( TextureStats.MinVSize, VSize );
						TextureStats.AccumUSize += USize;
						TextureStats.AccumVSize += VSize;
					}
					
					if( It->IsA(StaticMeshClass) )//&& It->IsIn(Package) )
					{
						UStaticMesh* StaticMesh	= (UStaticMesh*) *It;

						StaticMesh->RawTriangles.Load();

						StaticMeshStats.Amount		+= 1;
						StaticMeshStats.Sections	+= StaticMesh->Sections.Num();
						StaticMeshStats.Triangles	+= StaticMesh->RawTriangles.Num();

						StaticMesh->RawTriangles.Unload();

						//scion jg -- Figure out much memory this mesh consumes
						FArchiveCountMem Count( StaticMesh );
						StaticMeshStats.dwTotalUsed += Count.GetNum();

						new(StaticMeshStats.StaticMeshes)FString(FString::Printf(TEXT("%8.d %9.d  %s"),
							StaticMeshStats.Triangles, Count.GetMax(),
							StaticMesh->GetPathName()));
					}
                    //scion rwd - static mesh instance checking
                    if (It->IsA(UStaticMeshInstance::StaticClass()))
                    {
                        UStaticMeshInstance *MeshInst = (UStaticMeshInstance*)*It;
                        StaticMeshStats.InstanceCnt++;
                        FArchiveCountMem Count(MeshInst);
                        StaticMeshStats.InstanceSize += Count.GetNum();
                    }

					if( It->IsA(SoundClass) )//&& It->IsIn(Package) )
					{
						USound* Sound = (USound*) *It;

						Sound->Data.Load();

						SoundStats.Amount		+= 1;
						SoundStats.Bytes		+= Sound->Data.Num();
						SoundStats.Bytes8bit	+= Sound->Data.Num(); 
						SoundStats.Bytes16bit	+= Sound->Data.Num();
						
						new(SoundStats.Sounds)FString(FString::Printf(TEXT("%i %s"), 0, Sound->GetPathName()));

						Sound->Data.Unload();
					}
				}

				UObject::CollectGarbage(RF_Native);
			}

			warnf( TEXT("\nTexture Stats") );
			warnf( TEXT("") );
			warnf( TEXT("%5.d  RGBA textures consuming %11.d KByte in video memory"), TextureStats.AmountRGBA, TextureStats.BytesRGBA / 1024);
			warnf( TEXT("%5.d  PAL8 textures consuming %11.d KByte in video memory"), TextureStats.AmountPAL8, TextureStats.BytesPAL8 / 1024);
			warnf( TEXT("%5.d  DXT1 textures consuming %11.d KByte in video memory"), TextureStats.AmountDXT1, TextureStats.BytesDXT1 / 1024);
			warnf( TEXT("%5.d  DXT3 textures consuming %11.d KByte in video memory"), TextureStats.AmountDXT3, TextureStats.BytesDXT3 / 1024);
			warnf( TEXT("%5.d  DXT5 textures consuming %11.d KByte in video memory"), TextureStats.AmountDXT5, TextureStats.BytesDXT5 / 1024);
			warnf( TEXT("") );
			warnf( TEXT("%5.d total textures consuming %11.d KByte in video memory"), TextureStats.Amount, TextureStats.Bytes / 1024);

			warnf( TEXT("") );
			warnf( TEXT("") );

			warnf( TEXT("StaticMesh Stats") );
			warnf( TEXT("") );
			warnf( TEXT("%5.d static meshes with a total of %d triangles"), StaticMeshStats.Amount, StaticMeshStats.Triangles );
			if( StaticMeshStats.Amount )
			{
				warnf( TEXT("%5.d average triangles per mesh"), StaticMeshStats.Triangles / StaticMeshStats.Amount );
				warnf( TEXT("%5.d average sections per mesh"), StaticMeshStats.Sections / StaticMeshStats.Amount );
			}
            //scion rwd - static mesh instance checking
            warnf( TEXT("%5.d static mesh instances using %d KByte mem"), StaticMeshStats.InstanceCnt, StaticMeshStats.InstanceSize/1024);
			warnf( TEXT("") );
            //scion rwd - verbose check
            if (ParseParam(appCmdLine(),TEXT("verbose")))
            {
    			warnf( TEXT("Tris     MemUsage   Name") );
    			//scion jg -- Report each static mesh in the package and it's poly count
    			for (INT nIndex = 0; nIndex < StaticMeshStats.StaticMeshes.Num();
    				nIndex++)
    			{
    				GWarn->Log( StaticMeshStats.StaticMeshes(nIndex) );
    			}
            }
            //scion rwd - static mesh instance checking
			warnf( TEXT("        %10.d  KByte Total"),(StaticMeshStats.InstanceSize+StaticMeshStats.dwTotalUsed)/1024 );
			warnf( TEXT("") );
			warnf( TEXT("") );

			warnf( TEXT("Sound Stats") );
			warnf( TEXT("") );
			warnf( TEXT("%5.d sounds consuming a total of %8.d KByte in memory"), SoundStats.Amount, SoundStats.Bytes / 1024 );
			if( SoundStats.Amount )
			{
				warnf( TEXT("%5.d average KByte per sound"), SoundStats.Bytes / SoundStats.Amount / 1024 );
			}
			warnf( TEXT("") );
			warnf( TEXT("") );

			if( ParseCommand( &Parms, TEXT("LISTALL") ))
			{
				warnf( TEXT("All textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.Textures.Num(); i++)
					warnf( TEXT("%s"), TextureStats.Textures(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
			if( ParseCommand( &Parms, TEXT("LISTNOMIPS") ))
			{
				warnf( TEXT("Textures without miplevels") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesNoMips.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesNoMips(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}			
			if( ParseCommand( &Parms, TEXT("LISTRGBA") ))
			{
				warnf( TEXT("RGBA textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesRGBA.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesRGBA(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
			if( ParseCommand( &Parms, TEXT("LISTPAL8") ))
			{
				warnf( TEXT("PAL8 textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesPAL8.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesPAL8(i) );
							warnf( TEXT("") );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
			if( ParseCommand( &Parms, TEXT("LISTDXT1") ))
			{
				warnf( TEXT("DXT1 textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesDXT1.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesDXT1(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
			if( ParseCommand( &Parms, TEXT("LISTDXT3") ))
			{
				warnf( TEXT("DXT3 textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesDXT3.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesDXT3(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
			if( ParseCommand( &Parms, TEXT("LISTDXT5") ))
			{
				warnf( TEXT("DXT5 textures") );
				warnf( TEXT("") );
				for (INT i=0; i<TextureStats.TexturesDXT5.Num(); i++)
					warnf( TEXT("%s"), TextureStats.TexturesDXT5(i) );
				warnf( TEXT("") );
				warnf( TEXT("") );
			}
		}

		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UAnalyzeContentCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
