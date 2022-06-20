// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceScene.h"

#include "Components/SceneComponent.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "CoreGlobals.h"
#include "Engine/World.h"
#include "SceneInterface.h"
#include "ShaderCore.h"

FString UOptimusSceneDataInterface::GetDisplayName() const
{
	return TEXT("Scene Data");
}

TArray<FOptimusCDIPinDefinition> UOptimusSceneDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({"GameTime", "ReadGameTime"});
	Defs.Add({"GameTimeDelta", "ReadGameTimeDelta"});
	Defs.Add({"FrameNumber", "ReadFrameNumber"});
	return Defs;
}

void UOptimusSceneDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadGameTime"))
		.AddReturnType(EShaderFundamentalType::Float);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadGameTimeDelta"))
		.AddReturnType(EShaderFundamentalType::Float);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadFrameNumber"))
		.AddReturnType(EShaderFundamentalType::Uint);
}

BEGIN_SHADER_PARAMETER_STRUCT(FSceneDataInterfaceParameters, )
	SHADER_PARAMETER(float, GameTime)
	SHADER_PARAMETER(float, GameTimeDelta)
	SHADER_PARAMETER(uint32, FrameNumber)
END_SHADER_PARAMETER_STRUCT()

void UOptimusSceneDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FSceneDataInterfaceParameters>(UID);
}

void UOptimusSceneDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TEXT("/Plugin/Optimus/Private/DataInterfaceScene.ush"), EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UOptimusSceneDataInterface::GetHLSL(FString& OutHLSL) const
{
	OutHLSL += TEXT("#include \"/Plugin/Optimus/Private/DataInterfaceScene.ush\"\n");
}

void UOptimusSceneDataInterface::GetSourceTypes(TArray<UClass*>& OutSourceTypes) const
{
	OutSourceTypes.Add(USceneComponent::StaticClass());
}

UComputeDataProvider* UOptimusSceneDataInterface::CreateDataProvider(TArrayView< TObjectPtr<UObject> > InSourceObjects, uint64 InInputMask, uint64 InOutputMask) const
{
	UOptimusSceneDataProvider* Provider = NewObject<UOptimusSceneDataProvider>();

	if (InSourceObjects.Num() == 1)
	{
		Provider->SceneComponent = Cast<USceneComponent>(InSourceObjects[0]);
	}

	return Provider;
}


FComputeDataProviderRenderProxy* UOptimusSceneDataProvider::GetRenderProxy()
{
	return new FOptimusSceneDataProviderProxy(SceneComponent);
}


FOptimusSceneDataProviderProxy::FOptimusSceneDataProviderProxy(USceneComponent* SceneComponent)
{
	bool bUseSceneTime = SceneComponent != nullptr;
#if WITH_EDITOR
	// Don't tick time in Editor unless in PIE.
	if (GIsEditor && bUseSceneTime)
	{
		bUseSceneTime &= (bUseSceneTime && SceneComponent->GetWorld() != nullptr && SceneComponent->GetWorld()->WorldType != EWorldType::Editor);
	}
#endif
	GameTime = bUseSceneTime ? SceneComponent->GetWorld()->TimeSeconds : 0;
	GameTimeDelta = bUseSceneTime ? SceneComponent->GetWorld()->DeltaTimeSeconds : 0;
	FrameNumber = bUseSceneTime ? SceneComponent->GetScene()->GetFrameNumber() : 0;
}

void FOptimusSceneDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FSceneDataInterfaceParameters)))
	{
		return;
	}

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSceneDataInterfaceParameters* Parameters = (FSceneDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->GameTime = GameTime;
		Parameters->GameTimeDelta = GameTimeDelta;
		Parameters->FrameNumber = FrameNumber;
	}
}
