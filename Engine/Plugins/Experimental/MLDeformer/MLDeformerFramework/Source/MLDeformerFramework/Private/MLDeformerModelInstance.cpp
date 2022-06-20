// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModelInstance.h"
#include "MLDeformerModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "NeuralNetwork.h"

void UMLDeformerModelInstance::BeginDestroy()
{
	Release();
	Super::BeginDestroy();
}

void UMLDeformerModelInstance::Release()
{
	UNeuralNetwork* NeuralNetwork = Model.Get() ? Model->GetNeuralNetwork() : nullptr;
	if (NeuralNetwork != nullptr && NeuralNetworkInferenceHandle != -1)
	{
		NeuralNetwork->DestroyInferenceContext(NeuralNetworkInferenceHandle);
		NeuralNetworkInferenceHandle = -1;
	}
}

void UMLDeformerModelInstance::Init(USkeletalMeshComponent* SkelMeshComponent)
{
	SkeletalMeshComponent = SkelMeshComponent;

	if (SkelMeshComponent == nullptr)
	{
		AssetBonesToSkelMeshMappings.Empty();
		return;
	}

	USkeletalMesh* SkelMesh = SkelMeshComponent->SkeletalMesh;
	if (SkelMesh)
	{
		// Init the bone mapping table.
		const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
		const int32 NumAssetBones = InputInfo->GetNumBones();
		AssetBonesToSkelMeshMappings.Reset();
		AssetBonesToSkelMeshMappings.AddUninitialized(NumAssetBones);
		TrainingBoneTransforms.SetNumUninitialized(NumAssetBones);

		// For each bone in the deformer asset, find the matching bone index inside the skeletal mesh component.
		for (int32 Index = 0; Index < NumAssetBones; ++Index)
		{
			const FName BoneName = InputInfo->GetBoneName(Index);
			const int32 SkelMeshBoneIndex = SkeletalMeshComponent->GetBaseComponent()->GetBoneIndex(BoneName);
			AssetBonesToSkelMeshMappings[Index] = SkelMeshBoneIndex;
		}
	}

	// Perform a compatibility check.
	UpdateCompatibilityStatus();
}

void UMLDeformerModelInstance::UpdateCompatibilityStatus()
{
	bIsCompatible = SkeletalMeshComponent->SkeletalMesh && CheckCompatibility(SkeletalMeshComponent, true).IsEmpty();
}

FString UMLDeformerModelInstance::CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues)
{
	ErrorText = FString();

	// If we're not compatible, generate a compatibility string.
	USkeletalMesh* SkelMesh = InSkelMeshComponent ? InSkelMeshComponent->SkeletalMesh.Get() : nullptr;
	UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
	if (SkelMesh && !InputInfo->IsCompatible(SkelMesh) && Model->GetDeformerAsset())
	{
		ErrorText += InputInfo->GenerateCompatibilityErrorString(SkelMesh);
		ErrorText += "\n";
		check(!ErrorText.IsEmpty());
		if (LogIssues)
		{
			UE_LOG(LogMLDeformer, Error, TEXT("ML Deformer '%s' isn't compatible with Skeletal Mesh '%s'.\nReason(s):\n%s"), 
				*Model->GetDeformerAsset()->GetName(), 
				*SkelMesh->GetName(), 
				*ErrorText);
		}
	}

	UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork != nullptr && NeuralNetwork->IsLoaded() && Model->GetDeformerAsset())
	{
		const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensor().Num();
		const int64 NumDeformerAssetInputs = static_cast<int64>(Model->GetInputInfo()->CalcNumNeuralNetInputs());
		if (NumNeuralNetInputs != NumDeformerAssetInputs)
		{
			const FString InputErrorString = "The number of network inputs doesn't match the asset. Please retrain the asset."; 
			ErrorText += InputErrorString + "\n";
			if (LogIssues)
			{
				UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *(Model->GetDeformerAsset()->GetName()), *InputErrorString);
			}
		}
	}

	return ErrorText;
}

void UMLDeformerModelInstance::UpdateBoneTransforms()
{
	const USkinnedMeshComponent* MasterPoseComponent = SkeletalMeshComponent->MasterPoseComponent.Get();
	if (MasterPoseComponent)
	{
		const TArray<FTransform>& MasterTransforms = MasterPoseComponent->GetComponentSpaceTransforms();
		USkeletalMesh* Mesh = MasterPoseComponent->SkeletalMesh;
		const int32 NumTrainingBones = AssetBonesToSkelMeshMappings.Num();
		for (int32 Index = 0; Index < NumTrainingBones; ++Index)
		{
			const int32 ComponentBoneIndex = AssetBonesToSkelMeshMappings[Index];
			const FTransform& ComponentSpaceTransform = MasterTransforms[ComponentBoneIndex];
			const int32 ParentIndex = Mesh->GetRefSkeleton().GetParentIndex(ComponentBoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				TrainingBoneTransforms[Index] = ComponentSpaceTransform.GetRelativeTransform(MasterTransforms[ParentIndex]);
			}
			else
			{
				TrainingBoneTransforms[Index] = ComponentSpaceTransform;
			}
			TrainingBoneTransforms[Index].NormalizeRotation();
		}
	}
	else
	{
		BoneTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();
		const int32 NumTrainingBones = AssetBonesToSkelMeshMappings.Num();
		for (int32 Index = 0; Index < NumTrainingBones; ++Index)
		{
			const int32 ComponentBoneIndex = AssetBonesToSkelMeshMappings[Index];
			TrainingBoneTransforms[Index] = BoneTransforms[ComponentBoneIndex];
		}
	}
}

int64 UMLDeformerModelInstance::SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	// Get the transforms for the bones we used during training.
	// These are in the space relative to their parent.
	UpdateBoneTransforms();

	// Write the transforms into the output buffer.
	const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
	const int32 AssetNumBones = InputInfo->GetNumBones();
	int64 Index = StartIndex;
	check((Index + AssetNumBones * 6) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer. (6 because of two columns of the 3x3 rotation matrix)
	for (int32 BoneIndex = 0; BoneIndex < AssetNumBones; ++BoneIndex)
	{
		const FMatrix RotationMatrix = TrainingBoneTransforms[BoneIndex].GetRotation().ToMatrix();
		const FVector X = RotationMatrix.GetColumn(0);
		const FVector Y = RotationMatrix.GetColumn(1);	
		OutputBuffer[Index++] = X.X;
		OutputBuffer[Index++] = X.Y;
		OutputBuffer[Index++] = X.Z;
		OutputBuffer[Index++] = Y.X;
		OutputBuffer[Index++] = Y.Y;
		OutputBuffer[Index++] = Y.Z;
	}

	return Index;
}

int64 UMLDeformerModelInstance::SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();

	// Write the weights into the output buffer.
	int64 Index = StartIndex;
	const int32 AssetNumCurves = InputInfo->GetNumCurves();
	check((Index + AssetNumCurves) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer.

	// Write the curve weights to the output buffer.
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (AnimInstance)
	{
		for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
		{
			const FName CurveName = InputInfo->GetCurveName(CurveIndex);
			const float CurveValue = AnimInstance->GetCurveValue(CurveName);	// Outputs 0.0 when not found.
			OutputBuffer[Index++] = CurveValue;
		}
	}
	else
	{
		for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
		{
			OutputBuffer[Index++] = 0.0f;
		}
	}

	return Index;
}

void UMLDeformerModelInstance::SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats)
{
	check(SkeletalMeshComponent);

	// Feed data to the network inputs.
	int64 BufferOffset = 0;
	BufferOffset = SetBoneTransforms(InputData, NumInputFloats, BufferOffset);
	BufferOffset = SetCurveValues(InputData, NumInputFloats, BufferOffset);
	check(BufferOffset == NumInputFloats);
}

bool UMLDeformerModelInstance::IsValidForDataProvider() const
{
	const UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork == nullptr || 
		!NeuralNetwork->IsLoaded() || 
		NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU || 
		NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
	{
		return false;
	}

	return
		Model->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr &&
		GetNeuralNetworkInferenceHandle() != -1;
}

void UMLDeformerModelInstance::Tick(float DeltaTime)
{
	// Some safety checks.
	if (Model == nullptr || 
		SkeletalMeshComponent == nullptr || 
		SkeletalMeshComponent->SkeletalMesh == nullptr || 
		!bIsCompatible)
	{
		return;
	}

	// Get the network and make sure it's loaded.
	UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
	{
		return;
	}

	// If we're not on the GPU we can't continue really.
	// This is needed as the deformer graph system needs it on the GPU.
	// Some platforms might not support GPU yet.
	// Only the inputs are on the CPU.
	check(NeuralNetwork->GetInputDeviceType() == ENeuralDeviceType::CPU);
	if (!bAllowCPU)
	{
		if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU ||
			NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
		{
			return;
		}
	}

	// Allocate an inference context if none has already been allocated.
	if (NeuralNetworkInferenceHandle == -1)
	{
		NeuralNetworkInferenceHandle = NeuralNetwork->CreateInferenceContext();
		if (NeuralNetworkInferenceHandle == -1)
		{
			return;
		}
	}

	// If the neural network expects a different number of inputs, do nothing.
	const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensorForContext(NeuralNetworkInferenceHandle).Num();
	const int64 NumDeformerAssetInputs = Model->GetInputInfo()->CalcNumNeuralNetInputs();
	if (NumNeuralNetInputs != NumDeformerAssetInputs)
	{
		return;
	}

	// Update and write the input values directly into the input tensor.
	float* InputDataPointer = static_cast<float*>(NeuralNetwork->GetInputDataPointerMutableForContext(NeuralNetworkInferenceHandle));
	SetNeuralNetworkInputValues(InputDataPointer, NumNeuralNetInputs);

	// Run the neural network.
	if (NeuralNetwork->GetOutputDeviceType() == ENeuralDeviceType::GPU)
	{
		ENQUEUE_RENDER_COMMAND(RunNeuralNetwork)(
			[NeuralNetwork, Handle = NeuralNetworkInferenceHandle](FRHICommandListImmediate& RHICmdList)
		{
				// Output deltas will be available on GPU for DeformerGraph via UMLDeformerDataProvider.
				FRDGBuilder GraphBuilder(RHICmdList);
				NeuralNetwork->Run(GraphBuilder, Handle);
				GraphBuilder.Execute();
		});
	}
	else
	{
		NeuralNetwork->Run();
	}
}
