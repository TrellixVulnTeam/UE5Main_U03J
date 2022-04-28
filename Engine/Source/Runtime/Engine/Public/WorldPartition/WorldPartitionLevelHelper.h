// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionLevelHelper
 *
 * Helper class to build Levels for World Partition
 *
 */

#pragma once

#if WITH_EDITOR

#include "Engine/World.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

class FWorldPartitionPackageHelper;
class UWorldPartition;
struct FActorContainerID;

class FWorldPartitionLevelHelper
{
public:
	struct FPackageReferencer
	{
		~FPackageReferencer() { RemoveReferences(); }

		void AddReference(UPackage* InPackage);
		void RemoveReferences();
	};

	static ULevel* CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, UPackage* DestPackage = nullptr);
	static void DuplicateActorFolderToRuntimeCell(ULevel* CellLevel, ULevel* SrcLevel, const FGuid& ActorFolderGuid);
	static void MoveExternalActorsToLevel(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages, ULevel* InLevel);
	static void RemapLevelSoftObjectPaths(ULevel* InLevel, UWorldPartition* InWorldPartition);
	
	static bool LoadActors(UWorld* InOwningWorld, ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FPackageReferencer& InPackageReferencer, TFunction<void(bool)> InCompletionCallback, bool bInLoadAsync, FLinkerInstancingContext InOutInstancingContext);
	
	static FString AddActorContainerIDToActorPath(const FActorContainerID& InContainerID, const FString& InActorPath);
	static FString AddActorContainerIDToSubPathString(const FActorContainerID& InContainerID, const FString& InSubPathString);
private:
	static UWorld::InitializationValues GetWorldInitializationValues();

	struct FPackageReference
	{
		TSet<FPackageReferencer*> Referencers;
		TWeakObjectPtr<UPackage> Package;
	};
	static TMap<FName, FPackageReference> PackageReferences;
};

#endif