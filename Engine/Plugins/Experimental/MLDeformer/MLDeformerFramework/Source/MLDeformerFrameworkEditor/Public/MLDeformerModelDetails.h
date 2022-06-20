// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class USkeleton;
class IDetailCategoryBuilder;
class UMLDeformerModel;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerModelDetails
		: public IDetailCustomization
	{
	public:
		// ILayoutDetails overrides.
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		// ~END ILayoutDetails overrides.

		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects);
		virtual void CreateCategories();
		virtual void AddTargetMesh() {}
		virtual void AddBaseMeshErrors() {}
		virtual void AddAnimSequenceErrors() {}
		virtual void AddTrainingInputErrors() {}
		virtual void AddBoneInputErrors() {}
		virtual void AddCurveInputErrors() {}

	protected:
		bool FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton);
		FReply OnFilterAnimatedBonesOnly() const;
		FReply OnFilterAnimatedCurvesOnly() const;


	protected:
		/** Associated detail layout builder. */
		IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;

		TObjectPtr<UMLDeformerModel> Model = nullptr;
		FMLDeformerEditorModel* EditorModel = nullptr;

		IDetailCategoryBuilder* BaseMeshCategoryBuilder = nullptr;
		IDetailCategoryBuilder* TargetMeshCategoryBuilder = nullptr;
		IDetailCategoryBuilder* InputOutputCategoryBuilder = nullptr;
		IDetailCategoryBuilder* SettingsCategoryBuilder = nullptr;
	};
}	// namespace UE::MLDeformer
