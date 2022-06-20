// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AssetTypeCategories.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "TickableEditorObject.h"
#include "IDetailsView.h"

#include "InputEditorModule.generated.h"

////////////////////////////////////////////////////////////////////
// FInputEditorModule

class FInputEditorModule : public IModuleInterface, public FTickableEditorObject
{
public:

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface interface

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FInputEditorModule, STATGROUP_Tickables); }
	// End FTickableEditorObject interface

	static EAssetTypeCategories::Type GetInputAssetsCategory() { return InputAssetsCategory; }
	
private:
	void RegisterAssetTypeActions(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	}

	static EAssetTypeCategories::Type InputAssetsCategory;
	
	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;
};

////////////////////////////////////////////////////////////////////
// Asset factories

UCLASS()
class INPUTEDITOR_API UInputMappingContext_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class INPUTEDITOR_API UInputAction_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class INPUTEDITOR_API UPlayerMappableInputConfig_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

// TODO: Add trigger/modifier factories and hook up RegisterAssetTypeActions type construction.
//
//UCLASS()
//class INPUTEDITOR_API UInputTrigger_Factory : public UBlueprintFactory
//{
//	GENERATED_UCLASS_BODY()
//};
//
//UCLASS()
//class INPUTEDITOR_API UInputModifier_Factory : public UBlueprintFactory
//{
//	GENERATED_UCLASS_BODY()
//
//	UPROPERTY(EditAnywhere, Category = DataAsset)
//	TSubclassOf<UDataAsset> DataAssetClass;
//};
