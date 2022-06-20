// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditor.h"

#include "AnimationEditorPreviewActor.h"
#include "EditorModeManager.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "PersonaModule.h"
#include "IPersonaToolkit.h"
#include "IAssetFamily.h"
#include "ISkeletonEditorModule.h"
#include "Preferences/PersonaOptions.h"
#include "AnimCustomInstanceHelper.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RetargetEditor/IKRetargetApplicationMode.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetEditorController.h"

#define LOCTEXT_NAMESPACE "IKRetargeterEditor"

const FName IKRetargetApplicationModes::IKRetargetApplicationMode("IKRetargetApplicationMode");
const FName IKRetargetEditorAppName = FName(TEXT("IKRetargetEditorApp"));

FIKRetargetEditor::FIKRetargetEditor()
	: EditorController(MakeShared<FIKRetargetEditorController>())
{
}

FIKRetargetEditor::~FIKRetargetEditor()
{
}

void FIKRetargetEditor::InitAssetEditor(
	const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost, 
    UIKRetargeter* InAsset)
{
	EditorController->Initialize(SharedThis(this), InAsset);

	BindCommands();
	
	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FIKRetargetEditor::HandlePreviewSceneCreated);
	
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	EditorController->PersonaToolkit = PersonaModule.CreatePersonaToolkit(InAsset, PersonaToolkitArgs);
	
	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(InAsset);
	AssetFamily->RecordAssetOpened(FAssetData(InAsset));

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode, 
		InitToolkitHost, 
		IKRetargetEditorAppName, 
		FTabManager::FLayout::NullLayout, 
		bCreateDefaultStandaloneMenu, 
		bCreateDefaultToolbar, 
		InAsset);

	// this sets the application mode which defines the tab factory that builds the editor layout
	AddApplicationMode(
		IKRetargetApplicationModes::IKRetargetApplicationMode,
		MakeShareable(new FIKRetargetApplicationMode(SharedThis(this),EditorController->PersonaToolkit->GetPreviewScene())));
	SetCurrentMode(IKRetargetApplicationModes::IKRetargetApplicationMode);

	// set the default editing mode to use in the editor
	GetEditorModeManager().SetDefaultMode(FIKRetargetDefaultMode::ModeName);
	
	// give default editing mode a pointer to the editor controller
	GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);
	FIKRetargetDefaultMode* DefaultMode = GetEditorModeManager().GetActiveModeTyped<FIKRetargetDefaultMode>(FIKRetargetDefaultMode::ModeName);
	DefaultMode->SetEditorController(EditorController);

	// give edit pose mode a pointer to the editor controller
	GetEditorModeManager().ActivateMode(FIKRetargetEditPoseMode::ModeName);
	FIKRetargetEditPoseMode* EditPoseMode = GetEditorModeManager().GetActiveModeTyped<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName);
	EditPoseMode->SetEditorController(EditorController);
	GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

void FIKRetargetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_IKRigEditor", "IK Rig Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FIKRetargetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FIKRetargetEditor::BindCommands()
{
	const FIKRetargetCommands& Commands = FIKRetargetCommands::Get();

	ToolkitCommands->MapAction(
		Commands.GoToRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleGoToRetargetPose),
		FCanExecuteAction(),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);
	
	ToolkitCommands->MapAction(
        Commands.EditRetargetPose,
        FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleEditPose),
        FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanEditPose),
        FIsActionChecked::CreateSP(EditorController,  &FIKRetargetEditorController::IsEditingPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.SetToRefPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleResetPose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanResetPose),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.NewRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleNewPose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanNewPose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.DeleteRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleDeletePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanDeletePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);

	ToolkitCommands->MapAction(
		Commands.RenameRetargetPose,
		FExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::HandleRenamePose),
		FCanExecuteAction::CreateSP(EditorController, &FIKRetargetEditorController::CanRenamePose),
		FCanExecuteAction(),
		EUIActionRepeatMode::RepeatDisabled);
}

void FIKRetargetEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
        "Asset",
        EExtensionHook::After,
        GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateSP(this, &FIKRetargetEditor::FillToolbar)
    );
}

void FIKRetargetEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Go To Retarget Pose");
	{
		ToolbarBuilder.AddToolBarButton(
			FIKRetargetCommands::Get().GoToRetargetPose,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(),"GenericStop"));
	}
	ToolbarBuilder.EndSection();
}

FName FIKRetargetEditor::GetToolkitFName() const
{
	return FName("IKRetargetEditor");
}

FText FIKRetargetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("IKRetargetEditorAppLabel", "IK Retarget Editor");
}

FText FIKRetargetEditor::GetToolkitName() const
{
	return FText::FromString(EditorController->AssetController->GetAsset()->GetName());
}

FLinearColor FIKRetargetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FIKRetargetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("IKRetargetEditor");
}

void FIKRetargetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	// hold the asset we are working on
	const UIKRetargeter* Retargeter = EditorController->AssetController->GetAsset();
	Collector.AddReferencedObject(Retargeter);
}

void FIKRetargetEditor::Tick(float DeltaTime)
{
	// update with latest offsets
	EditorController->AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, EditorController->SourceSkelMeshComponent);
	EditorController->AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, EditorController->TargetSkelMeshComponent);
}

TStatId FIKRetargetEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FIKRetargetEditor, STATGROUP_Tickables);
}

void FIKRetargetEditor::PostUndo(bool bSuccess)
{
	EditorController->ClearOutputLog();
	
	const bool WasEditing = EditorController->IsEditingPose();
	
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();

	// restore pose mode state to avoid stepping out of the edition when undoing things
	// note that BroadcastNeedsReinitialized will unset it in FIKRetargetEditorController::OnRetargeterNeedsInitialized
	if (WasEditing)
	{
		EditorController->HandleEditPose();
	}
}

void FIKRetargetEditor::PostRedo(bool bSuccess)
{
	EditorController->ClearOutputLog();
	
	const bool WasEditing = EditorController->IsEditingPose();
	
	EditorController->AssetController->BroadcastNeedsReinitialized();
	EditorController->RefreshAllViews();
	
	// restore pose mode state to avoid stepping out of the edition when undoing things
	// note that BroadcastNeedsReinitialized will unset it in FIKRetargetEditorController::OnRetargeterNeedsInitialized
	if (WasEditing)
	{
		EditorController->HandleEditPose();
	}
}

void FIKRetargetEditor::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	AAnimationEditorPreviewActor* Actor = InPersonaPreviewScene->GetWorld()->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);
	
	// create the skeletal mesh components
	EditorController->SourceSkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
	EditorController->TargetSkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);

	// setup an apply an anim instance to the skeletal mesh component
	EditorController->SourceAnimInstance = NewObject<UAnimPreviewInstance>(EditorController->SourceSkelMeshComponent, TEXT("IKRetargetSourceAnimScriptInstance"));
	EditorController->TargetAnimInstance = NewObject<UIKRetargetAnimInstance>(EditorController->TargetSkelMeshComponent, TEXT("IKRetargetTargetAnimScriptInstance"));
	SetupAnimInstance();
	
	// set the source and target skeletal meshes on the component
	// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
	USkeletalMesh* SourceMesh = EditorController->GetSourceSkeletalMesh();
	USkeletalMesh* TargetMesh = EditorController->GetTargetSkeletalMesh();
	EditorController->SourceSkelMeshComponent->SetSkeletalMesh(SourceMesh);
	EditorController->TargetSkelMeshComponent->SetSkeletalMesh(TargetMesh);

	// apply mesh to the preview scene
	InPersonaPreviewScene->SetPreviewMeshComponent(EditorController->SourceSkelMeshComponent);
	InPersonaPreviewScene->SetPreviewMesh(SourceMesh);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);

	// SetPreviewMesh() sets this flag true, which the render uses to filter out objects for selection highlighting...
	// but since we want to be able to select the mesh in this viewport, we have to set it back to false
	EditorController->SourceSkelMeshComponent->bCanHighlightSelectedSections = false;
	
	InPersonaPreviewScene->AddComponent(EditorController->SourceSkelMeshComponent, FTransform::Identity);
	InPersonaPreviewScene->AddComponent(EditorController->TargetSkelMeshComponent, FTransform::Identity);
}

void FIKRetargetEditor::SetupAnimInstance()
{
	// connect the retarget asset and the source component ot the target anim instance
	EditorController->TargetAnimInstance->SetRetargetAssetAndSourceComponent(EditorController->AssetController->GetAsset(), EditorController->SourceSkelMeshComponent);

	EditorController->SourceSkelMeshComponent->PreviewInstance = EditorController->SourceAnimInstance.Get();
	EditorController->TargetSkelMeshComponent->PreviewInstance = EditorController->TargetAnimInstance.Get();

	EditorController->SourceAnimInstance->InitializeAnimation();
	EditorController->TargetAnimInstance->InitializeAnimation();
}

void FIKRetargetEditor::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView)
{
	EditorController->DetailsView = InDetailsView;
	EditorController->DetailsView->OnFinishedChangingProperties().AddSP(this, &FIKRetargetEditor::OnFinishedChangingDetails);
	EditorController->DetailsView->SetObject(EditorController->AssetController->GetAsset());
}

void FIKRetargetEditor::OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent)
{
	const bool bSourceIKRigChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetSourceIKRigPropertyName();
	const bool bTargetIKRigChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetIKRigPropertyName();
	const bool bSourcePreviewChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetSourcePreviewMeshPropertyName();
	const bool bTargetPreviewChanged = PropertyChangedEvent.GetPropertyName() == UIKRetargeter::GetTargetPreviewMeshPropertyName();

	if (bTargetIKRigChanged || bSourceIKRigChanged)
	{
		EditorController->ClearOutputLog();
		EditorController->BindToIKRigAsset(EditorController->AssetController->GetAsset()->GetTargetIKRigWriteable());
		EditorController->BindToIKRigAsset(EditorController->AssetController->GetAsset()->GetSourceIKRigWriteable());
		EditorController->AssetController->CleanChainMapping();
		EditorController->AssetController->AutoMapChains();
	}
	
	if (bTargetIKRigChanged || bSourceIKRigChanged || bTargetPreviewChanged || bSourcePreviewChanged)
	{
		EditorController->ClearOutputLog();
		
		// set the source and target skeletal meshes on the component
		// NOTE: this must be done AFTER setting the AnimInstance so that the correct root anim node is loaded
		USkeletalMesh* SourceMesh = EditorController->GetSourceSkeletalMesh();
		USkeletalMesh* TargetMesh = EditorController->GetTargetSkeletalMesh();
		EditorController->SourceSkelMeshComponent->SetSkeletalMesh(SourceMesh);
		EditorController->TargetSkelMeshComponent->SetSkeletalMesh(TargetMesh);
	
		// apply mesh to the preview scene
		TSharedRef<IPersonaPreviewScene> PreviewScene = GetPersonaToolkit()->GetPreviewScene();
		if (PreviewScene->GetPreviewMesh() != SourceMesh)
		{
			PreviewScene->SetPreviewMeshComponent(EditorController->SourceSkelMeshComponent);
			PreviewScene->SetPreviewMesh(SourceMesh);
			EditorController->SourceSkelMeshComponent->bCanHighlightSelectedSections = false;
		}
	
		SetupAnimInstance();

		EditorController->RefreshAllViews();
	}
}

#undef LOCTEXT_NAMESPACE
