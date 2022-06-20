// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

class AActor;
class APCGWorldActor;
class ALandscape;
class ALandscapeProxy;
class UWorld;

namespace PCGHelpers
{
	/** Tag that will be added on every component generated through the PCG system */
	const FName DefaultPCGTag = TEXT("PCG Generated Component");
	const FName DefaultPCGDebugTag = TEXT("PCG Generated Debug Component");
	const FName DefaultPCGActorTag = TEXT("PCG Generated Actor");

	int ComputeSeed(int A);
	int ComputeSeed(int A, int B);
	int ComputeSeed(int A, int B, int C);

	bool IsInsideBounds(const FBox& InBox, const FVector& InPosition);
	bool IsInsideBoundsXY(const FBox& InBox, const FVector& InPosition);

	FBox GetActorBounds(AActor* InActor);
	FBox GetLandscapeBounds(ALandscapeProxy* InLandscape);

	ALandscape* GetLandscape(UWorld* InWorld, const FBox& InActorBounds);

#if WITH_EDITOR
	APCGWorldActor* GetPCGWorldActor(UWorld* InWorld);

	void GatherDependencies(UObject* Object, TSet<TObjectPtr<UObject>>& OutDependencies);
	void GatherDependencies(FProperty* Property, const void* InContainer, TSet<TObjectPtr<UObject>>& OutDependencies);
#endif
};