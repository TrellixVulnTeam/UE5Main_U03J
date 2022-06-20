// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditorModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorStyle.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerModelRegistry.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorActor.h"
#include "MLDeformerComponent.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerSampler.h"
#include "MLDeformerModelInstance.h"
#include "AnimationEditorPreviewActor.h"
#include "AnimPreviewInstance.h"
#include "Animation/MeshDeformer.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EditorViewportClient.h"
#include "Components/TextRenderComponent.h"
#include "Materials/Material.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "NeuralNetwork.h"

#define LOCTEXT_NAMESPACE "MLDeformerEditorModel"

namespace UE::MLDeformer
{
	FMLDeformerEditorModel::~FMLDeformerEditorModel()
	{
		DeleteEditorActors();

		FMLDeformerEditorModule& EditorModule = FModuleManager::GetModuleChecked<FMLDeformerEditorModule>("MLDeformerFrameworkEditor");
		EditorModule.GetModelRegistry().RemoveEditorModelInstance(this);
	}

	void FMLDeformerEditorModel::Init(const InitSettings& Settings)
	{
		check(Settings.Editor);
		check(Settings.Model);

		Editor = Settings.Editor;
		Model = Settings.Model;

		EditorInputInfo = Model->CreateInputInfo();
		check(EditorInputInfo);

		Sampler = CreateSampler();
		check(Sampler);
		Sampler->Init(this);
	}

	void FMLDeformerEditorModel::UpdateEditorInputInfo()
	{
		InitInputInfo(EditorInputInfo);
	}

	UWorld* FMLDeformerEditorModel::GetWorld() const
	{
		check(Editor);
		return Editor->GetPersonaToolkit()->GetPreviewScene()->GetWorld();
	}

	FMLDeformerSampler* FMLDeformerEditorModel::CreateSampler() const
	{
		return new FMLDeformerSampler();
	}

	void FMLDeformerEditorModel::CreateTrainingLinearSkinnedActor(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
	{
		UWorld* World = InPersonaPreviewScene->GetWorld();

		// Spawn the linear skinned actor.
		FActorSpawnParameters BaseSpawnParams;
		BaseSpawnParams.Name = MakeUniqueObjectName(World, AAnimationEditorPreviewActor::StaticClass(), "Train Base Actor");
		AActor* Actor = World->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity, BaseSpawnParams);
		Actor->SetFlags(RF_Transient);

		// Create the preview skeletal mesh component.
		const FLinearColor BaseWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.WireframeColor");
		UDebugSkelMeshComponent* SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
		SkelMeshComponent->SetWireframeMeshOverlayColor(BaseWireColor);
		SkelMeshComponent->SetVisibility(false);
		SkelMeshComponent->MarkRenderStateDirty();

		// Setup an apply an anim instance to the skeletal mesh component.
		UAnimPreviewInstance* AnimPreviewInstance = NewObject<UAnimPreviewInstance>(SkelMeshComponent, TEXT("MLDeformerAnimInstance"));
		SkelMeshComponent->PreviewInstance = AnimPreviewInstance;
		AnimPreviewInstance->InitializeAnimation();

		// Set the skeletal mesh on the component.
		// NOTE: This must be done AFTER setting the AnimInstance so that the correct root anim node is loaded.
		USkeletalMesh* Mesh = Model->GetSkeletalMesh();
		SkelMeshComponent->SetSkeletalMesh(Mesh);

		// Update the persona scene.
		InPersonaPreviewScene->SetActor(Actor);
		InPersonaPreviewScene->SetPreviewMeshComponent(SkelMeshComponent);
		InPersonaPreviewScene->AddComponent(SkelMeshComponent, FTransform::Identity);
		InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);
		InPersonaPreviewScene->SetPreviewMesh(Mesh);

		// Register the editor actor.
		const FLinearColor LabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.LabelColor");
		FMLDeformerEditorActor::FConstructSettings Settings;
		Settings.Actor = Actor;
		Settings.TypeID = ActorID_Train_Base;
		Settings.LabelColor = LabelColor;
		Settings.LabelText = LOCTEXT("TrainBaseActorLabelText", "Training Base");
		Settings.bIsTrainingActor = true;
		FMLDeformerEditorActor* EditorActor = CreateEditorActor(Settings);
		EditorActor->SetSkeletalMeshComponent(SkelMeshComponent);
		EditorActor->SetCanDestroyActor(false);	// Crash will occur when destroying the Persona actor, so disable this.
		EditorActor->SetMeshOffsetFactor(0.0f);
		EditorActors.Add(EditorActor);
	}

	void FMLDeformerEditorModel::CreateTestLinearSkinnedActor(UWorld* World)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), "Test Linear Skinned Actor");
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		Actor->SetFlags(RF_Transient);

		const FLinearColor BaseWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.WireframeColor");
		UDebugSkelMeshComponent* SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
		SkelMeshComponent->SetWireframeMeshOverlayColor(BaseWireColor);
		SkelMeshComponent->SetSkeletalMesh(Model->GetSkeletalMesh());
		Actor->SetRootComponent(SkelMeshComponent);
		SkelMeshComponent->RegisterComponent();
		SkelMeshComponent->SetVisibility(false);
		SkelMeshComponent->MarkRenderStateDirty();

		// Register the editor actor.
		const FLinearColor LabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.LabelColor");
		FMLDeformerEditorActor::FConstructSettings Settings;
		Settings.Actor = Actor;
		Settings.TypeID = ActorID_Test_Base;
		Settings.LabelColor = LabelColor;
		Settings.LabelText = LOCTEXT("TestBaseActorLabelText", "Linear Skinned");
		Settings.bIsTrainingActor = false;
		FMLDeformerEditorActor* EditorActor = CreateEditorActor(Settings);
		EditorActor->SetSkeletalMeshComponent(SkelMeshComponent);
		EditorActor->SetMeshOffsetFactor(0.0f);
		EditorActors.Add(EditorActor);
	}

	void FMLDeformerEditorModel::CreateTestMLDeformedActor(UWorld* World)
	{
		// Create the ML deformed actor.
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), "Test ML Deformed");
		AActor* Actor = World->SpawnActor<AActor>(SpawnParams);
		Actor->SetFlags(RF_Transient);

		// Create the skeletal mesh component.
		const FLinearColor MLDeformedWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.MLDeformedMesh.WireframeColor");
		UDebugSkelMeshComponent* SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
		SkelMeshComponent->SetSkeletalMesh(Model->GetSkeletalMesh());
		Actor->SetRootComponent(SkelMeshComponent);
		SkelMeshComponent->RegisterComponent();
		SkelMeshComponent->SetWireframeMeshOverlayColor(MLDeformedWireColor);
		SkelMeshComponent->SetVisibility(false);
		SkelMeshComponent->MarkRenderStateDirty();

		// Create the ML Deformer component.
		UMLDeformerAsset* DeformerAsset = Model->GetDeformerAsset();
		UMLDeformerComponent* MLDeformerComponent = NewObject<UMLDeformerComponent>(Actor);
		MLDeformerComponent->SetDeformerAsset(DeformerAsset);
		MLDeformerComponent->RegisterComponent();
		MLDeformerComponent->SetupComponent(DeformerAsset, SkelMeshComponent);

		// Create the editor actor.
		const FLinearColor LabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.MLDeformedMesh.LabelColor");
		FMLDeformerEditorActor::FConstructSettings Settings;
		Settings.Actor = Actor;
		Settings.TypeID = ActorID_Test_MLDeformed;
		Settings.LabelColor = LabelColor;
		Settings.LabelText = LOCTEXT("TestMLDeformedActorLabelText", "ML Deformed");
		Settings.bIsTrainingActor = false;
		FMLDeformerEditorActor* EditorActor = static_cast<FMLDeformerEditorActor*>(CreateEditorActor(Settings));
		EditorActor->SetSkeletalMeshComponent(SkelMeshComponent);
		EditorActor->SetMLDeformerComponent(MLDeformerComponent);
		EditorActor->SetMeshOffsetFactor(1.0f);
		EditorActors.Add(EditorActor);
	}

	void FMLDeformerEditorModel::CreateActors(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
	{
		UWorld* World = InPersonaPreviewScene->GetWorld();
		CreateTrainingLinearSkinnedActor(InPersonaPreviewScene);
		CreateTestLinearSkinnedActor(World);
		CreateTestMLDeformedActor(World);
		CreateTrainingGroundTruthActor(World);
		CreateTestGroundTruthActor(World);

		// Set the default mesh translation offsets for our ground truth actors.
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor->IsGroundTruthActor())
			{			
				// The mesh offset factor basically just offsets the actor position by a given factor.
				// The amount the actor is moved from the origin is: (MeshSpacing * MeshOffsetFactor).
				// In test mode we have 3 actors (Linear, ML Deformed, Ground Truth), so it's mesh offset factor will be 2.0 for the ground truth.
				// It is 2.0 because the ground truth actor in testing mode is all the way on the right, next to the ML Deformed model.
				// In training mode we have only the Linear Skinned actor and the ground truth, so there the spacing factor is 1.0.
				// TLDR: Basically 1.0 means its the first actor next to the linear skinned actor while 2.0 means its the second character, etc.
				EditorActor->SetMeshOffsetFactor(EditorActor->IsTestActor() ? 2.0f : 1.0f);
			}
		}

		OnPostCreateActors();
	}

	void FMLDeformerEditorModel::ClearWorld()
	{
		TSharedRef<IPersonaPreviewScene> PreviewScene = Editor->GetPersonaToolkit()->GetPreviewScene();

		UWorld* World = PreviewScene->GetWorld();
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			World->RemoveActor(EditorActor->GetActor(), true);
			if (EditorActor->GetCanDestroyActor())
			{
				EditorActor->GetActor()->Destroy();
			}
		}

		PreviewScene->SetPreviewAnimationAsset(nullptr);
		PreviewScene->SetPreviewAnimationBlueprint(nullptr, nullptr);
		PreviewScene->SetPreviewMesh(nullptr);
		PreviewScene->SetPreviewMeshComponent(nullptr);
		PreviewScene->SetActor(nullptr);
		PreviewScene->ClearSelectedActor();

		// Clear the editor actors.
		DeleteEditorActors();
	}

	FMLDeformerEditorActor* FMLDeformerEditorModel::CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const
	{ 
		return new FMLDeformerEditorActor(Settings);
	}

	void FMLDeformerEditorModel::DeleteEditorActors()
	{
		for (FMLDeformerEditorActor* Actor : EditorActors)
		{
			delete Actor;
		}
		EditorActors.Empty();
	}

	FMLDeformerEditorActor* FMLDeformerEditorModel::FindEditorActor(int32 ActorTypeID) const
	{
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor->GetTypeID() == ActorTypeID)
			{
				return EditorActor;
			}
		}

		return nullptr;
	}

	void FMLDeformerEditorModel::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
	{
		UpdateActorTransforms();
		UpdateLabels();
		CheckTrainingDataFrameChanged();

		// Update the ML Deformer component's weight.
		FMLDeformerEditorActor* EditorActor = FindEditorActor(ActorID_Test_MLDeformed);
		if (EditorActor)
		{
			UMLDeformerComponent* DeformerComponent = EditorActor->GetMLDeformerComponent();
			if (DeformerComponent)
			{		
				DeformerComponent->SetWeight(Model->GetVizSettings()->GetWeight());
			}
		}
	}

	void FMLDeformerEditorModel::UpdateLabels()
	{
		const UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		const bool bDrawTrainingActors = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData);
		const bool bDrawTestActors = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData);

		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			UTextRenderComponent* LabelComponent = EditorActor->GetLabelComponent();
			if (LabelComponent == nullptr)
			{
				continue;
			}

			if (VizSettings->GetDrawLabels())
			{
				const AActor* Actor = EditorActor->GetActor();
				const FVector ActorLocation = Actor->GetActorLocation();
				const FVector AlignmentOffset = EditorActor->IsGroundTruthActor() ? Model->GetAlignmentTransform().GetTranslation() : FVector::ZeroVector;

				LabelComponent->SetRelativeLocation(ActorLocation + FVector(0.0f, 0.0f, VizSettings->GetLabelHeight()) - AlignmentOffset);
				LabelComponent->SetRelativeRotation(FQuat(FVector(0.0f, 0.0f, 1.0f), FMath::DegreesToRadians(90.0f)));
				LabelComponent->SetRelativeScale3D(FVector(VizSettings->GetLabelScale() * 0.5f));

				// Update visibility.
				const bool bLabelIsVisible = (bDrawTrainingActors && EditorActor->IsTrainingActor()) || (bDrawTestActors && EditorActor->IsTestActor());
				LabelComponent->SetVisibility(bLabelIsVisible);

				// Handle test ground truth, disable its label when no ground truth asset was selected.
				if (EditorActor->GetTypeID() == ActorID_Test_GroundTruth && !VizSettings->HasTestGroundTruth())
				{
					LabelComponent->SetVisibility(false);
				}
			}
			else
			{
				LabelComponent->SetVisibility(false);
			}
		}
	}

	void FMLDeformerEditorModel::UpdateActorTransforms()
	{
		const FVector MeshSpacingVector = Model->GetVizSettings()->GetMeshSpacingOffsetVector();
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			FTransform Transform = EditorActor->IsGroundTruthActor() ? Model->GetAlignmentTransform() : FTransform::Identity;
			Transform.AddToTranslation(MeshSpacingVector * EditorActor->GetMeshOffsetFactor());
			EditorActor->GetActor()->SetActorTransform(Transform);
		}
	}

	void FMLDeformerEditorModel::UpdateActorVisibility()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		const bool bShowTrainingData = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData);
		const bool bShowTestData = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData);
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			const bool bIsVisible = (EditorActor->IsTestActor() && bShowTestData) || (EditorActor->IsTrainingActor() && bShowTrainingData);
			EditorActor->SetVisibility(bIsVisible);
		}
	}

	void FMLDeformerEditorModel::OnInputAssetsChanged()
	{
		// Force the training sequence to use Step interpolation and sample raw animation data.
		UAnimSequence* TrainingAnimSequence = Model->GetAnimSequence();
		if (TrainingAnimSequence)
		{
			TrainingAnimSequence->bUseRawDataOnly = true;
			TrainingAnimSequence->Interpolation = EAnimInterpolationType::Step;
		}

		// Update the training base actor.
		UDebugSkelMeshComponent* SkeletalMeshComponent = FindEditorActor(ActorID_Train_Base)->GetSkeletalMeshComponent();
		check(SkeletalMeshComponent);
		SkeletalMeshComponent->SetSkeletalMesh(Model->GetSkeletalMesh());
		if (GetEditor()->GetPersonaToolkitPointer())
		{
			GetEditor()->GetPersonaToolkit()->GetPreviewScene()->SetPreviewMesh(Model->GetSkeletalMesh());
		}
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		UAnimSequence* TestAnimSequence = VizSettings->GetTestAnimSequence();
		const float TestAnimSpeed = VizSettings->GetAnimPlaySpeed();
		SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		SkeletalMeshComponent->SetAnimation(TrainingAnimSequence);
		SkeletalMeshComponent->SetPosition(0.0f);
		SkeletalMeshComponent->SetPlayRate(TestAnimSpeed);
		SkeletalMeshComponent->Play(false);

		// Update the test base model.
		SkeletalMeshComponent = FindEditorActor(ActorID_Test_Base)->GetSkeletalMeshComponent();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetSkeletalMesh(Model->GetSkeletalMesh());
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			SkeletalMeshComponent->SetAnimation(TestAnimSequence);
			SkeletalMeshComponent->SetPosition(0.0f);
			SkeletalMeshComponent->SetPlayRate(TestAnimSpeed);
			SkeletalMeshComponent->Play(true);
		}

		// Update the test ML Deformed skeletal mesh component.
		SkeletalMeshComponent = FindEditorActor(ActorID_Test_MLDeformed)->GetSkeletalMeshComponent();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->SetSkeletalMesh(Model->GetSkeletalMesh());
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			SkeletalMeshComponent->SetAnimation(TestAnimSequence);
			SkeletalMeshComponent->SetPosition(0.0f);
			SkeletalMeshComponent->SetPlayRate(TestAnimSpeed);
			SkeletalMeshComponent->Play(true);
		}

		bIsDataNormalized = false;
	}

	void FMLDeformerEditorModel::OnPostInputAssetChanged()
	{
		CurrentTrainingFrame = -1;
		Editor->UpdateTimeSliderRange();
		Model->UpdateCachedNumVertices();
		UpdateDeformerGraph();
		RefreshMLDeformerComponents();
		UpdateIsReadyForTrainingState();
		SetTrainingFrame(0);
		UpdateEditorInputInfo();
		CheckTrainingDataFrameChanged();
	}

	void FMLDeformerEditorModel::OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing)
	{
		float PlayOffset = static_cast<float>(NewScrubTime);
		const int32 TargetFrame = GetFrameAtTime(NewScrubTime);

		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
		{
			for (FMLDeformerEditorActor* EditorActor : EditorActors)
			{
				if (EditorActor->IsTrainingActor())
				{
					if (Model->HasTrainingGroundTruth())
					{
						PlayOffset = GetTimeAtFrame(TargetFrame);
					}
					EditorActor->SetPlayPosition(PlayOffset);
				}
			}
			VizSettings->FrameNumber = TargetFrame;
		}
		else if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
		{
			for (FMLDeformerEditorActor* EditorActor : EditorActors)
			{
				if (EditorActor->IsTestActor())
				{
					if (Model->GetVizSettings()->HasTestGroundTruth())
					{
						PlayOffset = GetTimeAtFrame(TargetFrame);
					}
					EditorActor->SetPlayPosition(PlayOffset);
				}
			}
		}
	}

	void FMLDeformerEditorModel::SetTrainingFrame(int32 FrameNumber)
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		VizSettings->FrameNumber = FrameNumber;
		OnTimeSliderScrubPositionChanged(GetTimeAtFrame(FrameNumber), false);
	}

	void FMLDeformerEditorModel::HandleDefaultPropertyChanges(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		// When we change one of these properties below, restart animations etc.
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, SkeletalMesh))
		{
			TriggerInputAssetChanged();
			Model->InitVertexMap();
			Model->InitGPUData();
			UpdateDeformerGraph();
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AnimSequence) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TestAnimSequence))
		{
			TriggerInputAssetChanged(true);
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AlignmentTransform))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				SampleDeltas();
			}
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, MaxTrainingFrames))
		{
			TriggerInputAssetChanged();
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, TrainingInputs))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
				UpdateEditorInputInfo();
				UpdateIsReadyForTrainingState();
				GetEditor()->GetModelDetailsView()->ForceRefresh();
			}
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, BoneIncludeList) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerModel, CurveIncludeList))
		{
			UpdateEditorInputInfo();
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, AnimPlaySpeed))
		{
			UpdateTestAnimPlaySpeed();
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, FrameNumber))
		{
			ClampCurrentFrameIndex();
			const int32 CurrentFrameNumber = Model->GetVizSettings()->GetFrameNumber();
			OnTimeSliderScrubPositionChanged(GetTimeAtFrame(CurrentFrameNumber), false);
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bShowHeatMap))
		{
			SetHeatMapMaterialEnabled(Model->GetVizSettings()->GetShowHeatMap());
			UpdateDeformerGraph();
		}
		else
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLinearSkinnedActor) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawMLDeformedActor) ||
			Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawGroundTruthActor))
		{
			UpdateActorVisibility();
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawDeltas))
		{
			SampleDeltas();
		} 
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph))
		{
			UpdateDeformerGraph();
			GetEditor()->GetVizSettingsDetailsView()->ForceRefresh();
		}
	}

	void FMLDeformerEditorModel::OnPlayButtonPressed()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() != EMLDeformerVizMode::TestData)
		{
			return;
		}
		
		FMLDeformerEditorActor* BaseTestActor = FindEditorActor(ActorID_Test_Base);
		const bool bMustPause = (BaseTestActor && BaseTestActor->GetSkeletalMeshComponent()) ? !BaseTestActor->GetSkeletalMeshComponent()->bPauseAnims : false;
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor->IsTestActor())
			{
				EditorActor->Pause(bMustPause);
			}
		}
	}

	bool FMLDeformerEditorModel::IsPlayingAnim() const
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
		{
			FMLDeformerEditorActor* EditorActor = FindEditorActor(ActorID_Test_Base);
			if (EditorActor)
			{
				UDebugSkelMeshComponent* SkeletalMeshComponent = EditorActor->GetSkeletalMeshComponent();
				if (SkeletalMeshComponent)
				{
					return !SkeletalMeshComponent->bPauseAnims;
				}
			}
		}
		return false;
	}

	double FMLDeformerEditorModel::CalcTimelinePosition() const
	{
		// Get the base editor actor, depending on the mode we're in.
		FMLDeformerEditorActor* EditorActor = nullptr;
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
		{
			EditorActor = FindEditorActor(ActorID_Test_Base);
		}
		else if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
		{
			EditorActor = FindEditorActor(ActorID_Train_Base);
		}

		return EditorActor ? EditorActor->GetPlayPosition() : 0.0;
	}

	void FMLDeformerEditorModel::UpdateTestAnimPlaySpeed()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		const float Speed = VizSettings->GetAnimPlaySpeed();
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor->IsTestActor()) // Only do test actors, no training actors.
			{
				EditorActor->SetPlaySpeed(Speed);
			}
		}
	}

	void FMLDeformerEditorModel::ClampCurrentFrameIndex()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (GetNumFrames() > 0)
		{
			VizSettings->FrameNumber = FMath::Min(VizSettings->FrameNumber, static_cast<uint32>(GetNumFrames() - 1));
		}
		else
		{
			VizSettings->FrameNumber = 0;
		}
	}

	int32 FMLDeformerEditorModel::GetNumFramesForTraining() const 
	{ 
		return FMath::Min(GetNumFrames(), Model->GetTrainingFrameLimit());
	}

	FText FMLDeformerEditorModel::GetBaseAssetChangedErrorText() const
	{
		FText Result;
		if (Model->SkeletalMesh && Model->GetInputInfo())
		{
			if (Model->NumBaseMeshVerts != Model->GetInputInfo()->GetNumBaseMeshVertices() &&
				Model->NumBaseMeshVerts > 0 && Model->GetInputInfo()->GetNumBaseMeshVertices() > 0)
			{
				Result = FText::Format(LOCTEXT("BaseMeshMismatch", "Number of vertices in base mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
					Model->GetInputInfo()->GetNumBaseMeshVertices(),
					Model->NumBaseMeshVerts,
					IsTrained() ? LOCTEXT("BaseMeshMismatchNN", "Neural network needs to be retrained!") : FText());
			}
		}
		return Result;
	}

	FText FMLDeformerEditorModel::GetVertexMapChangedErrorText() const
	{
		FText Result;
		if (Model->SkeletalMesh)
		{
			bool bVertexMapMatch = true;
			const FSkeletalMeshModel* ImportedModel = Model->SkeletalMesh->GetImportedModel();
			if (ImportedModel)
			{
				const TArray<int32>& MeshVertexMap = ImportedModel->LODModels[0].MeshToImportVertexMap;
				const TArray<int32>& ModelVertexMap = Model->GetVertexMap();
				if (MeshVertexMap.Num() == ModelVertexMap.Num())
				{
					for (int32 Index = 0; Index < ModelVertexMap.Num(); ++Index)
					{
						if (MeshVertexMap[Index] != ModelVertexMap[Index])
						{
							bVertexMapMatch = false;
							break;
						}
					}
					
					if (!bVertexMapMatch)
					{
						Result = FText(LOCTEXT("VertexMapMismatch", "The vertex order of your Skeletal Mesh changed."));
					}
				}
			}
		}
		return Result;
	}

	FText FMLDeformerEditorModel::GetSkeletalMeshNeedsReimportErrorText() const
	{
		FText Result;

		if (Model->SkeletalMesh)
		{
			FSkeletalMeshModel* ImportedModel = Model->SkeletalMesh->GetImportedModel();
			check(ImportedModel);

			const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
			if (SkelMeshInfos.IsEmpty())
			{
				Result = LOCTEXT("SkelMeshNeedsReimport", "Skeletal Mesh asset needs to be reimported.");
			}
		}

		return Result;
	}

	FText FMLDeformerEditorModel::GetInputsErrorText() const
	{
		if (Model->SkeletalMesh && GetEditorInputInfo()->IsEmpty())
		{
			switch (Model->TrainingInputs)
			{
				case EMLDeformerTrainingInputFilter::BonesOnly:			return FText(LOCTEXT("InputsEmptyBonesErrorText", "Your base mesh has no bones to train on."));
				case EMLDeformerTrainingInputFilter::CurvesOnly:		return FText(LOCTEXT("InputsEmptyCurvesErrorText", "Your base mesh has no curves to train on."));
				case EMLDeformerTrainingInputFilter::BonesAndCurves:	return FText(LOCTEXT("InputsEmptyBonesCurvesErrorText", "Your base mesh has no bones or curves to train on."));
				default: return FText(LOCTEXT("InputsEmptyDefaultErrorText", "There are no inputs to train on. There are no bones, curves or other inputs we can use."));
			}
		}

		return FText();
	}

	FText FMLDeformerEditorModel::GetIncompatibleSkeletonErrorText(USkeletalMesh* InSkelMesh, UAnimSequence* InAnimSeq) const
	{
		FText Result;
		if (InSkelMesh && InAnimSeq)
		{
			if (!InSkelMesh->GetSkeleton()->IsCompatible(InAnimSeq->GetSkeleton()))
			{
				Result = LOCTEXT("SkeletonMismatch", "The base skeletal mesh and anim sequence use different skeletons. The animation might not play correctly.");
			}
		}
		return Result;
	}

	FText FMLDeformerEditorModel::GetTargetAssetChangedErrorText() const
	{
		FText Result;

		UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
		if (Model->HasTrainingGroundTruth() && InputInfo)
		{
			if (Model->NumTargetMeshVerts != InputInfo->GetNumTargetMeshVertices() &&
				Model->NumTargetMeshVerts > 0 && InputInfo->GetNumTargetMeshVertices() > 0)
			{
				Result = FText::Format(LOCTEXT("TargetMeshMismatch", "Number of vertices in target mesh has changed from {0} to {1} vertices since this ML Deformer Asset was saved! {2}"),
					InputInfo->GetNumTargetMeshVertices(),
					Model->NumTargetMeshVerts,
					IsTrained() ? LOCTEXT("BaseMeshMismatchNN", "Model needs to be retrained!") : FText());
			}
		}

		return Result;
	}

	void FMLDeformerEditorModel::InitInputInfo(UMLDeformerInputInfo* InputInfo)
	{
		InputInfo->Reset();

		TArray<FString>& BoneNameStrings = InputInfo->GetBoneNameStrings();
		TArray<FString>& CurveNameStrings = InputInfo->GetCurveNameStrings();
		TArray<FName>& BoneNames = InputInfo->GetBoneNames();
		TArray<FName>& CurveNames = InputInfo->GetCurveNames();
		USkeletalMesh* SkeletalMesh = Model->GetSkeletalMesh();

		BoneNames.Reset();
		BoneNameStrings.Reset();
		BoneNames.Reset();
		CurveNames.Reset();

		InputInfo->SetNumBaseVertices(Model->GetNumBaseMeshVerts());
		InputInfo->SetNumTargetVertices(Model->GetNumTargetMeshVerts());

		const bool bIncludeBones = (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesAndCurves || Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesOnly);
		const bool bIncludeCurves = (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesAndCurves || Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::CurvesOnly);
		const USkeleton* Skeleton = Model->GetSkeletalMesh() ? Model->GetSkeletalMesh()->GetSkeleton() : nullptr;

		// Handle bones.
		if (bIncludeBones && SkeletalMesh)
		{
			// Include all the bones when no list was provided.
			const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
			if (Model->GetBoneIncludeList().IsEmpty())
			{
				// Grab all bone names.
				const int32 NumBones = RefSkeleton.GetNum();
				BoneNameStrings.Reserve(NumBones);
				for (int32 Index = 0; Index < NumBones; ++Index)
				{
					const FName BoneName = RefSkeleton.GetBoneName(Index);
					BoneNameStrings.Add(BoneName.ToString());
					BoneNames.Add(BoneName);
				}
			}
			else // A list of bones to include was provided.
			{
				for (const FBoneReference& BoneReference : Model->GetBoneIncludeList())
				{
					if (BoneReference.BoneName.IsValid())
					{
						const FName BoneName = BoneReference.BoneName;
						if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
						{
							UE_LOG(LogMLDeformer, Warning, TEXT("Bone '%s' in the bones include list doesn't exist, ignoring it."), *BoneName.ToString());
							continue;
						}

						BoneNameStrings.Add(BoneName.ToString());
						BoneNames.Add(BoneName);
					}
				}
			}
		}

		// Handle curves.
		if (bIncludeCurves && SkeletalMesh)
		{
			// Anim curves.
			const FSmartNameMapping* SmartNameMapping = Skeleton ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr;
			if (SmartNameMapping) // When there are curves.
			{
				// Include all curves when no list was provided.
				if (Model->GetCurveIncludeList().IsEmpty())
				{
					SmartNameMapping->FillNameArray(CurveNames);
					CurveNameStrings.Reserve(CurveNames.Num());
					for (const FName Name : CurveNames)
					{
						CurveNameStrings.Add(Name.ToString());
					}
				}
				else // A list of curve names was provided.
				{
					for (const FMLDeformerCurveReference& CurveReference : Model->GetCurveIncludeList())
					{
						if (CurveReference.CurveName.IsValid())
						{
							const FName CurveName = CurveReference.CurveName;
							if (!SmartNameMapping->Exists(CurveName))
							{
								UE_LOG(LogMLDeformer, Warning, TEXT("Curve '%s' doesn't exist, ignoring it."), *CurveName.ToString());
								continue;
							}

							CurveNameStrings.Add(CurveName.ToString());
							CurveNames.Add(CurveName);
						}
					}
				}
			}
		}
	}

	void FMLDeformerEditorModel::InitBoneIncludeListToAnimatedBonesOnly()
	{
		if (!Model->AnimSequence)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Cannot initialize bone list as no Anim Sequence has been picked."));
			return;
		}

		const UAnimDataModel* DataModel = Model->AnimSequence->GetDataModel();
		if (!DataModel)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Anim sequence has no data model."));
			return;
		}

		if (!Model->SkeletalMesh)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has not been set."));
			return;
		}

		USkeleton* Skeleton = Model->SkeletalMesh->GetSkeleton();
		if (!Skeleton)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has no skeleton."));
			return;
		}

		// Iterate over all bones that are both in the skeleton and the animation.
		TArray<FName> AnimatedBoneList;
		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		const int32 NumBones = RefSkeleton.GetNum();
		for (int32 Index = 0; Index < NumBones; ++Index)
		{
			const FName BoneName = RefSkeleton.GetBoneName(Index);
			const int32 BoneTrackIndex = DataModel->GetBoneTrackIndexByName(BoneName);
			if (BoneTrackIndex == INDEX_NONE)
			{
				continue;
			}

			// Check if there is actually animation data.
			const FBoneAnimationTrack& BoneAnimTrack = DataModel->GetBoneTrackByIndex(BoneTrackIndex);
			const TArray<FQuat4f>& Rotations = BoneAnimTrack.InternalTrackData.RotKeys;
			bool bIsAnimated = false;
			if (!Rotations.IsEmpty())
			{
				const FQuat4f FirstQuat = Rotations[0];
				for (const FQuat4f KeyValue : Rotations)
				{
					if (!KeyValue.Equals(FirstQuat))
					{
						bIsAnimated = true;
						break;
					}
				}

				if (!bIsAnimated)
				{
					UE_LOG(LogMLDeformer, Display, TEXT("Bone '%s' has keyframes but isn't animated."), *BoneName.ToString());
				}
			}

			if (bIsAnimated)
			{
				AnimatedBoneList.Add(BoneName);
			}
		}

		// Init the bone include list using the animated bones.
		if (!AnimatedBoneList.IsEmpty())
		{
			Model->BoneIncludeList.Empty();
			Model->BoneIncludeList.Reserve(AnimatedBoneList.Num());
			for (FName BoneName : AnimatedBoneList)
			{
				Model->BoneIncludeList.AddDefaulted();
				FBoneReference& BoneRef = Model->BoneIncludeList.Last();
				BoneRef.BoneName = BoneName;
			}
		}
		else
		{
			Model->BoneIncludeList.Empty();
			UE_LOG(LogMLDeformer, Warning, TEXT("There are no animated bone rotations in Anim Sequence '%s'."), *(Model->AnimSequence->GetName()));
		}
	}

	void FMLDeformerEditorModel::InitCurveIncludeListToAnimatedCurvesOnly()
	{
		if (!Model->AnimSequence)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Cannot initialize curve list as no Anim Sequence has been picked."));
			return;
		}

		const UAnimDataModel* DataModel = Model->AnimSequence->GetDataModel();
		if (!DataModel)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Anim sequence has no data model."));
			return;
		}

		if (!Model->SkeletalMesh)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has not been set."));
			return;
		}

		USkeleton* Skeleton = Model->SkeletalMesh->GetSkeleton();
		if (!Skeleton)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Skeletal Mesh has no skeleton."));
			return;
		}

		// Iterate over all curves that are both in the skeleton and the animation.
		TArray<FName> AnimatedCurveList;
		const FSmartNameMapping* Mapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
		if (Mapping)
		{
			TArray<FName> SkeletonCurveNames;
			Mapping->FillNameArray(SkeletonCurveNames);
			for (const FName& SkeletonCurveName : SkeletonCurveNames)
			{
				const TArray<FFloatCurve>& AnimCurves = DataModel->GetFloatCurves();
				for (const FFloatCurve& AnimCurve : AnimCurves)
				{
					if (AnimCurve.Name.IsValid() && AnimCurve.Name.DisplayName == SkeletonCurveName)
					{
						TArray<float> TimeValues;
						TArray<float> KeyValues;
						AnimCurve.GetKeys(TimeValues, KeyValues);
						if (KeyValues.Num() > 0)
						{
							const float FirstKeyValue = KeyValues[0];					
							for (float CurKeyValue : KeyValues)
							{
								if (CurKeyValue != FirstKeyValue)
								{
									AnimatedCurveList.Add(SkeletonCurveName);
									break;
								}
							}
						}
						break;
					}
				}
			}
		}

		// Init the bone include list using the animated bones.
		if (!AnimatedCurveList.IsEmpty())
		{
			Model->CurveIncludeList.Empty();
			Model->CurveIncludeList.Reserve(AnimatedCurveList.Num());
			for (FName CurveName : AnimatedCurveList)
			{
				Model->CurveIncludeList.AddDefaulted();
				FMLDeformerCurveReference& CurveRef = Model->CurveIncludeList.Last();
				CurveRef.CurveName = CurveName;
			}
		}
		else
		{
			Model->CurveIncludeList.Empty();
			UE_LOG(LogMLDeformer, Warning, TEXT("There are no animated curves in Anim Sequence '%s'."), *(Model->AnimSequence->GetName()));
		}
	}

	void FMLDeformerEditorModel::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
	{
		// Make sure that before we render anything, that our sampler is ready.
		if (!Sampler->IsInitialized())
		{
			Sampler->Init(this); // This can still fail.
			Sampler->SetVertexDeltaSpace(EVertexDeltaSpace::PostSkinning);
			if (Sampler->IsInitialized()) // If we actually managed to initialize this frame.
			{
				SampleDeltas(); // Update the deltas.
			}
		}

		const UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
		{
			// Draw the deltas for the current frame.
			const TArray<float>& VertexDeltas = Sampler->GetVertexDeltas();
			const TArray<FVector3f>& LinearSkinnedPositions = Sampler->GetSkinnedVertexPositions();
			if (VizSettings->GetDrawVertexDeltas() && (VertexDeltas.Num() / 3) == LinearSkinnedPositions.Num())
			{
				const FLinearColor DeltasColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.Deltas.Color");
				const FLinearColor DebugVectorsColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color");
				const FLinearColor DebugVectorsColor2 = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color2");
				const uint8 DepthGroup = VizSettings->GetXRayDeltas() ? 100 : 0;
				for (int32 Index = 0; Index < LinearSkinnedPositions.Num(); ++Index)
				{
					const int32 ArrayIndex = 3 * Index;
					const FVector Delta(
						VertexDeltas[ArrayIndex], 
						VertexDeltas[ArrayIndex + 1], 
						VertexDeltas[ArrayIndex + 2]);
					const FVector VertexPos = (FVector)LinearSkinnedPositions[Index];
					PDI->DrawLine(VertexPos, VertexPos + Delta, DeltasColor, DepthGroup);
				}
			}
		}
	}

	void FMLDeformerEditorModel::SampleDeltas()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		ClampCurrentFrameIndex();

		// If we have no Persona toolkit yet, then it is not yet safe to init the sampler.
		if (Editor->GetPersonaToolkitPointer() != nullptr)
		{
			Sampler->Init(this);
		}

		if (Sampler->IsInitialized())
		{
			Sampler->SetVertexDeltaSpace(EVertexDeltaSpace::PostSkinning);
			Sampler->Sample(VizSettings->FrameNumber);
		}
	}

	void FMLDeformerEditorModel::CheckTrainingDataFrameChanged()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		ClampCurrentFrameIndex();
		if (CurrentTrainingFrame != VizSettings->FrameNumber)
		{
			OnTrainingDataFrameChanged();
		}
	}

	void FMLDeformerEditorModel::OnTrainingDataFrameChanged()
	{
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();

		// If the current frame number changed, re-sample the deltas if needed.
		if (CurrentTrainingFrame != VizSettings->FrameNumber)
		{
			CurrentTrainingFrame = VizSettings->FrameNumber;
			if (VizSettings->GetDrawVertexDeltas() && VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
			{
				SampleDeltas();
			}
		}
	}

	void FMLDeformerEditorModel::RefreshMLDeformerComponents()
	{
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor && EditorActor->GetMLDeformerComponent())
			{
				USkeletalMeshComponent* SkelMeshComponent = EditorActor ? EditorActor->GetSkeletalMeshComponent() : nullptr;
				UMLDeformerAsset* DeformerAsset = GetModel()->GetDeformerAsset();
				EditorActor->GetMLDeformerComponent()->SetupComponent(DeformerAsset, SkelMeshComponent);
				UMLDeformerModelInstance* ModelInstance = EditorActor->GetMLDeformerComponent()->GetModelInstance();
				if (ModelInstance)
				{
					ModelInstance->UpdateCompatibilityStatus();
				}
			}
		}
	}

	void FMLDeformerEditorModel::CreateHeatMapMaterial()
	{
		const FString HeatMapMaterialPath = GetHeatMapMaterialPath();
		UObject* MaterialObject = StaticLoadObject(UMaterial::StaticClass(), nullptr, *HeatMapMaterialPath);
		HeatMapMaterial = Cast<UMaterial>(MaterialObject);
	}

	void FMLDeformerEditorModel::CreateHeatMapDeformerGraph()
	{
		const FString HeatMapDeformerPath = GetHeatMapDeformerGraphPath();
		UObject* DeformerObject = StaticLoadObject(UMeshDeformer::StaticClass(), nullptr, *HeatMapDeformerPath);
		HeatMapDeformerGraph = Cast<UMeshDeformer>(DeformerObject);
	}

	void FMLDeformerEditorModel::CreateHeatMapAssets()
	{
		CreateHeatMapMaterial();
		CreateHeatMapDeformerGraph();
	}

	void FMLDeformerEditorModel::SetHeatMapMaterialEnabled(bool bEnabled)
	{
		FMLDeformerEditorActor* EditorActor = FindEditorActor(ActorID_Test_MLDeformed);
		if (EditorActor == nullptr)
		{
			return;
		}

		USkeletalMeshComponent* Component = EditorActor->GetSkeletalMeshComponent();
		if (Component)
		{
			if (bEnabled)
			{
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					Component->SetMaterial(Index, HeatMapMaterial);
				}
			}
			else
			{
				Component->EmptyOverrideMaterials();
			}
		}

		UpdateDeformerGraph();
	}

	UMeshDeformer* FMLDeformerEditorModel::LoadDefaultDeformerGraph()
	{
		const FString GraphAssetPath = GetDefaultDeformerGraphAssetPath();
		UObject* Object = StaticLoadObject(UMeshDeformer::StaticClass(), nullptr, *GraphAssetPath);
		UMeshDeformer* DeformerGraph = Cast<UMeshDeformer>(Object);
		if (DeformerGraph == nullptr)
		{
			UE_LOG(LogMLDeformer, Warning, TEXT("Failed to load default ML Deformer compute graph from: %s"), *GraphAssetPath);
		}
		else
		{
			UE_LOG(LogMLDeformer, Verbose, TEXT("Loaded default ML Deformer compute graph from: %s"), *GraphAssetPath);
		}

		return DeformerGraph;
	}

	void FMLDeformerEditorModel::SetDefaultDeformerGraphIfNeeded()
	{
		// Initialize the asset on the default plugin deformer graph.
		UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings && VizSettings->GetDeformerGraph() == nullptr)
		{
			UMeshDeformer* DefaultGraph = LoadDefaultDeformerGraph();
			VizSettings->SetDeformerGraph(DefaultGraph);
		}
	}

	FText FMLDeformerEditorModel::GetOverlayText() const
	{
		const FMLDeformerEditorActor* EditorActor = FindEditorActor(ActorID_Test_MLDeformed);
		const UMLDeformerComponent* DeformerComponent = EditorActor ? EditorActor->GetMLDeformerComponent() : nullptr;
		if (DeformerComponent)
		{
			const UMLDeformerModelInstance* ModelInstance = DeformerComponent->GetModelInstance();
			if (ModelInstance &&
				ModelInstance->GetSkeletalMeshComponent() && 
				ModelInstance->GetSkeletalMeshComponent()->SkeletalMesh &&
				!ModelInstance->IsCompatible() )
			{
				return FText::FromString( ModelInstance->GetCompatibilityErrorText() );
			}
		}
	 
		return FText::GetEmpty();
	}

	void FMLDeformerEditorModel::UpdateDeformerGraph()
	{	
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			USkeletalMeshComponent* SkelMeshComponent = EditorActor->GetSkeletalMeshComponent();
			if (EditorActor->GetMLDeformerComponent() == nullptr || SkelMeshComponent == nullptr)
			{
				continue;
			}

			if (SkelMeshComponent)
			{
				UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
				UMeshDeformer* MeshDeformer = IsTrained() ? VizSettings->GetDeformerGraph() : nullptr;	
				const bool bUseHeatMapDeformer = VizSettings->GetShowHeatMap();
				SkelMeshComponent->SetMeshDeformer(bUseHeatMapDeformer ? HeatMapDeformerGraph.Get() : MeshDeformer);
			}
		}
	}

	void FMLDeformerEditorModel::OnPostTraining(ETrainingResult TrainingResult)
	{
		for (FMLDeformerEditorActor* EditorActor : EditorActors)
		{
			if (EditorActor->GetMLDeformerComponent())
			{
				USkeletalMeshComponent* SkelMeshComponent = EditorActor ? EditorActor->GetSkeletalMeshComponent() : nullptr;
				UMLDeformerAsset* DeformerAsset = Model->GetDeformerAsset();
				EditorActor->GetMLDeformerComponent()->SetupComponent(DeformerAsset, SkelMeshComponent);
			}
		}

		if (TrainingResult == ETrainingResult::Success || TrainingResult == ETrainingResult::Aborted)
		{
			// The InitAssets call resets the normalized flag, so set it back to true.
			// This is safe as we finished training, which means we already normalized data.
			// If we aborted we still have normalized the data. Only when we have AbortedCantUse then we canceled the normalization process.
			bIsDataNormalized = true;
		}
	}

	FMLDeformerEditorActor* FMLDeformerEditorModel::GetTimelineEditorActor() const
	{
		const UMLDeformerVizSettings* VizSettings = Model->GetVizSettings();
		if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
		{
			return FindEditorActor(ActorID_Train_GroundTruth);
		}
		else if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData)
		{
			return FindEditorActor(ActorID_Test_GroundTruth);
		}
		return nullptr;
	}

	UNeuralNetwork* FMLDeformerEditorModel::LoadNeuralNetworkFromOnnx(const FString& Filename) const
	{
		FString OnnxFile = FPaths::ConvertRelativePathToFull(Filename);
		if (FPaths::FileExists(OnnxFile))
		{
			UE_LOG(LogMLDeformer, Display, TEXT("Loading Onnx file '%s'..."), *OnnxFile);
			UNeuralNetwork* Result = NewObject<UNeuralNetwork>(Model, UNeuralNetwork::StaticClass());		
			if (Result->Load(OnnxFile))
			{
				Result->SetDeviceType(ENeuralDeviceType::GPU, ENeuralDeviceType::CPU, ENeuralDeviceType::GPU);	
				UE_LOG(LogMLDeformer, Display, TEXT("Successfully loaded Onnx file '%s'..."), *OnnxFile);
				return Result;
			}
			else
			{
				UE_LOG(LogMLDeformer, Error, TEXT("Failed to load Onnx file '%s'"), *OnnxFile);
			}
		}
		else
		{
			UE_LOG(LogMLDeformer, Error, TEXT("Onnx file '%s' does not exist!"), *OnnxFile);
		}

		return nullptr;
	}

	bool FMLDeformerEditorModel::IsEditorReadyForTrainingBasicChecks()
	{
		// Make sure we have picked required assets.
		if (!Model->HasTrainingGroundTruth() ||
			Model->GetAnimSequence() == nullptr ||
			Model->GetSkeletalMesh() == nullptr ||
			GetNumFrames() == 0)
		{
			return false;
		}

		// Make sure we have inputs.
		UpdateEditorInputInfo();
		if (GetEditorInputInfo()->IsEmpty())
		{
			return false;
		}

		return true;
	}

	bool FMLDeformerEditorModel::LoadTrainedNetwork() const
	{
		const FString OnnxFile = GetTrainedNetworkOnnxFile();
		UNeuralNetwork* Network = LoadNeuralNetworkFromOnnx(OnnxFile);
		if (Network)
		{
			Model->SetNeuralNetwork(Network);
			return true;
		}

		return false;
	}

	bool FMLDeformerEditorModel::IsTrained() const
	{
		return (Model->GetNeuralNetwork() != nullptr);
	}

	void FMLDeformerEditorModel::TriggerInputAssetChanged(bool RefreshVizSettings)
	{
		OnInputAssetsChanged();
		OnPostInputAssetChanged();
		GetEditor()->GetModelDetailsView()->ForceRefresh();
		if (RefreshVizSettings)
		{
			GetEditor()->GetVizSettingsDetailsView()->ForceRefresh();
		}
	}

	FString FMLDeformerEditorModel::GetHeatMapMaterialPath() const
	{
		return FString(TEXT("/MLDeformerFramework/Materials/MLDeformerHeatMapMat.MLDeformerHeatMapMat"));
	}

	FString FMLDeformerEditorModel::GetHeatMapDeformerGraphPath() const
	{
		return FString(TEXT("/MLDeformerFramework/Deformers/DG_MLDeformerModel_HeatMap.DG_MLDeformerModel_HeatMap"));
	}

	FString FMLDeformerEditorModel::GetDefaultDeformerGraphAssetPath() const
	{
		return FString(TEXT("/MLDeformerFramework/Deformers/DG_MLDeformerModel.DG_MLDeformerModel"));
	}

	FString FMLDeformerEditorModel::GetTrainedNetworkOnnxFile() const
	{
		return FString(FPaths::ProjectIntermediateDir() + TEXT("MLDeformerNetwork.onnx"));
	}
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE
