﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraValidationRules.h"

#include "NiagaraClipboard.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSettings.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackEmitterSettingsGroup.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackRendererItem.h"
#include "ViewModels/Stack/NiagaraStackSystemPropertiesItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"

#include "AssetToolsModule.h"
#include "Toolkits/AssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "NiagaraValidationRules"

template<typename T>
TArray<T*> GetStackEntries(UNiagaraStackViewModel* StackViewModel, bool bRefresh = false)
{
	TArray<T*> Results;
	TArray<UNiagaraStackEntry*> EntriesToCheck;
	if (UNiagaraStackEntry* RootEntry = StackViewModel->GetRootEntry())
	{
		if (bRefresh)
		{
			RootEntry->RefreshChildren();
		}
		RootEntry->GetUnfilteredChildren(EntriesToCheck);
	}
	while (EntriesToCheck.Num() > 0)
	{
		UNiagaraStackEntry* Entry = EntriesToCheck.Pop();
		if (T* ItemToCheck = Cast<T>(Entry))
		{
			Results.Add(ItemToCheck);
		}
		Entry->GetUnfilteredChildren(EntriesToCheck);
	}
	return Results;
}

template<typename T>
TArray<T*> GetAllStackEntriesInSystem(TSharedPtr<FNiagaraSystemViewModel> ViewModel, bool bRefresh = false)
{
	TArray<T*> Results;
	Results.Append(GetStackEntries<T>(ViewModel->GetSystemStackViewModel(), bRefresh));
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		Results.Append(GetStackEntries<T>(EmitterHandleModel.Get().GetEmitterStackViewModel(), bRefresh));
	}
	return Results;
}

// helper function to retrieve a single stack entry from the system or emitter view model
template<typename T>
T* GetStackEntry(UNiagaraStackViewModel* StackViewModel, bool bRefresh = false)
{
	TArray<T*> StackEntries = GetStackEntries<T>(StackViewModel, bRefresh);
	if (StackEntries.Num() > 0)
	{
		return StackEntries[0];
	}
	return nullptr;
}

// --------------------------------------------------------------------------------------------------------------------------------------------
// Common fixes and links

void AddGoToFXTypeLink(FNiagaraValidationResult& Result, UNiagaraEffectType* FXType)
{
	if (FXType == nullptr)
	{
		return;
	}

	FNiagaraValidationFix& GoToValidationRulesLink = Result.Links.AddDefaulted_GetRef();
	GoToValidationRulesLink.Description = LOCTEXT("GoToValidationRulesFix", "Go To Validation Rules");
	TWeakObjectPtr<UNiagaraEffectType> WeakFXType = FXType;
	GoToValidationRulesLink.FixDelegate = FNiagaraValidationFixDelegate::CreateLambda([WeakFXType]
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			TWeakPtr<IAssetTypeActions> WeakAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UNiagaraEffectType::StaticClass());

			if (UNiagaraEffectType* FXType = WeakFXType.Get())
			{
				if (TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakAssetTypeActions.Pin())
				{
					TArray<UObject*> AssetsToEdit;
					AssetsToEdit.Add(FXType);
					AssetTypeActions->OpenAssetEditor(AssetsToEdit);
					//TODO: Is there a way for us to auto navigate to and open up the validation rules inside FXType?
				}
			}
		});
}

// --------------------------------------------------------------------------------------------------------------------------------------------

void UNiagaraValidationRule_NoWarmupTime::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{
	UNiagaraSystem& System = ViewModel->GetSystem();
	if (System.NeedsWarmup())
	{
		UNiagaraStackSystemPropertiesItem* SystemProperties = GetStackEntry<UNiagaraStackSystemPropertiesItem>(ViewModel->GetSystemStackViewModel());
		FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("WarumupSummary", "Warmuptime > 0 is not allowed"), LOCTEXT("WarmupDescription", "Systems with the chosen effect type do not allow warmup time, as it costs too much performance.\nPlease set the warmup time to 0 in the system properties."), SystemProperties);
		Results.Add(Result);
	}
}

void UNiagaraValidationRule_FixedGPUBoundsSet::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{
	// if the system has fixed bounds set then it overrides the emitter settings
	if (ViewModel->GetSystem().bFixedBounds)
	{
		return;
	}

	// check that all the gpu emitters have fixed bounds set
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		UNiagaraEmitter* NiagaraEmitter = EmitterHandleModel.Get().GetEmitterHandle()->GetInstance();
		if (NiagaraEmitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && NiagaraEmitter->CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Dynamic)
		{
			UNiagaraStackEmitterPropertiesItem* EmitterProperties = GetStackEntry<UNiagaraStackEmitterPropertiesItem>(EmitterHandleModel.Get().GetEmitterStackViewModel());
			FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("GpuDynamicBoundsErrorSummary", "GPU emitters do not support dynamic bounds"), LOCTEXT("GpuDynamicBoundsErrorDescription", "Gpu emitter should either not be in dynamic mode or the system must have fixed bounds."), EmitterProperties);
			Results.Add(Result);
		}
	}
}

bool IsEnabledForMaxQualityLevel(FNiagaraPlatformSet Platforms, int32 MaxQualityLevel)
{
	for (int i = 0; i < MaxQualityLevel; i++)
	{
		if (Platforms.IsEnabledForQualityLevel(i))
		{
			return true;
		}
	}
	return false;
}

void UNiagaraValidationRule_BannedRenderers::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{
	UNiagaraSystem& System = ViewModel->GetSystem();
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		int32 MaxQualityLevel = 3;
		UNiagaraEmitter* NiagaraEmitter = EmitterHandleModel.Get().GetEmitterHandle()->GetInstance();
		NiagaraEmitter->ForEachRenderer([&Results, EmitterHandleModel, MaxQualityLevel, this, &System](UNiagaraRendererProperties* RendererProperties)
		{
			if (RendererProperties->GetIsEnabled() && BannedRenderers.Contains(RendererProperties->GetClass()))
			{
				TArray<const FNiagaraPlatformSet*> CheckSets;
				CheckSets.Add(&Platforms);
				CheckSets.Add(&RendererProperties->Platforms);

				TArray<FNiagaraPlatformSetConflictInfo> Conflicts;
				FNiagaraPlatformSet::GatherConflicts(CheckSets, Conflicts);
				if (Conflicts.Num() > 0)
				{
					TArray<UNiagaraStackRendererItem*> RendererItems = GetStackEntries<UNiagaraStackRendererItem>(EmitterHandleModel.Get().GetEmitterStackViewModel());
					for (UNiagaraStackRendererItem* Item : RendererItems)
					{
						if (Item->GetRendererProperties() != RendererProperties)
						{
							continue;
						}						
						FNiagaraValidationResult Result = Results.AddDefaulted_GetRef();
						
						Result.Severity = ENiagaraValidationSeverity::Warning;
						Result.SummaryText = LOCTEXT("BannedRenderSummary", "Banned renderers used.");
						Result.Description = LOCTEXT("BannedRenderDescription", "Please ensure only allowed renderers are used for each platform according to the validation rules in the System's Effect Type.");
						Result.SourceObject = Item;
						
						AddGoToFXTypeLink(Result, System.GetEffectType());

						//Add autofix to disable the module
						FNiagaraValidationFix& DisableRendererFix = Result.Fixes.AddDefaulted_GetRef();
						DisableRendererFix.Description = LOCTEXT("DisableBannedRendererFix", "Disable Banned Renderer");
						TWeakObjectPtr<UNiagaraStackRendererItem> WeakRendererItem = Item;
						DisableRendererFix.FixDelegate = FNiagaraValidationFixDelegate::CreateLambda(
							[WeakRendererItem]()
							{
								if (UNiagaraStackRendererItem* RendererItem = WeakRendererItem.Get())
								{
									RendererItem->SetIsEnabled(false);
								}
							});

						Results.Add(Result);
					}
				}
			}
		});
	}
} 

void UNiagaraValidationRule_BannedModules::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{
	UNiagaraSystem& System = ViewModel->GetSystem();

	TArray<UNiagaraStackModuleItem*> StackModuleItems =	GetAllStackEntriesInSystem<UNiagaraStackModuleItem>(ViewModel);

	for (UNiagaraStackModuleItem* Item : StackModuleItems)
	{
		if (Item && Item->GetIsEnabled())
		{
			UNiagaraNodeFunctionCall& FuncCall = Item->GetModuleNode();

			for (UNiagaraScript* BannedModule : BannedModules)
			{
				if (BannedModule == FuncCall.FunctionScript)
				{
					UNiagaraEmitter* Emitter = Item->GetEmitterViewModel().IsValid() ? Item->GetEmitterViewModel()->GetEmitter() : nullptr;

					bool bApplyBan = true;
					if (Emitter)
					{
						//If we're on an emitter, this emitter may be culled on the platforms the rule applies to.
						TArray<const FNiagaraPlatformSet*> CheckSets;
						CheckSets.Add(&Platforms);
						CheckSets.Add(&Emitter->Platforms);
						TArray<FNiagaraPlatformSetConflictInfo> Conflicts;
						FNiagaraPlatformSet::GatherConflicts(CheckSets, Conflicts);
						bApplyBan = Conflicts.Num() > 0;
					}

					if (!bApplyBan)
					{
						continue;
					}

					const FTextFormat Format(LOCTEXT("BannedModuleFormat", "Module {0} is banned on some currently enabled platforms"));
					const FText WarningMessage = FText::Format(Format, FText::FromString(FuncCall.FunctionScript->GetName()));

					FNiagaraValidationResult& Result = Results.AddDefaulted_GetRef();
					Result.Severity = ENiagaraValidationSeverity::Warning;
					Result.SummaryText = WarningMessage;
					Result.Description = LOCTEXT("BanndeModulesDescription", "Check this module against the Effect Type's Banned Modules validators");
					Result.SourceObject = Item;

					AddGoToFXTypeLink(Result, System.GetEffectType());

					//Add autofix to disable the module
					FNiagaraValidationFix& DisableModuleFix = Result.Fixes.AddDefaulted_GetRef();
					DisableModuleFix.Description = LOCTEXT("DisableBannedModuleFix", "Disable Banned Module");					
					TWeakObjectPtr<UNiagaraStackModuleItem> WeakModuleItem = Item;
					DisableModuleFix.FixDelegate = FNiagaraValidationFixDelegate::CreateLambda([WeakModuleItem]()
					{
						if (UNiagaraStackModuleItem* ModuleItem = WeakModuleItem.Get())
						{
							ModuleItem->SetEnabled(false);
						}
					});
				}
			}
		}
	}
}

void UNiagaraValidationRule_InvalidEffectType::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{
	UNiagaraStackSystemPropertiesItem* SystemProperties = GetStackEntry<UNiagaraStackSystemPropertiesItem>(ViewModel->GetSystemStackViewModel());
	FNiagaraValidationResult Result(ENiagaraValidationSeverity::Error, LOCTEXT("InvalidEffectSummary", "Invalid Effect Type"), LOCTEXT("InvalidEffectDescription", "The effect type on this system was marked as invalid for production content and should only be used as placeholder."), SystemProperties);
	Results.Add(Result);
}

void UNiagaraValidationRule_LWC::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& Results)  const
{

	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	UNiagaraSystem& System = ViewModel->GetSystem();
	if (!System.SupportsLargeWorldCoordinates())
	{
		return;
	}

	// gather all the modules in the system, excluding localspace emitters
	TArray<UNiagaraStackModuleItem*> AllModules;
	AllModules.Append(GetStackEntries<UNiagaraStackModuleItem>(ViewModel->GetSystemStackViewModel()));
	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandleViewModels = ViewModel->GetEmitterHandleViewModels();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleModel : EmitterHandleViewModels)
	{
		if (EmitterHandleModel->GetEmitterHandle()->GetInstance()->bLocalSpace == false)
		{
			AllModules.Append(GetStackEntries<UNiagaraStackModuleItem>(EmitterHandleModel.Get().GetEmitterStackViewModel()));
		}
	}

	for (UNiagaraStackModuleItem* Module : AllModules)
	{
		TArray<UNiagaraStackFunctionInput*> StackInputs;
		Module->GetParameterInputs(StackInputs);
		
		for (UNiagaraStackFunctionInput* Input : StackInputs)
		{
			if (Input->GetInputType() == FNiagaraTypeDefinition::GetPositionDef())
			{
				// check if any position inputs are set locally to absolute values
				if (Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local)
				{
					FNiagaraValidationResult Result(ENiagaraValidationSeverity::Warning, FText::Format(LOCTEXT("LocalPosInputSummary", "Input '{0}' set to absolute value"), Input->GetDisplayName()), LOCTEXT("LocalPosInputDescription", "Position attributes should never be set to an absolute values, because they will be offset when using large world coordinates.\nInstead, set them relative to a known position like Engine.Owner.Position."), Input);
					Results.Add(Result);
				}

				// check if the linked dynamic input script outputs a vector
				if (Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Dynamic && Input->GetDynamicInputNode() && Settings->bEnforceStrictStackTypes)
				{
					if (UNiagaraScriptSource* DynamicInputSource = Cast<UNiagaraScriptSource>(Input->GetDynamicInputNode()->GetFunctionScriptSource()))
					{
						TArray<FNiagaraVariable> OutNodes;
						DynamicInputSource->NodeGraph->GetOutputNodeVariables(OutNodes);
						for (const FNiagaraVariable& OutVariable : OutNodes)
						{
							if (OutVariable.GetType() == FNiagaraTypeDefinition::GetVec3Def())
							{
								FTextFormat DescriptionFormat = LOCTEXT("VecDILinkedToPosInputDescription", "The position input {0} is linked to a dynamic input that outputs a vector.\nPlease use a dynamic input that outputs a position instead or explicitly convert the vector to a position type.");
								FNiagaraValidationResult Result(ENiagaraValidationSeverity::Warning, LOCTEXT("VecDILinkedToPosInputSummary", "Position input is linked to a vector output"), FText::Format(DescriptionFormat, Input->GetDisplayName()), Input);
								Results.Add(Result);
							}
						}
					}
				}

				// check if the linked input variable is a vector
				if (Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Linked && Settings->bEnforceStrictStackTypes)
				{
					FNiagaraVariable VectorVar(FNiagaraTypeDefinition::GetVec3Def(), Input->GetLinkedValueHandle().GetParameterHandleString());
					const UNiagaraGraph* NiagaraGraph = Input->GetInputFunctionCallNode().GetNiagaraGraph();

					// we check if metadata for a vector attribute with the linked name exists in the emitter/system script graph. Not 100% correct, but it needs to be fast and a few false negatives are acceptable.
					if (NiagaraGraph && NiagaraGraph->GetMetaData(VectorVar).IsSet())
					{
						FNiagaraValidationResult Result(ENiagaraValidationSeverity::Warning, FText::Format(LOCTEXT("PositionLinkedVectorSummary", "Input '{0}' is linked to a vector attribute"), Input->GetDisplayName()), LOCTEXT("PositionLinkedVectorDescription", "Position types should only be linked to position attributes. In this case, it is linked to a vector attribute and the implicit conversion can cause problems with large world coordinates."), Input);
						Results.Add(Result);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE