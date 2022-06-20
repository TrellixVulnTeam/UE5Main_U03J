// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceSkinnedMesh.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "OptimusDataDomain.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"


FString UOptimusSkinnedMeshDataInterface::GetDisplayName() const
{
	return TEXT("Skinned Mesh");
}

TArray<FOptimusCDIPinDefinition> UOptimusSkinnedMeshDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"NumVertices", "ReadNumVertices"});
	Defs.Add({"Position", "ReadPosition", Optimus::DomainName::Vertex, "ReadNumVertices"});
	Defs.Add({"TangentX", "ReadTangentX", Optimus::DomainName::Vertex, "ReadNumVertices"});
	Defs.Add({"TangentZ", "ReadTangentZ", Optimus::DomainName::Vertex, "ReadNumVertices"});
	Defs.Add({"NumUVChannels", "ReadNumUVChannels"});
	Defs.Add({"UV", "ReadUV", {{Optimus::DomainName::Vertex, "ReadNumVertices"}, {Optimus::DomainName::UVChannel, "ReadNumUVChannels"}}});
	Defs.Add({"Color", "ReadColor", Optimus::DomainName::Vertex, "ReadColor" });
	Defs.Add({"NumTriangles", "ReadNumTriangles" });
	Defs.Add({"IndexBuffer", "ReadIndexBuffer", Optimus::DomainName::Triangle, "ReadNumTriangles"});
	return Defs;
}

void UOptimusSkinnedMeshDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumTriangles"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumUVChannels"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadIndexBuffer"))
		.AddReturnType(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadPosition"))
		.AddReturnType(EShaderFundamentalType::Float, 3)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadTangentX"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadTangentZ"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadUV"))
		.AddReturnType(EShaderFundamentalType::Float, 2)
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadColor"))
		.AddReturnType(EShaderFundamentalType::Float, 4)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDuplicatedIndicesStart"))
		.AddReturnType(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDuplicatedIndicesLength"))
		.AddReturnType(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDuplicatedIndex"))
		.AddReturnType(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FSkinnedMeshDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, NumTriangles)
	SHADER_PARAMETER(uint32, NumUVChannels)
	SHADER_PARAMETER(uint32, IndexBufferStart)
	SHADER_PARAMETER(uint32, InputStreamStart)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndexBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, PositionInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<SNORM float4>, TangentInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<float2>, UVInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, ColorInputBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, DuplicatedIndicesIndices)
	SHADER_PARAMETER_SRV(Buffer<uint>, DuplicatedIndices)
END_SHADER_PARAMETER_STRUCT()

void UOptimusSkinnedMeshDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FSkinnedMeshDataInterfaceParameters>(UID);
}

void UOptimusSkinnedMeshDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TEXT("/Plugin/Optimus/Private/DataInterfaceSkinnedMesh.ush"), EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusSkinnedMeshDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceSkinnedMesh.ush\"\n");
}

void UOptimusSkinnedMeshDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USkinnedMeshComponent::StaticClass());
}

UComputeDataProvider* UOptimusSkinnedMeshDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusSkinnedMeshDataProvider* Provider = NewObject<UOptimusSkinnedMeshDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InSourceObjects[0]);
	}

	return Provider;
}


bool UOptimusSkinnedMeshDataProvider::IsValid() const
{
	return
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* UOptimusSkinnedMeshDataProvider::GetRenderProxy()
{
	return new FOptimusSkinnedMeshDataProviderProxy(SkinnedMesh);
}


FOptimusSkinnedMeshDataProviderProxy::FOptimusSkinnedMeshDataProviderProxy(USkinnedMeshComponent* SkinnedMeshComponent)
{
	SkeletalMeshObject = SkinnedMeshComponent->MeshObject;
}

void FOptimusSkinnedMeshDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FSkinnedMeshDataInterfaceParameters)))
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

	FRHIShaderResourceView* NullSRVBinding = GWhiteVertexBufferWithSRV->ShaderResourceViewRHI.GetReference();

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		FRHIShaderResourceView* IndexBufferSRV = LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
		FRHIShaderResourceView* MeshVertexBufferSRV = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();
		FRHIShaderResourceView* MeshTangentBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
		FRHIShaderResourceView* MeshUVBufferSRV = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
		FRHIShaderResourceView* MeshColorBufferSRV = LodRenderData->StaticVertexBuffers.ColorVertexBuffer.GetColorComponentsSRV();

		FRHIShaderResourceView* DuplicatedIndicesIndicesSRV = RenderSection.DuplicatedVerticesBuffer.LengthAndIndexDuplicatedVerticesIndexBuffer.VertexBufferSRV;
		FRHIShaderResourceView* DuplicatedIndicesSRV = RenderSection.DuplicatedVerticesBuffer.DuplicatedVerticesIndexBuffer.VertexBufferSRV;
		const bool bValidDuplicatedIndices = (DuplicatedIndicesIndicesSRV != nullptr) && (DuplicatedIndicesSRV != nullptr);

		FSkinnedMeshDataInterfaceParameters* Parameters = (FSkinnedMeshDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.NumVertices;
		Parameters->NumTriangles = RenderSection.NumTriangles;
		Parameters->NumUVChannels = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		Parameters->IndexBufferStart = RenderSection.BaseIndex;
		Parameters->InputStreamStart = RenderSection.BaseVertexIndex;
		Parameters->IndexBuffer = IndexBufferSRV != nullptr ? IndexBufferSRV : NullSRVBinding;
		Parameters->PositionInputBuffer = MeshVertexBufferSRV != nullptr ? MeshVertexBufferSRV : NullSRVBinding;
		Parameters->TangentInputBuffer = MeshTangentBufferSRV != nullptr ? MeshTangentBufferSRV : NullSRVBinding;
		Parameters->UVInputBuffer = MeshUVBufferSRV != nullptr ? MeshUVBufferSRV : NullSRVBinding;
		Parameters->ColorInputBuffer = MeshColorBufferSRV != nullptr ? MeshColorBufferSRV : NullSRVBinding;
		Parameters->DuplicatedIndicesIndices = DuplicatedIndicesIndicesSRV != nullptr ? DuplicatedIndicesIndicesSRV : NullSRVBinding;
		Parameters->DuplicatedIndices = DuplicatedIndicesSRV != nullptr ? DuplicatedIndicesSRV : NullSRVBinding;
	}
}
