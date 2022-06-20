// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceMorphTarget.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

FString UOptimusMorphTargetDataInterface::GetDisplayName() const
{
	return TEXT("Morph Target");
}

TArray<FOptimusCDIPinDefinition> UOptimusMorphTargetDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "DeltaPosition", "ReadDeltaPosition", Optimus::DomainName::Vertex, "ReadNumVertices" });
	Defs.Add({ "DeltaNormal", "ReadDeltaNormal", Optimus::DomainName::Vertex, "ReadNumVertices" });
	return Defs;
}

void UOptimusMorphTargetDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDeltaPosition"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDeltaNormal"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FMorphTargetDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER_SRV(Buffer<float>, MorphBuffer)
END_SHADER_PARAMETER_STRUCT()

void UOptimusMorphTargetDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FMorphTargetDataInterfaceParameters>(UID);
}

void UOptimusMorphTargetDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	OutPermutationVector.AddPermutation(TEXT("ENABLE_DEFORMER_MORPHTARGET"), 2);
}

void UOptimusMorphTargetDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TEXT("/Plugin/Optimus/Private/DataInterfaceMorphTarget.ush"), EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusMorphTargetDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceMorphTarget.ush\"\n");
}

void UOptimusMorphTargetDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkeletalMeshComponent::StaticClass());
}

UComputeDataProvider* UOptimusMorphTargetDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusMorphTargetDataProvider* Provider = NewObject<UOptimusMorphTargetDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkeletalMesh = Cast<USkeletalMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool UOptimusMorphTargetDataProvider::IsValid() const
{
	return
		SkeletalMesh != nullptr &&
		SkeletalMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* UOptimusMorphTargetDataProvider::GetRenderProxy()
{
	return new FOptimusMorphTargetDataProviderProxy(SkeletalMesh);
}


FOptimusMorphTargetDataProviderProxy::FOptimusMorphTargetDataProviderProxy(USkeletalMeshComponent* SkeletalMeshComponent)
{
	SkeletalMeshObject = SkeletalMeshComponent->MeshObject;
	FrameNumber = SkeletalMeshComponent->GetScene()->GetFrameNumber() + 1; // +1 matches the logic for FrameNumberToPrepare in FSkeletalMeshObjectGPUSkin::Update()
}

struct FMorphTargetDataInterfacePermutationIds
{
	uint32 EnableDeformerMorphTarget = 0;

	FMorphTargetDataInterfacePermutationIds(FComputeKernelPermutationVector const& PermutationVector)
	{
		{
			static FString Name(TEXT("ENABLE_DEFORMER_MORPHTARGET"));
			static uint32 Hash = GetTypeHash(Name);
			EnableDeformerMorphTarget = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
	}
};

void FOptimusMorphTargetDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FMorphTargetDataInterfaceParameters)))
	{
		return;
	}

	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = &SkeletalMeshRenderData.LODRenderData[LodIndex];
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FMorphTargetDataInterfacePermutationIds PermutationIds(InDispatchSetup.PermutationVector);

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		const bool bPreviousFrame = false;
		FRHIShaderResourceView* MorphBufferSRV = FSkeletalMeshDeformerHelpers::GetMorphTargetBufferForReading(SkeletalMeshObject, LodIndex, InvocationIndex, FrameNumber, bPreviousFrame);
		const bool bValidMorph = MorphBufferSRV != nullptr;

		FMorphTargetDataInterfaceParameters* Parameters = (FMorphTargetDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;
		Parameters->InputStreamStart = RenderSection.BaseVertexIndex;
		Parameters->MorphBuffer = MorphBufferSRV != nullptr ? MorphBufferSRV : NullSRVBinding;

		InOutDispatchData.PermutationId[InvocationIndex] |= (bValidMorph ? PermutationIds.EnableDeformerMorphTarget : 0);
	}
}
