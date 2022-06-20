// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGData.h"
#include "PCGNode.h"
#include "PCGSubsystem.h"

#include "PCGContext.generated.h"

class UPCGComponent;
struct FPCGGraphCache;

USTRUCT(BlueprintType)
struct PCG_API FPCGContext
{
	GENERATED_BODY()

	virtual ~FPCGContext() = default;

	FPCGDataCollection InputData;
	FPCGDataCollection OutputData;
	UPCGComponent* SourceComponent = nullptr;
	FPCGGraphCache* Cache = nullptr;
	int32 NumAvailableTasks = 0;

	// TODO: add RNG source
	// TODO: replace this by a better identification mechanism
	const UPCGNode* Node = nullptr;
	FPCGTaskId TaskId = InvalidTaskId;
	bool bIsPaused = false;

#if WITH_EDITOR
	double ElapsedTime = 0.0;
	int32 ExecutionCount = 0;
#endif

	template<typename SettingsType>
	const SettingsType* GetInputSettings() const
	{
		if (Node && Node->DefaultSettings)
		{
			return Cast<SettingsType>(InputData.GetSettings(Node->DefaultSettings));
		}
		else
		{
			return InputData.GetSettings<SettingsType>();
		}
	}

	FString GetTaskName() const;
	FString GetComponentName() const;
};
