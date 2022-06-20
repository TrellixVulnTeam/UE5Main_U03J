// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "View/MVVMView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"

#include "MVVMBlueprintView.generated.h"

class UWidget;
class UWidgetBlueprint;

/**
 * 
 */
UCLASS(Within=MVVMWidgetBlueprintExtension_View)
class MODELVIEWVIEWMODELBLUEPRINT_API UMVVMBlueprintView : public UObject
{
	GENERATED_BODY()

public:
	FMVVMBlueprintViewModelContext* FindViewModel(FGuid ViewModelId);
	const FMVVMBlueprintViewModelContext* FindViewModel(FGuid ViewModelId) const;

	void AddViewModel(const FMVVMBlueprintViewModelContext& NewContext);
	void RemoveViewModel(FGuid ViewModelId);
	void RemoveViewModels(const TArrayView<FGuid> ViewModelIds);
	void SetViewModels(const TArray<FMVVMBlueprintViewModelContext>& ViewModelContexts);

	const TArrayView<const FMVVMBlueprintViewModelContext> GetViewModels() const
	{
		return AvailableViewModels; 
	}

	const FMVVMBlueprintViewBinding* FindBinding(const UWidget* Widget, const FProperty* Property) const;
	FMVVMBlueprintViewBinding* FindBinding(const UWidget* Widget, const FProperty* Property);
	const FMVVMBlueprintViewBinding* FindBinding(FName WidgetName, FMVVMBindingName BindingName) const;
	FMVVMBlueprintViewBinding* FindBinding(FName WidgetName, FMVVMBindingName BindingName);

	void RemoveBinding(const FMVVMBlueprintViewBinding* Binding);
	void RemoveBindingAt(int32 Index);

	FMVVMBlueprintViewBinding& AddBinding(const UWidget* Widget, const FProperty* Property);
	FMVVMBlueprintViewBinding& AddDefaultBinding();

	int32 GetNumBindings() const
	{
		return Bindings.Num();
	}

	FMVVMBlueprintViewBinding* GetBindingAt(int32 Index);
	const FMVVMBlueprintViewBinding* GetBindingAt(int32 Index) const;

	TArrayView<FMVVMBlueprintViewBinding> GetBindings()
	{
		return Bindings;
	}

	const TArrayView<const FMVVMBlueprintViewBinding> GetBindings() const
	{
		return Bindings;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void WidgetRenamed(FName OldObjectName, FName NewObjectName);
#endif

	DECLARE_EVENT(UMVVMBlueprintView, FOnBindingsUpdated);
	FOnBindingsUpdated OnBindingsUpdated;

	DECLARE_EVENT(UMVVMBlueprintView, FOnViewModelsUpdated);
	FOnViewModelsUpdated OnViewModelsUpdated;

private:
	UPROPERTY(EditAnywhere, Category = "MVVM")
	TArray<FMVVMBlueprintViewBinding> Bindings;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	TArray<FMVVMBlueprintViewModelContext> AvailableViewModels;
};
