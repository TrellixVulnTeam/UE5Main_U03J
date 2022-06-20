// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericAssetsPipeline.generated.h"

class UInterchangeGenericAnimationPipeline;
class UInterchangeGenericMaterialPipeline;
class UInterchangeGenericMeshPipeline;
class UInterchangeGenericTexturePipeline;

/**
 * This pipeline is the generic pipeline option for all meshes type and should be call before specialized Mesh pipeline (like generic static mesh or skeletal mesh pipelines)
 * All shared import options between mesh type should be added here.
 *
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericAssetsPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	UInterchangeGenericAssetsPipeline();
	
	//////	COMMON_CATEGORY Properties //////
	/* Allow user to choose the re-import strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	EReimportStrategyFlags ReimportStrategy = EReimportStrategyFlags::ApplyNoProperties;

	/** If enable and there is only one asset and one source data, we will name the asset like the source data name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common")
	bool bUseSourceNameForAsset = true;

	/** Translation offset applied to meshes and animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (DisplayName = "Offset Translation"))
	FVector ImportOffsetTranslation;

	/** Rotation offset applied to meshes and animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (DisplayName = "Offset Rotation"))
	FRotator ImportOffsetRotation;

	/** Uniform scale offset applied to meshes and animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (DisplayName = "Offset Uniform Scale"))
	float ImportOffsetUniformScale;

	//////	COMMON_MESHES_CATEGORY Properties //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Common Meshes")
	TObjectPtr<UInterchangeGenericCommonMeshesProperties> CommonMeshesProperties;
		
	//////  COMMON_SKELETAL_ANIMATIONS_CATEGORY //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Common Skeletal Meshes and Animations")
	TObjectPtr<UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties> CommonSkeletalMeshesAndAnimationsProperties;

	//////	MESHES_CATEGORY Properties //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Meshes")
	TObjectPtr<UInterchangeGenericMeshPipeline> MeshPipeline;

	//////	ANIMATIONS_CATEGORY Properties //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Animation")
	TObjectPtr<UInterchangeGenericAnimationPipeline> AnimationPipeline;

	//////	MATERIALS_CATEGORY Properties //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Materials")
	TObjectPtr<UInterchangeGenericMaterialPipeline> MaterialPipeline;

	//////	TEXTURES_CATEGORY Properties //////
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Textures")
	TObjectPtr<UInterchangeGenericTexturePipeline> TexturePipeline;

	virtual void PreDialogCleanup(const FName PipelineStackName) override;

	virtual bool IsSettingsAreValid() const override;

	virtual void SetupReimportData(TObjectPtr<UObject> ReimportObject) override;

protected:

	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		//If a blueprint or python derived from this class, it will be execute on the game thread since we cannot currently execute script outside of the game thread, even if this function return true.
		return true;
	}

	virtual void SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex) override;

	//virtual bool ExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer) override;
private:
	
	/**
	 * Implement pipeline option bUseSourceNameForAsset
	 */
	void ImplementUseSourceNameForAssetOption();

	UInterchangeBaseNodeContainer* BaseNodeContainer;
	TArray<const UInterchangeSourceData*> SourceDatas;
	
};


