// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxHelper.h"
#include "FbxInclude.h"

/** Forward declarations */
struct FMeshDescription;
class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;

namespace UE::Interchange::Private
{
	class FAnimationPayloadContextTransform : public FPayloadContextBase
	{
	public:
		virtual ~FAnimationPayloadContextTransform() {}
		virtual FString GetPayloadType() const override { return TEXT("JointAnimation-PayloadContext"); }
		virtual bool FetchPayloadToFile(FFbxParser& Parser, const FString& PayloadFilepath) override;
		FbxNode* Node = nullptr;
		FbxScene* SDKScene = nullptr;
	};

	class FFbxAnimation
	{
	public:
		static void AddJointAnimation(FbxScene* SDKScene, FbxNode* JointNode, UInterchangeBaseNodeContainer& NodeContainer, UInterchangeSceneNode* UnrealNode, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts);
	};
}//ns UE::Interchange::Private
