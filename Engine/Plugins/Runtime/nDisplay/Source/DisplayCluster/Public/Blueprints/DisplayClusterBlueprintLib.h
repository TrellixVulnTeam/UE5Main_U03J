// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IDisplayClusterBlueprintAPI.h"
#include "DisplayClusterBlueprintLib.generated.h"

class ADisplayClusterLightCardActor;
class ADisplayClusterRootActor;

/**
 * Blueprint API function library
 */
UCLASS()
class UDisplayClusterBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "DisplayCluster Module API"), Category = "NDisplay")
	static void GetAPI(TScriptInterface<IDisplayClusterBlueprintAPI>& OutAPI);

	/** Create a new light card parented to the given nDisplay root actor. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "DisplayCluster Module API"), Category = "NDisplay")
	static ADisplayClusterLightCardActor* CreateLightCard(ADisplayClusterRootActor* RootActor);
};
