// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "DMXLibraryFromMVRFactory.generated.h"

struct FDMXMVRGeneralSceneDescription;
class FDMXMVRUnzip;
class UDMXImportGDTF;
class UDMXLibrary;



UCLASS()
class DMXEDITOR_API UDMXLibraryFromMVRFactory 
	: public UFactory
{
	GENERATED_BODY()

public:
	UDMXLibraryFromMVRFactory();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* Parent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled);
	virtual bool FactoryCanImport(const FString& Filename) override;
	//~ End UFactory Interface	

	/** File extention for MVR files */
	static const FName MVRFileExtension;

	/** File extention for GDTF files */
	static const FName GDTFFileExtension;

private:
	/** Creates a DMX Library asset. Returns nullptr if the library could not be created */
	UDMXLibrary* CreateDMXLibraryAsset(UObject* Parent, EObjectFlags Flags, const FString& InFilename);

	/** Creates GDTF assets from the MVR */
	TArray<UDMXImportGDTF*> CreateGDTFAssets(UObject* Parent, EObjectFlags Flags, const TSharedRef<FDMXMVRUnzip>& MVRUnzip, const FDMXMVRGeneralSceneDescription& GeneralSceneDescription);

	/** Initializes the DMX Library from the General Scene Description and GDTF assets */
	void InitDMXLibrary(UDMXLibrary* DMXLibrary, const TArray<UDMXImportGDTF*>& GDTFAssets, const FDMXMVRGeneralSceneDescription& GeneralSceneDescription) const;

};
