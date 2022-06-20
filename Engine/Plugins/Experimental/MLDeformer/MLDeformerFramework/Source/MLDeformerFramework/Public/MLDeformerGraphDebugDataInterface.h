// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerGraphDataInterface.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "MLDeformerGraphDebugDataInterface.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FRHIShaderResourceView;
class FSkeletalMeshObject;
class UMLDeformerAsset;
class USkeletalMeshComponent;

#define MLDEFORMER_DEBUG_SHADER_PARAMETERS() \
	SHADER_PARAMETER(uint32, NumVertices) \
	SHADER_PARAMETER(uint32, InputStreamStart) \
	SHADER_PARAMETER(int32, HeatMapMode) \
	SHADER_PARAMETER(float, HeatMapMax) \
	SHADER_PARAMETER(float, GroundTruthLerp) \
	SHADER_PARAMETER(uint32, GroundTruthBufferSize) \
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float3>, PositionGroundTruthBuffer) \
	SHADER_PARAMETER_SRV(Buffer<uint>, VertexMapBuffer)

#define MLDEFORMER_GRAPH_DISPATCH_DEFAULT_DEBUG_PARAMETERS() \
	Parameters->NumVertices = 0; \
	Parameters->InputStreamStart = RenderSection.BaseVertexIndex; \
	Parameters->HeatMapMode = HeatMapMode; \
	Parameters->HeatMapMax = HeatMapMax; \
	Parameters->GroundTruthLerp = GroundTruthLerp; \
	Parameters->GroundTruthBufferSize = GroundTruthPositions.Num(); \
	Parameters->PositionGroundTruthBuffer = GroundTruthBufferSRV; \
	Parameters->VertexMapBuffer = VertexMapBufferSRV;

#define MLDEFORMER_GRAPH_IMPLEMENT_DEBUG_BASICS(InterfaceClassName, DataProviderClassName, ParameterStructType, HLSLText, DisplayName) \
	FString InterfaceClassName::GetDisplayName() const \
	{ \
		return DisplayName; \
	} \
	void InterfaceClassName::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const \
	{ \
		InOutBuilder.AddNestedStruct<ParameterStructType>(UID); \
	} \
	void InterfaceClassName::GetHLSL(FString& OutHLSL) const \
	{ \
		OutHLSL += HLSLText; \
	} \
	UComputeDataProvider* InterfaceClassName::CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const \
	{ \
		DataProviderClassName* Provider = NewObject<DataProviderClassName>(); \
		if (InSourceObjects.Num() == 2) \
		{ \
			Provider->SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InSourceObjects[0]); \
			UMLDeformerComponent* DeformerComponent = Cast<UMLDeformerComponent>(InSourceObjects[1]); \
			Provider->DeformerAsset = (DeformerComponent != nullptr) ? DeformerComponent->GetDeformerAsset() : nullptr; \
		} \
		MLDEFORMER_EDITORDATA_ONLY( \
			if (Provider->DeformerAsset != nullptr) \
			{ \
				Provider->Init(); \
			} \
		,) \
		return Provider; \
	}


#define MLDEFORMER_GRAPH_IMPLEMENT_DEBUG_BASICS_WITH_PROXY(InterfaceClassName, DataProviderClassName, DataProviderProxyClassName, ParameterStructType, HLSLText, DisplayName) \
	MLDEFORMER_GRAPH_IMPLEMENT_DEBUG_BASICS(InterfaceClassName, DataProviderClassName, ParameterStructType, HLSLText, DisplayName) \
	FComputeDataProviderRenderProxy* DataProviderClassName::GetRenderProxy() \
	{ \
		MLDEFORMER_EDITORDATA_ONLY( \
			DataProviderProxyClassName* Proxy = new DataProviderProxyClassName(SkeletalMeshComponent, DeformerAsset, this); \
			const float SampleTime = SkeletalMeshComponent->GetPosition(); \
			UMLDeformerModel* Model = DeformerAsset->GetModel(); \
			Model->SampleGroundTruthPositions(SampleTime, Proxy->GetGroundTruthPositions()); \
			Proxy->HandleZeroGroundTruthPositions(); \
			return Proxy;, \
			return nullptr; \
		) \
	}

/** 
 * Compute Framework Data Interface for MLDeformer debugging data. 
 * This interfaces to editor only data, and so will only give valid results in that context.
 */
UCLASS(Category = ComputeFramework)
class MLDEFORMERFRAMEWORK_API UMLDeformerGraphDebugDataInterface
	: public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	// UOptimusComputeDataInterface overrides.
	virtual FString GetDisplayName() const override;
	virtual TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	// ~END UOptimusComputeDataInterface overrides.

	// UComputeDataInterface overrides.
	TCHAR const* GetClassName() const override { return TEXT("MLDeformerDebug"); }
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	virtual void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& OutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	virtual void GetHLSL(FString& OutHLSL) const override;
	virtual void GetSourceTypes(TArray<UClass*>& OutSourceTypes) const override;
	virtual UComputeDataProvider* CreateDataProvider(TArrayView<TObjectPtr<UObject>> InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const override;
	// ~END UComputeDataInterface overrides.
};

/** Compute Framework Data Provider for MLDeformer debugging data. */
UCLASS(BlueprintType, EditInlineNew, Category = ComputeFramework)
class MLDEFORMERFRAMEWORK_API UMLDeformerGraphDebugDataProvider
	: public UComputeDataProvider
{
	GENERATED_BODY()

public:
	virtual void Init() {};

	// UComputeDataProvider overrides.
	virtual bool IsValid() const override;
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() override;
	// ~END UComputeDataProvider overrides.

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;
};

/** Compute Framework Data Provider Proxy for MLDeformer debugging data. */
#if WITH_EDITORONLY_DATA
namespace UE::MLDeformer
{
	class MLDEFORMERFRAMEWORK_API FMLDeformerGraphDebugDataProviderProxy
		: public FComputeDataProviderRenderProxy
	{
	public:
		FMLDeformerGraphDebugDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent, UMLDeformerAsset* DeformerAsset, UMLDeformerGraphDebugDataProvider* InProvider);

		virtual void HandleZeroGroundTruthPositions();

		// FComputeDataProviderRenderProxy overrides.
		virtual void AllocateResources(FRDGBuilder& GraphBuilder) override;
		virtual void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
		// ~END FComputeDataProviderRenderProxy overrides.

		TArray<FVector3f>& GetGroundTruthPositions() { return GroundTruthPositions; }

	protected:
		TObjectPtr<UMLDeformerGraphDebugDataProvider> Provider = nullptr;
		FSkeletalMeshObject* SkeletalMeshObject;
		TArray<FVector3f> GroundTruthPositions;
		FRHIShaderResourceView* VertexMapBufferSRV = nullptr;
		FRDGBuffer* GroundTruthBuffer = nullptr;
		FRDGBufferSRV* GroundTruthBufferSRV = nullptr;
		int32 HeatMapMode = 0;
		float HeatMapMax = 0.0f;
		float GroundTruthLerp = 0.0f;
	};
}	// namespace UE::MLDeformer
#endif
