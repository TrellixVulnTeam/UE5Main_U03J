// Copyright Epic Games, Inc. All Rights Reserved.

#include "LegacyVertexDeltaModelDetails.h"
#include "LegacyVertexDeltaEditorModel.h"
#include "LegacyVertexDeltaModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerModel.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerEditorModule.h"
#include "MLDeformerGeomCacheHelpers.h"
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

#define LOCTEXT_NAMESPACE "LegacyVertexDeltaModelDetails"

namespace UE::LegacyVertexDeltaModel
{
	using namespace UE::MLDeformer;

	TSharedRef<IDetailCustomization> FLegacyVertexDeltaModelDetails::MakeInstance()
	{
		return MakeShareable(new FLegacyVertexDeltaModelDetails());
	}

	bool FLegacyVertexDeltaModelDetails::UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		if (!FMLDeformerModelDetails::UpdateMemberPointers(Objects))
		{
			return false;
		}

		VertexModel = Cast<ULegacyVertexDeltaModel>(Model);
		check(VertexModel);
		VertexEditorModel = static_cast<FLegacyVertexDeltaEditorModel*>(EditorModel);

		return (VertexModel != nullptr && VertexEditorModel != nullptr);
	}

	void FLegacyVertexDeltaModelDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
	{
		// Create all the detail categories and add the properties of the base class.
		FMLDeformerModelDetails::CustomizeDetails(DetailBuilder);

		// Training settings.
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, NumHiddenLayers));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, NumNeuronsPerLayer));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, Epochs));

		// Check whether shrinkage settings should be visible or not.
		auto IsShrinkageVisible = [this]()
		{
			return (VertexModel->GetLossFunction() == ELegacyVertexDeltaModelLossFunction::Shrinkage) ? EVisibility::Visible : EVisibility::Collapsed;
		};
	 
		// Advanced settings.
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, BatchSize));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, LearningRate));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, ActivationFunction));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, LossFunction));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, ShrinkageSpeed))
			.Visibility(TAttribute<EVisibility>::CreateLambda(IsShrinkageVisible));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, ShrinkageThreshold))
			.Visibility(TAttribute<EVisibility>::CreateLambda(IsShrinkageVisible));
		SettingsCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, MaxCacheSizeGB));
	}

	void FLegacyVertexDeltaModelDetails::AddBaseMeshErrors()
	{
		// Check if the base mesh matches the target mesh vertex count.
		FText ErrorText = GetGeomCacheVertexErrorText(Model->SkeletalMesh, VertexModel->GetGeometryCache(), FText::FromString("Base Mesh"), FText::FromString("Target Mesh"));
		BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("BaseMeshError"))
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
	}
	
	void FLegacyVertexDeltaModelDetails::AddAnimSequenceErrors()
	{
		const FText WarningText = GetGeomCacheAnimSequenceErrorText(VertexModel->GetGeometryCache(), Model->GetAnimSequence());
		BaseMeshCategoryBuilder->AddCustomRow(FText::FromString("AnimSeqWarning"))
			.Visibility(!WarningText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Warning)
					.Message(WarningText)
				]
			];
	}

	void FLegacyVertexDeltaModelDetails::AddTargetMesh()
	{
		TargetMeshCategoryBuilder->AddProperty(GET_MEMBER_NAME_CHECKED(ULegacyVertexDeltaModel, GeometryCache));

		const FText TargetMeshErrorText = GetGeomCacheErrorText(VertexModel->GetSkeletalMesh(), VertexModel->GetGeometryCache());
		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("TargetMeshError"))
			.Visibility(!TargetMeshErrorText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(TargetMeshErrorText)
				]
			];

		const FText ChangedErrorText = EditorModel->GetTargetAssetChangedErrorText();
		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("TargetMeshChangedError"))
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

		// Show meshes that have no matching geometry track list.
		const FText MeshMappingErrorList = GetGeomCacheMeshMappingErrorText(Model->GetSkeletalMesh(), VertexModel->GetGeometryCache());
		FString GeomTrackNameList;
		if (!MeshMappingErrorList.IsEmpty())
		{
			UGeometryCache* GeomCache = VertexModel->GetGeometryCache();
			for (int32 Index = 0; Index < GeomCache->Tracks.Num(); ++Index)
			{
				GeomTrackNameList += GeomCache->Tracks[Index]->GetName();
				if (Index < GeomCache->Tracks.Num() - 1)
				{
					GeomTrackNameList += TEXT("\n");
				}
			}
		}

		FText MeshMappingErrorFull = FText::Format(
			LOCTEXT("MeshMappingError", "No matching GeomCache Tracks names found for meshes:\n{0}\n\nGeomCache Track List:\n{1}"), 
			MeshMappingErrorList,
			FText::FromString(GeomTrackNameList));

		TargetMeshCategoryBuilder->AddCustomRow(FText::FromString("MeshMappingError"))
			.Visibility(!MeshMappingErrorList.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.0f, 4.0f))
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Message(MeshMappingErrorFull)
				]
			];
	}
}	// namespace UE::LegacyVertexDeltaModel

#undef LOCTEXT_NAMESPACE
