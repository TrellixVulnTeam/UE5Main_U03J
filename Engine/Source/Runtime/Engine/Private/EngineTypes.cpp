// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/EngineTypes.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"
#include "GameFramework/LightWeightInstanceManager.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/MeshMerging.h"
#include "Engine/CollisionProfile.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

FAttachmentTransformRules FAttachmentTransformRules::KeepRelativeTransform(EAttachmentRule::KeepRelative, false);
FAttachmentTransformRules FAttachmentTransformRules::KeepWorldTransform(EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetNotIncludingScale(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
FAttachmentTransformRules FAttachmentTransformRules::SnapToTargetIncludingScale(EAttachmentRule::SnapToTarget, false);

FDetachmentTransformRules FDetachmentTransformRules::KeepRelativeTransform(EDetachmentRule::KeepRelative, true);
FDetachmentTransformRules FDetachmentTransformRules::KeepWorldTransform(EDetachmentRule::KeepWorld, true);

#if WITH_EDITORONLY_DATA
void FMeshProxySettings::PostLoadDeprecated()
{
	MaterialSettings.MaterialMergeType = EMaterialMergeType::MaterialMergeType_Simplygon;
}

void FMeshMergingSettings::PostLoadDeprecated()
{
	FMeshMergingSettings DefaultObject;
	if (bImportVertexColors_DEPRECATED != DefaultObject.bImportVertexColors_DEPRECATED)
	{
		bBakeVertexDataToMesh = bImportVertexColors_DEPRECATED;
	}

	if (bExportNormalMap_DEPRECATED != DefaultObject.bExportNormalMap_DEPRECATED)
	{
		MaterialSettings.bNormalMap = bExportNormalMap_DEPRECATED;
	}

	if (bExportMetallicMap_DEPRECATED != DefaultObject.bExportMetallicMap_DEPRECATED)
	{
		MaterialSettings.bMetallicMap = bExportMetallicMap_DEPRECATED;
	}
	if (bExportRoughnessMap_DEPRECATED != DefaultObject.bExportRoughnessMap_DEPRECATED)
	{
		MaterialSettings.bRoughnessMap = bExportRoughnessMap_DEPRECATED;
	}
	if (bExportSpecularMap_DEPRECATED != DefaultObject.bExportSpecularMap_DEPRECATED)
	{
		MaterialSettings.bSpecularMap = bExportSpecularMap_DEPRECATED;
	}
	if (MergedMaterialAtlasResolution_DEPRECATED != DefaultObject.MergedMaterialAtlasResolution_DEPRECATED)
	{
		MaterialSettings.TextureSize.X = MergedMaterialAtlasResolution_DEPRECATED;
		MaterialSettings.TextureSize.Y = MergedMaterialAtlasResolution_DEPRECATED;
	}
	if (bCalculateCorrectLODModel_DEPRECATED != DefaultObject.bCalculateCorrectLODModel_DEPRECATED)
	{
		LODSelectionType = EMeshLODSelectionType::CalculateLOD;
	}

	if (ExportSpecificLOD_DEPRECATED != DefaultObject.ExportSpecificLOD_DEPRECATED)
	{
		SpecificLOD = ExportSpecificLOD_DEPRECATED;
		LODSelectionType = EMeshLODSelectionType::SpecificLOD;
	}
}
#endif

UEngineBaseTypes::UEngineBaseTypes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UEngineTypes::UEngineTypes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

ECollisionChannel UEngineTypes::ConvertToCollisionChannel(ETraceTypeQuery TraceType)
{
	return UCollisionProfile::Get()->ConvertToCollisionChannel(true, (int32)TraceType);
}

ECollisionChannel UEngineTypes::ConvertToCollisionChannel(EObjectTypeQuery ObjectType)
{
	return UCollisionProfile::Get()->ConvertToCollisionChannel(false, (int32)ObjectType);
}

EObjectTypeQuery UEngineTypes::ConvertToObjectType(ECollisionChannel CollisionChannel)
{
	return UCollisionProfile::Get()->ConvertToObjectType(CollisionChannel);
}

ETraceTypeQuery UEngineTypes::ConvertToTraceType(ECollisionChannel CollisionChannel)
{
	return UCollisionProfile::Get()->ConvertToTraceType(CollisionChannel);
}

FLightmassDebugOptions::FLightmassDebugOptions()
	: bDebugMode(false)
	, bStatsEnabled(false)
	, bGatherBSPSurfacesAcrossComponents(true)
	, CoplanarTolerance(0.001f)
	, bUseImmediateImport(true)
	, bImmediateProcessMappings(true)
	, bSortMappings(true)
	, bDumpBinaryFiles(false)
	, bDebugMaterials(false)
	, bPadMappings(true)
	, bDebugPaddings(false)
	, bOnlyCalcDebugTexelMappings(false)
	, bUseRandomColors(false)
	, bColorBordersGreen(false)
	, bColorByExecutionTime(false)
	, ExecutionTimeDivisor(15.0f)
{}

UActorComponent* FComponentReference::GetComponent(AActor* OwningActor) const
{
	UActorComponent* Result = nullptr;

	// Component is specified directly, use that
	if(OverrideComponent.IsValid())
	{
		Result = OverrideComponent.Get();
	}
	else
	{
		// Look in Actor if specified, OwningActor if not
		AActor* SearchActor = (OtherActor != NULL) ? ToRawPtr(OtherActor) : OwningActor;
		if(SearchActor)
		{
			if(ComponentProperty != NAME_None)
			{
				FObjectPropertyBase* ObjProp = FindFProperty<FObjectPropertyBase>(SearchActor->GetClass(), ComponentProperty);
				if(ObjProp != NULL)
				{
					// .. and return the component that is there
					Result = Cast<UActorComponent>(ObjProp->GetObjectPropertyValue_InContainer(SearchActor));
				}
			}
			else if (!PathToComponent.IsEmpty())
			{
				Result = FindObject<UActorComponent>(SearchActor, *PathToComponent);
			}
			else
			{
				Result = SearchActor->GetRootComponent();
			}
		}
	}

	return Result;
}

const TCHAR* LexToString(const EWorldType::Type Value)
{
	switch (Value)
	{
	case EWorldType::Type::Editor:
		return TEXT("Editor");
		break;
	case EWorldType::Type::EditorPreview:
		return TEXT("EditorPreview");
		break;
	case EWorldType::Type::Game:
		return TEXT("Game");
		break;
	case EWorldType::Type::GamePreview:
		return TEXT("GamePreview");
		break;
	case EWorldType::Type::GameRPC:
		return TEXT("GameRPC");
		break;
	case EWorldType::Type::Inactive:
		return TEXT("Inactive");
		break;
	case EWorldType::Type::PIE:
		return TEXT("PIE");
		break;
	case EWorldType::Type::None:
		return TEXT("None");
		break;
	default:
		return TEXT("Unknown");
		break;
	}
}

/// @endcond
