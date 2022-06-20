// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerGeomCacheSampler.h"
#include "LegacyVertexDeltaModel.h"
#include "MLDeformerEditorActor.h"
#include "UObject/GCObject.h"

class UMLDeformerAsset;
class USkeletalMesh;
class UGeometryCache;
class ULegacyVertexDeltaModel;
class ULegacyVertexDeltaModelVizSettings;

namespace UE::LegacyVertexDeltaModel
{
	using namespace UE::MLDeformer;

	class FLegacyVertexDeltaEditorModelActor;
	class FLegacyVertexDeltaModelSampler;

	class LEGACYVERTEXDELTAMODELEDITOR_API FLegacyVertexDeltaEditorModel
		: public UE::MLDeformer::FMLDeformerEditorModel
	{
	public:
		virtual ~FLegacyVertexDeltaEditorModel() override;

		// We need to implement this static MakeInstance method.
		static FMLDeformerEditorModel* MakeInstance();

		// FGCObject overrides.
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("FLegacyVertexDeltaEditorModel"); }
		// ~END FGCObject overrides.
	
		// FMLDeformerEditorModel overrides.
		virtual void Init(const InitSettings& Settings) override;
		virtual FMLDeformerEditorActor* CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const override;
		virtual FMLDeformerSampler* CreateSampler() const;
		virtual void CreateTrainingGroundTruthActor(UWorld* World) override;
		virtual void CreateTestGroundTruthActor(UWorld* World) override;
		virtual void OnPreTraining() override;
		virtual void OnTrainingAborted() override;
		virtual void OnInputAssetsChanged() override;
		virtual int32 GetNumFrames() const override;
		virtual double GetTimeAtFrame(int32 FrameNumber) const override;
		virtual int32 GetFrameAtTime(double TimeInSeconds) const override;
		virtual void UpdateIsReadyForTrainingState() override;
		virtual ETrainingResult Train() override;
		virtual FString GetDefaultDeformerGraphAssetPath() const override;
		virtual FString GetHeatMapDeformerGraphPath() const override;
		FString GetTrainedNetworkOnnxFile() const override;
		// ~END FMLDeformerEditorModel overrides.

		// Some helpers that cast to this model's variants of some classes.
		ULegacyVertexDeltaModel* GetVertexDeltaModel() const { return Cast<ULegacyVertexDeltaModel>(Model); }
		ULegacyVertexDeltaModelVizSettings* GetVertexDeltaModelVizSettings() const;
		FLegacyVertexDeltaEditorModelActor* FindVertexDeltaModelEditorActor(int32 TypeID) const;
		FMLDeformerGeomCacheSampler* GetGeomCacheSampler() const { return static_cast<FMLDeformerGeomCacheSampler*>(Sampler); }
	
		void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);

	protected:
		void CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor);

	protected:
		FVector VertexDeltaMeanBackup = FVector::ZeroVector;
		FVector VertexDeltaScaleBackup = FVector::OneVector;
	};
}	// namespace UE::LegacyVertexDeltaModel
