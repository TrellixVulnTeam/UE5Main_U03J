// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "StaticMeshResources.h"
#include "DynamicMeshBuilder.h"

class UWaterBodyComponent;
class UWaterBodyLakeComponent;
class UWaterBodyOceanComponent;
class UWaterBodyRiverComponent;
class FWaterBodyMeshSection;
class FMaterialRenderProxy;

class FWaterBodySceneProxy final : public FPrimitiveSceneProxy
{
public:
	FWaterBodySceneProxy(UWaterBodyComponent* Component, const TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices);
	virtual ~FWaterBodySceneProxy();
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	uint32 GetAllocatedSize() const;

	bool IsShown(const FSceneView* View) const;

	bool IsWithinWaterInfoPass() const { return bWithinWaterInfoPass; }
	void SetWithinWaterInfoPass(bool bInWithinWaterInfoPass);

private:
	void InitResources(FWaterBodyMeshSection* Section);

	TArray<FWaterBodyMeshSection*> Sections;
	FMaterialRenderProxy* Material;

	bool bWithinWaterInfoPass = false;
};

