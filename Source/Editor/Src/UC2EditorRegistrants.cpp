/*=============================================================================
	UC2EditorRegistrants.cpp: static-link registration of the Editor package (uc2 port, M14).

	Epic always built Editor as a DLL, where every IMPLEMENT_CLASS registers itself when the DLL loads.
	Under __STATIC_LINK a class exists only once something calls its StaticClass(), which is what the
	generated AUTO_INITIALIZE_REGISTRANTS_<PKG> macros do from the launcher. Editor's macro
	(Inc/EditorClasses.h) cannot be used there: most Editor classes (the commandlets) are declared inside
	their .cpp files. This file makes the same calls, in the same order, through the per-class C accessors
	that IMPLEMENT_CLASS emits for this package only (UC2_STATIC_CLASS_ACCESSORS, Core/Inc/UnObjBas.h).
	The list mirrors AUTO_INITIALIZE_REGISTRANTS_EDITOR; keep the two in step.
=============================================================================*/

#include "EditorPrivate.h"

#if __STATIC_LINK

#define UC2_EDITOR_CLASSES(X) \
	X(UAnalyzeContentCommandlet) \
	X(UAnalyzeMeshesCommandlet) \
	X(UAnalyzeScriptCommandlet) \
	X(UAnimNotifyProps) \
	X(UBatchExportCommandlet) \
	X(UBitArray) \
	X(UBitMatrix) \
	X(UBrushBuilder) \
	X(UBulkDataExporterRDF) \
	X(UCheckLODActorsCommandlet) \
	X(UCheckStaticMeshes) \
	X(UCheckUnicodeCommandlet) \
	X(UClassExporterH) \
	X(UClassExporterUC) \
	X(UClassFactoryNew) \
	X(UClassFactoryUC) \
	X(UCompareIntCommandlet) \
	X(UCompileWeaponsCommandlet) \
	X(UConformCommandlet) \
	X(UConvertMaterialCommandlet) \
	X(UCutdownContentCommandlet) \
	X(UDXTConvertCommandlet) \
	X(UDataRipCommandlet) \
	X(UDumpIntCommandlet) \
	X(UEditInfo) \
	X(UEditorEngine) \
	X(UFindInstancesCommandlet) \
	X(UFindRefCommandlet) \
	X(UFontFactory) \
	X(UFontKerningCommandlet) \
	X(UFontUpdateCommandlet) \
	X(UInvalidMaterialsCommandlet) \
	X(ULevelExporterOBJ) \
	X(ULevelExporterSTL) \
	X(ULevelExporterT3D) \
	X(ULevelFactory) \
	X(ULevelFactoryNew) \
	X(UListAmbientSoundsCommandlet) \
	X(UListBrokenDistortionCommandlet) \
	X(UListDuplicateEmittersCommandlet) \
	X(UListUsedMaterialTypesCommandlet) \
	X(UMapConvertCommandlet) \
	X(UMasterCommandlet) \
	X(UMaterialExporterRDF) \
	X(UMaterialFactory) \
	X(UMergeIntCommandlet) \
	X(UModelExporterT3D) \
	X(UModelFactory) \
	X(UPackageFlagCommandlet) \
	X(UPkgCommandlet) \
	X(UPolysExporterOBJ) \
	X(UPolysExporterT3D) \
	X(UPolysFactory) \
	X(UPrefab) \
	X(UPrefabExporterT3D) \
	X(UPrefabFactory) \
	X(UPushbufferExporterRDF) \
	X(URearrangeIntCommandlet) \
	X(URebuildPathsCommandlet) \
	X(URedigestAnimationsCommandlet) \
	X(UReferenceCountCommandlet) \
	X(URemoveDuplicatesCommandlet) \
	X(URemoveTwoSidedCommandlet) \
	X(UResavePackageCommandlet) \
	X(USoundExporterWAV) \
	X(USoundFactory) \
	X(UStaticMeshCalcCacheCommandlet) \
	X(UStaticMeshExporterT3D) \
	X(UStaticMeshFactory) \
	X(UStripSourceCommandlet) \
	X(UTextBufferExporterTXT) \
	X(UTextureExporterBMP) \
	X(UTextureExporterDDS) \
	X(UTextureExporterPCX) \
	X(UTextureExporterTGA) \
	X(UTextureFactory) \
	X(UTextureFactoryNew) \
	X(UTransBuffer) \
	X(UTransactor) \
	X(UTrueTypeFontFactory) \
	X(UUpdateUModCommandlet) \
	X(UMakeCommandlet) \

#define UC2_DECLARE_ACCESSOR(TClass) extern "C" UClass* UC2StaticClass##TClass();
UC2_EDITOR_CLASSES(UC2_DECLARE_ACCESSOR)
#undef UC2_DECLARE_ACCESSOR

EDITOR_API void UC2InitializeEditorRegistrants( INT& Lookup )
{
	UC2StaticClassUAnalyzeContentCommandlet();
	UC2StaticClassUAnalyzeMeshesCommandlet();
	UC2StaticClassUAnalyzeScriptCommandlet();
	UC2StaticClassUAnimNotifyProps();
	UC2StaticClassUBatchExportCommandlet();
	UC2StaticClassUBitArray();
	UC2StaticClassUBitMatrix();
	UC2StaticClassUBrushBuilder();
	GNativeLookupFuncs[Lookup++] = &FindEditorUBrushBuilderNative;
	UC2StaticClassUBulkDataExporterRDF();
	UC2StaticClassUCheckLODActorsCommandlet();
	UC2StaticClassUCheckStaticMeshes();
	UC2StaticClassUCheckUnicodeCommandlet();
	UC2StaticClassUClassExporterH();
	UC2StaticClassUClassExporterUC();
	UC2StaticClassUClassFactoryNew();
	UC2StaticClassUClassFactoryUC();
	UC2StaticClassUCompareIntCommandlet();
	UC2StaticClassUCompileWeaponsCommandlet();
	UC2StaticClassUConformCommandlet();
	UC2StaticClassUConvertMaterialCommandlet();
	UC2StaticClassUCutdownContentCommandlet();
	UC2StaticClassUDXTConvertCommandlet();
	UC2StaticClassUDataRipCommandlet();
	UC2StaticClassUDumpIntCommandlet();
	UC2StaticClassUEditInfo();
	UC2StaticClassUEditorEngine();
	UC2StaticClassUFindInstancesCommandlet();
	UC2StaticClassUFindRefCommandlet();
	UC2StaticClassUFontFactory();
	UC2StaticClassUFontKerningCommandlet();
	UC2StaticClassUFontUpdateCommandlet();
	UC2StaticClassUInvalidMaterialsCommandlet();
	UC2StaticClassULevelExporterOBJ();
	UC2StaticClassULevelExporterSTL();
	UC2StaticClassULevelExporterT3D();
	UC2StaticClassULevelFactory();
	UC2StaticClassULevelFactoryNew();
	UC2StaticClassUListAmbientSoundsCommandlet();
	UC2StaticClassUListBrokenDistortionCommandlet();
	UC2StaticClassUListDuplicateEmittersCommandlet();
	UC2StaticClassUListUsedMaterialTypesCommandlet();
	UC2StaticClassUMapConvertCommandlet();
	UC2StaticClassUMasterCommandlet();
	UC2StaticClassUMaterialExporterRDF();
	UC2StaticClassUMaterialFactory();
	GNativeLookupFuncs[Lookup++] = &FindEditorUMaterialFactoryNative;
	UC2StaticClassUMergeIntCommandlet();
	UC2StaticClassUModelExporterT3D();
	UC2StaticClassUModelFactory();
	UC2StaticClassUPackageFlagCommandlet();
	UC2StaticClassUPkgCommandlet();
	UC2StaticClassUPolysExporterOBJ();
	UC2StaticClassUPolysExporterT3D();
	UC2StaticClassUPolysFactory();
	UC2StaticClassUPrefab();
	UC2StaticClassUPrefabExporterT3D();
	UC2StaticClassUPrefabFactory();
	UC2StaticClassUPushbufferExporterRDF();
	UC2StaticClassURearrangeIntCommandlet();
	UC2StaticClassURebuildPathsCommandlet();
	UC2StaticClassURedigestAnimationsCommandlet();
	UC2StaticClassUReferenceCountCommandlet();
	UC2StaticClassURemoveDuplicatesCommandlet();
	UC2StaticClassURemoveTwoSidedCommandlet();
	UC2StaticClassUResavePackageCommandlet();
	UC2StaticClassUSoundExporterWAV();
	UC2StaticClassUSoundFactory();
	UC2StaticClassUStaticMeshCalcCacheCommandlet();
	UC2StaticClassUStaticMeshExporterT3D();
	UC2StaticClassUStaticMeshFactory();
	UC2StaticClassUStripSourceCommandlet();
	UC2StaticClassUTextBufferExporterTXT();
	UC2StaticClassUTextureExporterBMP();
	UC2StaticClassUTextureExporterDDS();
	UC2StaticClassUTextureExporterPCX();
	UC2StaticClassUTextureExporterTGA();
	UC2StaticClassUTextureFactory();
	UC2StaticClassUTextureFactoryNew();
	UC2StaticClassUTransBuffer();
	UC2StaticClassUTransactor();
	UC2StaticClassUTrueTypeFontFactory();
	UC2StaticClassUUpdateUModCommandlet();
	UC2StaticClassUMakeCommandlet();
}

#endif // __STATIC_LINK
