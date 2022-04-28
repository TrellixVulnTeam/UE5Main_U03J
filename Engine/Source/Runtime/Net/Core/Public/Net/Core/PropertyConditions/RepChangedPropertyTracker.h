// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreNet.h"

/**
 * This class is used to store meta data about properties that is shared between connections,
 * including whether or not a given property is Conditional, Active, and any external data
 * that may be needed for Replays.
 *
 * TODO: This class (and arguably IRepChangedPropertyTracker) should be renamed to reflect
 *			what they actually do now.
 */
class NETCORE_API FRepChangedPropertyTracker : public IRepChangedPropertyTracker
{
public:
	FRepChangedPropertyTracker() = delete;
	FRepChangedPropertyTracker(FCustomPropertyConditionState&& InActiveState);

	UE_DEPRECATED(5.1, "Replay arguments no longer used.")
	FRepChangedPropertyTracker(const bool InbIsReplay, const bool InbIsClientReplayRecording);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual ~FRepChangedPropertyTracker() = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin IRepChangedPropertyTracker Interface.
	/**
		* Manually set whether or not Property should be marked inactive.
		* This will change the Active status for all connections.
		*
		* @see DOREPLIFETIME_ACTIVE_OVERRIDE
		*
		* @param OwningObject	The object that we're tracking.
		* @param RepIndex		Replication index for the Property.
		* @param bIsActive		The new Active state.
		*/
	virtual void SetCustomIsActiveOverride(UObject* OwningObject, const uint16 RepIndex, const bool bIsActive) override;

	/**
	 * Sets (or resets) the External Data.
	 * External Data is primarily used for Replays, and is used to track additional non-replicated
	 * data or state about an object.
	 *
	 * @param Src		Memory containing the external data.
	 * @param NumBits	Size of the memory, in bits.
	 */
	UE_DEPRECATED(5.0, "Please use UReplaySubsystem::SetExternalDataForObject instead.")
	virtual void SetExternalData(const uint8* Src, const int32 NumBits) override;

	virtual void CountBytes(FArchive& Ar) const override;
	//~ End IRepChangedPropertyTracker Interface

	UE_DEPRECATED(5.1, "No longer used, ActiveState must be constructed with the correct number of properties.")
	void InitActiveParents(int32 ParentCount) {}

	bool IsParentActive(int32 ParentIndex) const
	{
		return ActiveState.GetActiveState(ParentIndex);
	}

	int32 GetParentCount() const
	{
		return ActiveState.GetNumProperties();
	}

private:
	/** Activation data for top level Properties on the given Actor / Object. */
	FCustomPropertyConditionState ActiveState;

public:
	UE_DEPRECATED(5.0, "No longer used, see UReplaySubsystem::SetExternalDataForObject")
	TArray<uint8> ExternalData;

	UE_DEPRECATED(5.0, "No longer used, see UReplaySubsystem::SetExternalDataForObject")
	uint32 ExternalDataNumBits;
};
