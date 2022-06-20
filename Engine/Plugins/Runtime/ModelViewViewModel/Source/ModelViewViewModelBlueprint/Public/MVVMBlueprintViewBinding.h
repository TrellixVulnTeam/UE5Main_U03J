// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"

#include "MVVMBlueprintViewBinding.generated.h"

class UMVVMBlueprintView;

/**
*
*/
USTRUCT(BlueprintType)
struct FMVVMBlueprintViewConversionPath
{
	GENERATED_BODY()

	/** The Conversion function when converting the value from the destination to the source. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay)
	FString DestinationToSourceFunctionPath;

	/** The Conversion function when converting the value from the source to the destination. */
	UPROPERTY(EditAnywhere, Category = "MVVM", AdvancedDisplay)
	FString SourceToDestinationFunctionPath;
};

/**
*
*/
USTRUCT(BlueprintType)
struct FMVVMBlueprintViewBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMViewModelPropertyPath ViewModelPath;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMWidgetPropertyPath WidgetPath;

	/** */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	EMVVMBindingMode BindingType = EMVVMBindingMode::OneWayToDestination;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	EMVVMViewBindingUpdateMode UpdateMode = EMVVMViewBindingUpdateMode::Immediate;

	/** */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMBlueprintViewConversionPath Conversion;

	/** */
	UPROPERTY(VisibleAnywhere, Category = "MVVM", Transient)
	TArray<FText> Errors;

	/** Whether the binding is enabled or disabled by default. The instance may enable the binding at runtime. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	bool bEnabled = true;

	/** The binding is visible in the editor, but is not compiled and cannot be used at runtime. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	bool bCompile = true;

	/** 
	 * Get a string that identifies this binding. 
	 * This is of the form: ViewModel.Property -> Widget.Property
	 */
	FString GetNameString(const UMVVMBlueprintView* View) const;
};
