// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyComponent.h"
#include "WaterBodyRiverComponent.generated.h"

class UMaterialInstanceDynamic;
class USplineMeshComponent;

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API UWaterBodyRiverComponent : public UWaterBodyComponent
{
	GENERATED_UCLASS_BODY()
	friend class AWaterBodyRiver;
public:
	/** UWaterBodyComponent Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::River; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual TArray<UPrimitiveComponent*> GetStandardRenderableComponents() const override;
	virtual UMaterialInstanceDynamic* GetRiverToLakeTransitionMaterialInstance() override;
	virtual UMaterialInstanceDynamic* GetRiverToOceanTransitionMaterialInstance() override;

#if WITH_EDITOR
	virtual TArray<UPrimitiveComponent*> GetBrushRenderableComponents() const override;
#endif //WITH_EDITOR

	void SetLakeTransitionMaterial(UMaterialInterface* InMat);
	void SetOceanTransitionMaterial(UMaterialInterface* InMat);

	float GetShapeDilationZOffsetFar() const { return ShapeDilationZOffsetFar; }

protected:
	/** UWaterBodyComponent Interface */
	virtual void Reset() override;
	virtual void UpdateMaterialInstances() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	virtual void GenerateWaterBodyMesh() override;

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged) override;

	virtual const TCHAR* GetWaterSpriteTextureName() const override;
#endif

	void CreateOrUpdateLakeTransitionMID();
	void CreateOrUpdateOceanTransitionMID();

	void GenerateMeshes();
	void UpdateSplineMesh(USplineMeshComponent* MeshComp, int32 SplinePointIndex);

protected:
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<USplineMeshComponent*> SplineMeshComponents;

	/** Material used when a river is overlapping a lake. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "River to Lake Transition"))
	UMaterialInterface* LakeTransitionMaterial;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "LakeTransitionMaterial"))
	UMaterialInstanceDynamic* LakeTransitionMID;

	/** This is the material used when a river is overlapping the ocean. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "River to Ocean Transition"))
	UMaterialInterface* OceanTransitionMaterial;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "OceanTransitionMaterial"))
	UMaterialInstanceDynamic* OceanTransitionMID;

	/** How far to push down the furthest vertices of the dilated portion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Rendering)
	float ShapeDilationZOffsetFar = -128.f;
};