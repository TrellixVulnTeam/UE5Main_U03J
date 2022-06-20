// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Mirror.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "DynamicPlayRate/DynamicPlayRateLibrary.h"
#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchLibrary.h"

#include "AnimNode_MotionMatching.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_MotionMatching : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPoseLink Source;

	// Collection of animations for motion matching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	TObjectPtr<const UPoseSearchDatabase> Database = nullptr;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	bool bUseDatabaseTagQuery = false;

	// Query used to filter database groups which can be searched
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinShownByDefault))
	FGameplayTagQuery DatabaseTagQuery;

	// Motion trajectory samples for pose search queries
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	FTrajectorySampleRange Trajectory;

	// Settings for dynamic play rate adjustment on sequences chosen by motion matching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	FDynamicPlayRateSettings DynamicPlayRateSettings;

	// Settings for the core motion matching algorithm evaluation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	FMotionMatchingSettings Settings;

	// Reset the motion matching state if we have become relevant to the graph
	// after not being ticked on the previous frame(s)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	bool bResetOnBecomingRelevant = true;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDraw = false;

	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDrawQuery = true;

	UPROPERTY(EditAnywhere, Category=Debug, meta = (PinShownByDefault))
	bool bDebugDrawMatch = true;
#endif

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:

	// Embedded sequence player node for playing animations from the motion matching database
	FAnimNode_SequencePlayer_Standalone SequencePlayerNode;

	// Embedded blendspace player node for playing blendspaces from the motion matching database
	FAnimNode_BlendSpacePlayer_Standalone BlendSpacePlayerNode;

	// Embedded mirror node to handle mirroring if the pose search results in a mirrored sequence
	FAnimNode_Mirror_Standalone MirrorNode;

	// Encapsulated motion matching algorithm and internal state
	FMotionMatchingState MotionMatchingState;

	// Current Asset Player Node
	FAnimNode_AssetPlayerBase* CurrentAssetPlayerNode = &SequencePlayerNode;

	// Update Counter for detecting being relevant
	FGraphTraversalCounter UpdateCounter;

	// FAnimNode_AssetPlayerBase
protected:
	// FAnimNode_AssetPlayerBase interface
	virtual float GetAccumulatedTime() const;
	virtual UAnimationAsset* GetAnimAsset() const override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual float GetCurrentAssetLength() const override;
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() const override;
	virtual bool GetIgnoreForRelevancyTest() const override;
	virtual bool SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest) override;
	// End of FAnimNode_AssetPlayerBase interface

private:

#if WITH_EDITORONLY_DATA
	// If true, "Relevant anim" nodes that look for the highest weighted animation in a state will ignore this node
	UPROPERTY(EditAnywhere, Category=Relevancy, meta=(FoldProperty, PinHiddenByDefault))
	bool bIgnoreForRelevancyTest = false;

	// Whether this node was evaluated last frame
	bool bWasEvaluated = false;
#endif // WITH_EDITORONLY_DATA
};