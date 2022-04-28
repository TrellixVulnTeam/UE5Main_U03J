// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Engine/AssetUserData.h"
#include "LevelSequenceAnimSequenceLink.generated.h"

class UAnimSequence;

/** Link To Anim Sequence that we are linked too.*/
USTRUCT(BlueprintType)
struct LEVELSEQUENCE_API FLevelSequenceAnimSequenceLinkItem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = Property)
	FGuid SkelTrackGuid;

	UPROPERTY(BlueprintReadWrite, Category = Property)
	FSoftObjectPath PathToAnimSequence;

	//From Editor Only UAnimSeqExportOption we cache this since we can re-import dynamically
	UPROPERTY(BlueprintReadWrite, Category = Property)
	bool bExportTransforms = true;
	UPROPERTY(BlueprintReadWrite, Category = Property)
	bool bExportMorphTargets = true;
	UPROPERTY(BlueprintReadWrite, Category = Property)
	bool bExportAttributeCurves = true;
	UPROPERTY(BlueprintReadWrite, Category = Property)
	bool bExportMaterialCurves = true;
	UPROPERTY(BlueprintReadWrite, Category = Property)
	bool bRecordInWorldSpace = false;

	void SetAnimSequence(UAnimSequence* InAnimSequence);
	UAnimSequence* ResolveAnimSequence();

};

/** Link To Set of Anim Sequences that we may be linked to.*/
UCLASS(BlueprintType)
class LEVELSEQUENCE_API ULevelSequenceAnimSequenceLink : public UAssetUserData
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = Links)
	TArray< FLevelSequenceAnimSequenceLinkItem> AnimSequenceLinks;
};