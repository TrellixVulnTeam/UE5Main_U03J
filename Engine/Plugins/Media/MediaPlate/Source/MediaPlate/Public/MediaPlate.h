// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MediaPlate.generated.h"

class UMediaPlateComponent;

/**
 * MediaPlate is an actor that can play and show media in the world.
 */
UCLASS()
class MEDIAPLATE_API AMediaPlate : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
	virtual void BeginDestroy() override;
	//~ End AActor Interface

	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UMediaPlateComponent> MediaPlateComponent;

	/** Holds the mesh. */
	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

private:
	/** Name for our media plate component. */
	static FLazyName MediaPlateComponentName;
	/** Name for the media texture parameter in the material. */
	static FLazyName MediaTextureName;
};
