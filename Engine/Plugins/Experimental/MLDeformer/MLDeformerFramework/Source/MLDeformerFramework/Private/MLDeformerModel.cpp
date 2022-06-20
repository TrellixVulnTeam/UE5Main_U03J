// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModel.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerVizSettings.h"
#include "MLDeformerAsset.h"
#include "MLDeformerComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"
#include "NeuralNetwork.h"
#include "UObject/UObjectGlobals.h"

namespace UE::MLDeformer
{
	void FVertexMapBuffer::InitRHI()
	{
		if (VertexMap.Num() > 0)
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FVertexMapBuffer"));

			VertexBufferRHI = RHICreateVertexBuffer(VertexMap.Num() * sizeof(uint32), BUF_Static | BUF_ShaderResource, CreateInfo);
			uint32* Data = reinterpret_cast<uint32*>(RHILockBuffer(VertexBufferRHI, 0, VertexMap.Num() * sizeof(uint32), RLM_WriteOnly));
			for (int32 Index = 0; Index < VertexMap.Num(); ++Index)
			{
				Data[Index] = static_cast<uint32>(VertexMap[Index]);
			}
			RHIUnlockBuffer(VertexBufferRHI);
			VertexMap.Empty();

			ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, 4, PF_R32_UINT);
		}
		else
		{
			VertexBufferRHI = nullptr;
			ShaderResourceViewRHI = nullptr;
		}
	}
}	// namespace UE::MLDeformer

UMLDeformerInputInfo* UMLDeformerModel::CreateInputInfo()
{ 
	return NewObject<UMLDeformerInputInfo>(this);
}

UMLDeformerModelInstance* UMLDeformerModel::CreateModelInstance(UMLDeformerComponent* Component)
{ 
	return NewObject<UMLDeformerModelInstance>(Component);
}

void UMLDeformerModel::Init(UMLDeformerAsset* InDeformerAsset) 
{ 
	check(InDeformerAsset); 
	DeformerAsset = InDeformerAsset; 
	if (InputInfo == nullptr)
	{
		InputInfo = CreateInputInfo();
	}
}

void UMLDeformerModel::Serialize(FArchive& Archive)
{
	#if WITH_EDITOR
		if (Archive.IsSaving() && Archive.IsPersistent())
		{
			InitVertexMap();
			UpdateCachedNumVertices();
		}
	#endif

	Super::Serialize(Archive);
}

UMLDeformerAsset* UMLDeformerModel::GetDeformerAsset() const
{ 
	return DeformerAsset.Get(); 
}

void UMLDeformerModel::PostLoad()
{
	Super::PostLoad();

	InitGPUData();

	#if WITH_EDITOR
		UpdateCachedNumVertices();
	#endif

	UMLDeformerAsset* MLDeformerAsset = Cast<UMLDeformerAsset>(GetOuter());
	Init(MLDeformerAsset);

	if (InputInfo)
	{
		InputInfo->OnPostLoad();
	}

	if (NeuralNetwork)
	{
		NeuralNetwork->SetDeviceType(ENeuralDeviceType::GPU, ENeuralDeviceType::CPU, ENeuralDeviceType::GPU);
		if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU || 
			NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
		{
			UE_LOG(LogMLDeformer, Error, TEXT("Neural net in ML Deformer '%s' cannot run on the GPU, it will not be active."), *GetName());
		}
	}
}

void UMLDeformerModel::SetNeuralNetwork(UNeuralNetwork* InNeuralNetwork)
{
	NeuralNetworkModifyDelegate.Broadcast();
	NeuralNetwork = InNeuralNetwork;
}

// Used for the FBoenReference, so it knows what skeleton to pick bones from.
USkeleton* UMLDeformerModel::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	#if WITH_EDITOR
		bInvalidSkeletonIsError = false;
		if (SkeletalMesh)
		{
			return SkeletalMesh->GetSkeleton();
		}
	#endif
	return nullptr;
}

void UMLDeformerModel::BeginDestroy()
{
	BeginReleaseResource(&VertexMapBuffer);
	RenderResourceDestroyFence.BeginFence();
	Super::BeginDestroy();
}

bool UMLDeformerModel::IsReadyForFinishDestroy()
{
	return Super::IsReadyForFinishDestroy() && RenderResourceDestroyFence.IsFenceComplete();
}

void UMLDeformerModel::InitGPUData()
{
	BeginReleaseResource(&VertexMapBuffer);
	VertexMapBuffer.Init(VertexMap);
	BeginInitResource(&VertexMapBuffer);
}

#if WITH_EDITOR
	void UMLDeformerModel::UpdateNumBaseMeshVertices()
	{
		NumBaseMeshVerts = UMLDeformerModel::ExtractNumImportedSkinnedVertices(SkeletalMesh);
	}

	void UMLDeformerModel::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		OnPostEditChangeProperty().ExecuteIfBound(PropertyChangedEvent);
	}

	void UMLDeformerModel::UpdateCachedNumVertices()
	{
		UpdateNumBaseMeshVertices();
		UpdateNumTargetMeshVertices();
	}

	int32 UMLDeformerModel::ExtractNumImportedSkinnedVertices(const USkeletalMesh* SkeletalMesh)
	{
		return SkeletalMesh ? SkeletalMesh->GetNumImportedVertices() : 0;
	}
#endif	// #if WITH_EDITOR

#if WITH_EDITORONLY_DATA
	void UMLDeformerModel::InitVertexMap()
	{
		VertexMap.Empty();
		if (SkeletalMesh)
		{
			FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();
			if (SkeletalMeshModel)
			{
				VertexMap = SkeletalMeshModel->LODModels[0].MeshToImportVertexMap;
			}
		}
	}
#endif	// WITH_EDITORDATA_ONLY
