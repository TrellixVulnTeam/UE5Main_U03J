// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigTemplateNodeSpawner.h"
#include "ControlRigUnitNodeSpawner.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorEngine.h"
#include "ObjectEditorUtils.h"
#include "EditorCategoryUtils.h"
#include "K2Node_Variable.h"
#include "BlueprintNodeTemplateCache.h"
#include "ControlRigBlueprintUtils.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigTemplateNodeSpawner"

UControlRigTemplateNodeSpawner* UControlRigTemplateNodeSpawner::CreateFromNotation(const FName& InNotation, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip)
{
	UControlRigTemplateNodeSpawner* NodeSpawner = NewObject<UControlRigTemplateNodeSpawner>(GetTransientPackage());
	NodeSpawner->TemplateNotation = InNotation;
	NodeSpawner->NodeClass = UControlRigGraphNode::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = InMenuDesc;
	MenuSignature.Tooltip  = InTooltip;
	MenuSignature.Category = InCategory;

	if (const FRigVMTemplate* Template = FRigVMRegistry::Get().FindTemplate(InNotation))
	{
#if WITH_EDITOR
		FString KeywordsMetadata = Template->GetKeywords();
		MenuSignature.Keywords = FText::FromString(KeywordsMetadata);
#endif
	}

	// add at least one character, so that PrimeDefaultUiSpec() doesn't 
	// attempt to query the template node
	//
	// @TODO: maybe UPROPERTY() fields should have keyword metadata like functions
	if (MenuSignature.Keywords.IsEmpty())
	{
		// want to set it to something so we won't end up back in this condition
		MenuSignature.Keywords = FText::FromString(TEXT(" "));
	}

	// @TODO: should use details customization-like extensibility system to provide editor only data like this
	MenuSignature.Icon = FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.RigUnit"));

	return NodeSpawner;
}

void UControlRigTemplateNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigTemplateNodeSpawner::GetSpawnerSignature() const
{
	const int32 NotationHash = (int32)GetTypeHash(TemplateNotation);
	return FBlueprintNodeSignature(FString("RigVMTemplate_") + FString::FromInt(NotationHash));
}

FBlueprintActionUiSpec UControlRigTemplateNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigTemplateNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	UControlRigGraphNode* NewNode = nullptr;

	if(!TemplateNotation.IsNone())
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(ParentGraph);
		NewNode = SpawnNode(ParentGraph, Blueprint, TemplateNotation, Location);
	}

	return NewNode;
}

UControlRigGraphNode* UControlRigTemplateNodeSpawner::SpawnNode(UEdGraph* ParentGraph, UBlueprint* Blueprint, const FName& InNotation, FVector2D const Location)
{
	UControlRigGraphNode* NewNode = nullptr;
	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);

	if (RigBlueprint != nullptr && RigGraph != nullptr)
	{
		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);
		bool const bIsUserFacingNode = !bIsTemplateNode;

		const FRigVMTemplate* Template = FRigVMRegistry::Get().FindTemplate(InNotation);
		if (Template == nullptr)
		{
			return nullptr;
		}

		const int32 NotationHash = (int32)GetTypeHash(InNotation);
		const FString TemplateName = TEXT("RigVMTemplate_") + FString::FromInt(NotationHash);

		FName Name = bIsTemplateNode ? *TemplateName : FControlRigBlueprintUtils::ValidateName(RigBlueprint, Template->GetName().ToString());
		URigVMController* Controller = bIsTemplateNode ? RigGraph->GetTemplateController() : RigBlueprint->GetController(ParentGraph);

		if (!bIsTemplateNode)
		{
			Controller->OpenUndoBracket(FString::Printf(TEXT("Add '%s' Node"), *Name.ToString()));
		}

		if (URigVMTemplateNode* ModelNode = Controller->AddTemplateNode(InNotation, Location, Name.ToString(), bIsUserFacingNode, !bIsTemplateNode))
		{
			NewNode = Cast<UControlRigGraphNode>(RigGraph->FindNodeForModelNodeName(ModelNode->GetFName()));

			if (NewNode && bIsUserFacingNode)
			{
				Controller->ClearNodeSelection(true);
				Controller->SelectNode(ModelNode, true, true);

				UControlRigUnitNodeSpawner::HookupMutableNode(ModelNode, RigBlueprint);
			}

			if (bIsUserFacingNode)
			{
				Controller->CloseUndoBracket();
			}
			else
			{
				if(NewNode)
				{
					NewNode->ModelNodePath = Template->GetNotation().ToString();
				}
				Controller->RemoveNode(ModelNode, false);
			}
		}
		else
		{
			if (bIsUserFacingNode)
			{
				Controller->CancelUndoBracket();
			}
		}
	}
	return NewNode;
}

#undef LOCTEXT_NAMESPACE
