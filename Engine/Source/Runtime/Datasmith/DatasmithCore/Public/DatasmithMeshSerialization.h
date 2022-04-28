// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithCore.h"
#include "MeshDescription.h"
#include "Misc/SecureHash.h"


struct FDatasmithMeshModels
{
	FString MeshName;
	bool bIsCollisionMesh;
	TArray<FMeshDescription> SourceModels;

	DATASMITHCORE_API friend void operator << (FArchive& Ar, FDatasmithMeshModels& Models);
};

struct DATASMITHCORE_API FDatasmithPackedMeshes
{
	TArray<FDatasmithMeshModels> MeshesToExport;

	FMD5Hash Serialize(FArchive& Ar, bool bSaveCompressed=true);
};

DATASMITHCORE_API TArray<FDatasmithMeshModels> GetDatasmithMeshFromMeshPath(const FString& MeshPath);
