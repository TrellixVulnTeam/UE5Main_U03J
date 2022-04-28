// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

UDataLayerInstanceWithAsset::UDataLayerInstanceWithAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

#if WITH_EDITOR
FName UDataLayerInstanceWithAsset::MakeName(const UDataLayerAsset* DeprecatedDataLayer)
{
	return FName(FString::Format(TEXT("DataLayer_{0}"), { FGuid::NewGuid().ToString() }));
}

void UDataLayerInstanceWithAsset::OnCreated(const UDataLayerAsset* Asset)
{
	check(!GetOuterAWorldDataLayers()->HasDeprecatedDataLayers() || IsRunningCommandlet());

	Modify(/*bAlwaysMarkDirty*/false);

	check(DataLayerAsset == nullptr);
	DataLayerAsset = Asset;

	SetVisible(true);
}

bool UDataLayerInstanceWithAsset::AddActor(AActor* Actor) const
{
	return Actor->AddDataLayer(DataLayerAsset);
}

bool UDataLayerInstanceWithAsset::RemoveActor(AActor* Actor) const
{
	return Actor->RemoveDataLayer(DataLayerAsset);
}

bool UDataLayerInstanceWithAsset::ContainsActor(const AActor* Actor) const
{
	return Actor->ContainsDataLayer(DataLayerAsset);
}

bool UDataLayerInstanceWithAsset::Validate(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	bool bIsValid = true;

	if (GetAsset() == nullptr)
	{
		ErrorHandler->OnInvalidReferenceDataLayerAsset(this);
		return false;
	}

	GetOuterAWorldDataLayers()->ForEachDataLayer([&bIsValid, this, ErrorHandler](UDataLayerInstance* DataLayerInstance)
	{
		if (DataLayerInstance != this)
		{
			if (UDataLayerInstanceWithAsset* DataLayerInstanceWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayerInstance))
			{
				if (DataLayerInstanceWithAsset->GetAsset() == GetAsset())
				{
					ErrorHandler->OnDataLayerAssetConflict(this, DataLayerInstanceWithAsset);
					bIsValid = false;
					return false;
				}
			}
		}

		return true;
	});

	bIsValid &= Super::Validate(ErrorHandler);

	return bIsValid;
}
#endif