// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterOperatorToolbar.h"

#include "IDisplayClusterOperator.h"
#include "DisplayClusterRootActor.h"

#include "EditorStyleSet.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorOperator"

SDisplayClusterOperatorToolbar::~SDisplayClusterOperatorToolbar()
{
	if (LevelActorDeletedHandle.IsValid() && GEngine != nullptr)
	{
		GEngine->OnLevelActorDeleted().Remove(LevelActorDeletedHandle);
	}

	if (MapChangedHandle.IsValid())
	{
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditor.OnMapChanged().Remove(MapChangedHandle);
	}
	
	if (ActiveRootActor.IsValid())
	{
		if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(ActiveRootActor->GetClass()))
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}
}

void SDisplayClusterOperatorToolbar::Construct(const FArguments& InArgs)
{
	CommandList = InArgs._CommandList;

	FillRootActorList();
	
	TSharedPtr<FExtender> ToolBarExtender = IDisplayClusterOperator::Get().GetOperatorToolBarExtensibilityManager()->GetAllExtenders();

    FToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None, ToolBarExtender);

	RootActorComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&RootActorList)
		.OnSelectionChanged(this, &SDisplayClusterOperatorToolbar::OnRootActorChanged)
		.OnComboBoxOpening(this, &SDisplayClusterOperatorToolbar::OnRootActorComboBoxOpening)
		.OnGenerateWidget(this, &SDisplayClusterOperatorToolbar::GenerateRootActorComboBoxWidget)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &SDisplayClusterOperatorToolbar::GetRootActorComboBoxText)
		];

	if (RootActorList.Num())
	{
		RootActorComboBox->SetSelectedItem(RootActorList[0]);
	}

	ToolBarBuilder.BeginSection("General");
	{
		ToolBarBuilder.AddToolBarWidget(RootActorComboBox.ToSharedRef(), LOCTEXT("RootActorPickerLabel", "nDisplay Actor"));
	}
	ToolBarBuilder.EndSection();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			ToolBarBuilder.MakeWidget()
		]
	];

	if (GEngine != nullptr)
	{
		LevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddSP(this, &SDisplayClusterOperatorToolbar::OnLevelActorDeleted);
	}

	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	MapChangedHandle = LevelEditor.OnMapChanged().AddRaw(this, &SDisplayClusterOperatorToolbar::HandleMapChanged);
}

TSharedPtr<FString> SDisplayClusterOperatorToolbar::FillRootActorList(const FString& InitiallySelectedRootActor)
{
	RootActorList.Empty();

	TArray<ADisplayClusterRootActor*> RootActors;
	IDisplayClusterOperator::Get().GetRootActorLevelInstances(RootActors);

	TSharedPtr<FString> SelectedItem = nullptr;
	for (ADisplayClusterRootActor* RootActor : RootActors)
	{
		TSharedPtr<FString> ActorName = MakeShared<FString>(RootActor->GetActorNameOrLabel());
		if (InitiallySelectedRootActor == *ActorName)
		{
			SelectedItem = ActorName;
		}

		RootActorList.Add(ActorName);
	}

	return SelectedItem;
}

void SDisplayClusterOperatorToolbar::ClearSelectedRootActor()
{
	if (ActiveRootActor.IsValid())
	{
		if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(ActiveRootActor->GetClass()))
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}

	ActiveRootActorName.Reset();
	ActiveRootActor.Reset();
	IDisplayClusterOperator::Get().OnActiveRootActorChanged().Broadcast(nullptr);
	RootActorComboBox->SetSelectedItem(nullptr);
}

void SDisplayClusterOperatorToolbar::OnRootActorChanged(TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo)
{
	ActiveRootActorName = ItemSelected;
	if (!ItemSelected.IsValid())
	{
		return;
	}

	TArray<ADisplayClusterRootActor*> RootActors;
	IDisplayClusterOperator::Get().GetRootActorLevelInstances(RootActors);

	ADisplayClusterRootActor* SelectedRootActor = nullptr;
	for (ADisplayClusterRootActor* RootActor : RootActors)
	{
		if (RootActor->GetActorNameOrLabel() == *ItemSelected)
		{
			SelectedRootActor = RootActor;
			break;
		}
	}

	if (ActiveRootActor.IsValid())
	{
		if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(ActiveRootActor->GetClass()))
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}
	
	if (SelectedRootActor)
	{
		if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(SelectedRootActor->GetClass()))
		{
			Blueprint->OnCompiled().RemoveAll(this);
			Blueprint->OnCompiled().AddRaw(this, &SDisplayClusterOperatorToolbar::OnBlueprintCompiled);
		}
	}

	ActiveRootActor = SelectedRootActor;
	IDisplayClusterOperator::Get().OnActiveRootActorChanged().Broadcast(SelectedRootActor);
}

void SDisplayClusterOperatorToolbar::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	if (RootActorComboBox.IsValid() && ActiveRootActorName.IsValid())
	{
		// Compiling the blueprint invalidates the instance so we need to find and set the instance again.
		RootActorComboBox->SetSelectedItem(ActiveRootActorName);
		OnRootActorChanged(ActiveRootActorName, ESelectInfo::Direct);
	}
}

void SDisplayClusterOperatorToolbar::OnRootActorComboBoxOpening()
{
	FString SelectedRootActor = "";
	if (TSharedPtr<FString> SelectedItem = RootActorComboBox->GetSelectedItem())
	{
		SelectedRootActor = *SelectedItem;
	}

	TSharedPtr<FString> NewSelectedItem = FillRootActorList(SelectedRootActor);

	RootActorComboBox->RefreshOptions();
	RootActorComboBox->SetSelectedItem(NewSelectedItem);
}

TSharedRef<SWidget> SDisplayClusterOperatorToolbar::GenerateRootActorComboBoxWidget(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
}

FText SDisplayClusterOperatorToolbar::GetRootActorComboBoxText() const
{
	TSharedPtr<FString> SelectedRootActor = RootActorComboBox->GetSelectedItem();

	if (!SelectedRootActor.IsValid())
	{
		return LOCTEXT("NoRootActorSelectedLabel", "No nDisplay Actor Selected");
	}
	
	return FText::FromString(*RootActorComboBox->GetSelectedItem());
}

void SDisplayClusterOperatorToolbar::OnLevelActorDeleted(AActor* Actor)
{
	if (Actor == ActiveRootActor)
	{
		if (Actor && Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			// When a blueprint class is regenerated instances are deleted and replaced.
			// In this case the OnCompiled() delegate will fire and refresh the actor.
			return;
		}
		
		ClearSelectedRootActor();
	}
}

void SDisplayClusterOperatorToolbar::HandleMapChanged(UWorld* InWorld, EMapChangeType InMapChangeType)
{
	if (InMapChangeType == EMapChangeType::TearDownWorld &&
		(!ActiveRootActor.IsValid() || ActiveRootActor->GetWorld() == InWorld))
	{
		ClearSelectedRootActor();
	}
}

#undef LOCTEXT_NAMESPACE
