// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"
#include "Serialization/Archive.h"
#include "Misc/Guid.h"

class AActor;

/**
 * Helper struct that allows serializing the ActorGuid for runtime use.
 */
struct FLevelInstanceActorGuid
{
	// Exists only to support 'FVTableHelper' Actor constructors
	FLevelInstanceActorGuid() : FLevelInstanceActorGuid(nullptr) {}

	FLevelInstanceActorGuid(AActor* InActor) : Actor(InActor) {}

#if !WITH_EDITOR
	void AssignIfInvalid();
#endif

	const FGuid& GetGuid() const;

	TObjectPtr<AActor> Actor = nullptr;
	FGuid ActorGuid;

	friend FArchive& operator<<(FArchive& Ar, FLevelInstanceActorGuid& LevelInstanceActorGuid);
};