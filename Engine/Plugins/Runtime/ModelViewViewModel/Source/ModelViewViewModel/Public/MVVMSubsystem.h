// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"
#include "Templates/ValueOrError.h"

#include "Types/MVVMBindingName.h"
#include "Types/MVVMFieldVariant.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMViewModelCollection.h"

#include "MVVMSubsystem.generated.h"

class UMVVMView;
class UMVVMViewModelBase;
class UUserWidget;
class UWidget;
class UWidgetTree;

/** */
UCLASS()
class MODELVIEWVIEWMODEL_API UMVVMSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UEngineSubsystem interface

public:
	UFUNCTION(BlueprintCallable, Category="MVVM")
	UMVVMView* GetViewFromUserWidget(const UUserWidget* UserWidget) const;

	UFUNCTION(BlueprintCallable, Category="MVVM")
	bool IsViewModelValueValidForSourceBinding(const UMVVMViewModelBase* ViewModel, FMVVMBindingName ViewModelPropertyOrFunctionName) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool IsViewModelValueValidForDestinationBinding(const UMVVMViewModelBase* ViewModel, FMVVMBindingName ViewModelPropertyOrFunctionName) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool IsViewValueValidForSourceBinding(const UWidget* View, FMVVMBindingName ViewPropertyOrFunctionName) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool IsViewValueValidForDestinationBinding(const UWidget* View, FMVVMBindingName ViewPropertyOrFunctionName) const;

	UFUNCTION(BlueprintCallable, Category = "MVVM")
	bool DoesWidgetTreeContainedWidget(const UWidgetTree* WidgetTree, const UWidget* ViewWidget) const;

	/** Returns the list of all the binding that are available for the ViewModel. */
	UFUNCTION(BlueprintCallable, Category = "MVVM")
	TArray<FMVVMAvailableBinding> GetViewModelAvailableBindings(TSubclassOf<UMVVMViewModelBase> ViewModelClass) const;

	/** Returns the list of all the binding that are available for the Widget. */
	UFUNCTION(BlueprintCallable, Category = "MVVM")
	TArray<FMVVMAvailableBinding> GetWidgetAvailableBindings(TSubclassOf<UWidget> WidgetClass) const;

public:
	UFUNCTION(BlueprintCallable, Category = "MVVM")
	UMVVMViewModelCollectionObject* GetGlobalViewModelCollection() const
	{
		return GlobalViewModelCollection;
	}

public:

	struct FConstDirectionalBindingArgs
	{
		UE::MVVM::FMVVMConstFieldVariant SourceBinding;
		UE::MVVM::FMVVMConstFieldVariant DestinationBinding;
		const UFunction* ConversionFunction = nullptr;
	};

	struct FDirectionalBindingArgs
	{
		UE::MVVM::FMVVMFieldVariant SourceBinding;
		UE::MVVM::FMVVMFieldVariant DestinationBinding;
		UFunction* ConversionFunction = nullptr;

		FConstDirectionalBindingArgs ToConst() const
		{
			FConstDirectionalBindingArgs ConstArgs;
			ConstArgs.SourceBinding = SourceBinding;
			ConstArgs.DestinationBinding = DestinationBinding;
			ConstArgs.ConversionFunction = ConversionFunction;
			return MoveTemp(ConstArgs);
		}
	};

	struct FBindingArgs
	{
		EMVVMBindingMode Mode = EMVVMBindingMode::OneWayToDestination;
		FDirectionalBindingArgs ForwardArgs;
		FDirectionalBindingArgs BackwardArgs;
	};

	UE_NODISCARD TValueOrError<bool, FString> IsBindingValid(FConstDirectionalBindingArgs Args) const;
	UE_NODISCARD TValueOrError<bool, FString> IsBindingValid(FDirectionalBindingArgs Args) const;
	UE_NODISCARD TValueOrError<bool, FString> IsBindingValid(FBindingArgs Args) const;

private:
	UPROPERTY(Transient)
	UMVVMViewModelCollectionObject* GlobalViewModelCollection;
};
