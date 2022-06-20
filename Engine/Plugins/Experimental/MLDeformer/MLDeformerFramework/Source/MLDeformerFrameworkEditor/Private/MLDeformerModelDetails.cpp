// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModelDetails.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerEditorModule.h"
#include "NeuralNetwork.h"
#include "GeometryCache.h"
#include "GeometryCacheTrack.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Modules/ModuleManager.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailsView.h"
#include "IDetailGroup.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "DetailLayoutBuilder.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "MLDeformerModelDetails"

namespace UE::MLDeformer
{
	bool FMLDeformerModelDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		Model = nullptr;
		EditorModel = nullptr;
		if (Objects.Num() == 1 && Objects[0] != nullptr)
		{
			Model = static_cast<UMLDeformerModel*>(Objects[0].Get());

			// Get the editor model for this runtime model.
			FMLDeformerEditorModule& EditorModule = FModuleManager::GetModuleChecked<FMLDeformerEditorModule>("MLDeformerFrameworkEditor");
			if (Model)
			{
				EditorModel = EditorModule.GetModelRegistry().GetEditorModel(Model);
			}
		}

		return (Model != nullptr && EditorModel != nullptr);
	}

	void FMLDeformerModelDetails::CreateCategories()
	{
		BaseMeshCategoryBuilder = &DetailLayoutBuilder->EditCategory("Base Mesh", FText::GetEmpty(), ECategoryPriority::Important);
		TargetMeshCategoryBuilder = &DetailLayoutBuilder->EditCategory("Target Mesh", FText::GetEmpty(), ECategoryPriority::Important);
		InputOutputCategoryBuilder = &DetailLayoutBuilder->EditCategory("Inputs and Output", FText::GetEmpty(), ECategoryPriority::Important);
		SettingsCategoryBuilder = &DetailLayoutBuilder->EditCategory("Training Settings", FText::GetEmpty(), ECategoryPriority::Important);
	}

	void FMLDeformerModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		DetailLayoutBuilder = &DetailBuilder;

		// Update the pointers and check if they are valid.
		TArray<TWeakObjectPtr<UObject>> Objects;
		DetailBuilder.GetObjectsBeingCustomized(Objects);
		if (!UpdateMemberPointers(Objects))
		{
			return;
		}

		CreateCategories();

		// Base mesh details.
		BaseMeshCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, SkeletalMesh), UMLDeformerModel::StaticClass());

		AddBaseMeshErrors();

		// Check if the vertex counts of our asset have changed.
		const FText ChangedErrorText = EditorModel->GetBaseAssetChangedErrorText();
		FDetailWidgetRow& ChangedErrorRow = BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("BaseMeshChangedError"))
			.Visibility(!ChangedErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(ChangedErrorText)
				]
			];

		// Check if our skeletal mesh's imported model contains a list of mesh infos. If not, we need to reimport it as it is an older asset.
		const FText NeedsReimportErrorText = EditorModel->GetSkeletalMeshNeedsReimportErrorText();
		FDetailWidgetRow& NeedsReimportErrorRow = BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("BaseMeshNeedsReimportError"))
			.Visibility(!NeedsReimportErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(NeedsReimportErrorText)
				]
			];

		const FText VertexMapMisMatchErrorText = EditorModel->GetVertexMapChangedErrorText();
		FDetailWidgetRow& VertexMapErrorRow = BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("VertexMapError"))
			.Visibility(!VertexMapMisMatchErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(VertexMapMisMatchErrorText)
				]
			];

		// Animation sequence.
		IDetailPropertyRow& AnimRow = BaseMeshCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AnimSequence), UMLDeformerModel::StaticClass());
		AnimRow.CustomWidget()
		.NameContent()
		[
			AnimRow.GetPropertyHandle()->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SObjectPropertyEntryBox)
			.PropertyHandle(AnimRow.GetPropertyHandle())
			.AllowedClass(UAnimSequence::StaticClass())
			.ObjectPath(Model ? Model->GetAnimSequence()->GetPathName() : FString())
			.ThumbnailPool(DetailBuilder.GetThumbnailPool())
			.OnShouldFilterAsset(
				this, 
				&FMLDeformerModelDetails::FilterAnimSequences, 
				Model->GetSkeletalMesh() ? Model->GetSkeletalMesh()->GetSkeleton() : nullptr
			)
		];

		AddAnimSequenceErrors();

		const FText AnimErrorText = EditorModel->GetIncompatibleSkeletonErrorText(Model->GetSkeletalMesh(), Model->AnimSequence);
		FDetailWidgetRow& AnimErrorRow = BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("AnimSkeletonMisMatchError"))
			.Visibility(!AnimErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(AnimErrorText)
				]
			];


		AddTargetMesh();

		TargetMeshCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, AlignmentTransform), UMLDeformerModel::StaticClass());

		// Input and output.
		InputOutputCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, TrainingInputs), UMLDeformerModel::StaticClass());

		AddTrainingInputErrors();

		const FText ErrorText = EditorModel->GetInputsErrorText();
		FDetailWidgetRow& ErrorRow = InputOutputCategoryBuilder->AddCustomRow(FText::FromString("InputsError"))
			.Visibility(!ErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(ErrorText)
				]
			];

		InputOutputCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, MaxTrainingFrames), UMLDeformerModel::StaticClass());
		InputOutputCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, DeltaCutoffLength), UMLDeformerModel::StaticClass());

		// Bone include list group.
		IDetailGroup& BoneIncludeGroup = InputOutputCategoryBuilder->AddGroup("BoneIncludeGroup", LOCTEXT("BoneIncludeGroup", "Bones"), false, false);
		BoneIncludeGroup.AddWidgetRow()
			.ValueContent()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("AnimatedBonesButton", "Animated Bones Only"))
				.OnClicked(FOnClicked::CreateSP(this, &FMLDeformerModelDetails::OnFilterAnimatedBonesOnly))
				.IsEnabled_Lambda([this](){ return (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesAndCurves) || (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesOnly); })
			];
		BoneIncludeGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, BoneIncludeList), UMLDeformerModel::StaticClass()));

		AddBoneInputErrors();

		// Curve include list group.
		IDetailGroup& CurveIncludeGroup = InputOutputCategoryBuilder->AddGroup("CurveIncludeGroup", LOCTEXT("CurveIncludeGroup", "Curves"), false, false);
		CurveIncludeGroup.AddWidgetRow()
			.ValueContent()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("AnimatedCurvesButton", "Animated Curves Only"))
				.OnClicked(FOnClicked::CreateSP(this, &FMLDeformerModelDetails::OnFilterAnimatedCurvesOnly))
				.IsEnabled_Lambda([this](){ return (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::BonesAndCurves) || (Model->GetTrainingInputs() == EMLDeformerTrainingInputFilter::CurvesOnly); })
			];
		CurveIncludeGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMLDeformerModel, CurveIncludeList), UMLDeformerModel::StaticClass()));

		AddCurveInputErrors();

		// Show a warning when no neural network has been set.
		{		
			UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
			FDetailWidgetRow& NeuralNetErrorRow = SettingsCategoryBuilder->AddCustomRow(FText::FromString("NeuralNetError"))
				.Visibility((NeuralNetwork == nullptr) ? EVisibility::Visible : EVisibility::Collapsed)
				.WholeRowContent()
				[
					SNew(SBox)
					.Padding(FMargin(0.0f, 4.0f))
					[
						SNew(SWarningOrErrorBox)
						.MessageStyle(EMessageStyle::Warning)
						.Message(FText::FromString("Model still needs to be trained."))
					]
				];

			// Check if our network is compatible with the skeletal mesh.
			if (Model->GetSkeletalMesh() && NeuralNetwork)
			{
				FDetailWidgetRow& NeuralNetIncompatibleErrorRow = SettingsCategoryBuilder->AddCustomRow(FText::FromString("NeuralNetIncompatibleError"))
					.Visibility(!Model->GetInputInfo()->IsCompatible(Model->GetSkeletalMesh()) ? EVisibility::Visible : EVisibility::Collapsed)
					.WholeRowContent()
					[
						SNew(SBox)
						.Padding(FMargin(0.0f, 4.0f))
						[
							SNew(SWarningOrErrorBox)
							.MessageStyle(EMessageStyle::Error)
							.Message(FText::FromString("Trained neural network is incompatible with selected SkeletalMesh."))
						]
					];
			}
		}
	}

	bool FMLDeformerModelDetails::FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton)
	{
		if (Skeleton && Skeleton->IsCompatibleSkeletonByAssetData(AssetData))
		{
			return false;
		}

		return true;
	}

	FReply FMLDeformerModelDetails::OnFilterAnimatedBonesOnly() const
	{
		EditorModel->InitBoneIncludeListToAnimatedBonesOnly();
		DetailLayoutBuilder->ForceRefreshDetails();
		return FReply::Handled();
	}

	FReply FMLDeformerModelDetails::OnFilterAnimatedCurvesOnly() const
	{
		EditorModel->InitCurveIncludeListToAnimatedCurvesOnly();
		DetailLayoutBuilder->ForceRefreshDetails();
		return FReply::Handled();
	}
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE
