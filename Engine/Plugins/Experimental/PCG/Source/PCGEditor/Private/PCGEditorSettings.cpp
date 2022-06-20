// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorSettings.h"

UPCGEditorSettings::UPCGEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultNodeColor = FLinearColor(0.4f, 0.62f, 1.0f);
	InputOutputNodeColor = FLinearColor(1.0f, 0.0f, 0.0f);
	SetOperationNodeColor = FLinearColor(0.8f, 0.2f, 0.8f);
	DensityOperationNodeColor = FLinearColor(0.6f, 1.0f, 0.6f);
	BlueprintNodeColor = FLinearColor(0.0f, 0.6f, 1.0f);
	MetadataNodeColor = FLinearColor(0.4f, 0.4f, 0.8f);
	FilterNodeColor = FLinearColor(0.4f, 0.8f, 0.4f);
	SamplerNodeColor = FLinearColor(0.8f, 1.0f, 0.4f);
	ArtifactNodeColor = FLinearColor(1.0f, 0.6f, 0.4f);
	SubgraphNodeColor = FLinearColor(1.0f, 0.1f, 0.1f);
}

FLinearColor UPCGEditorSettings::GetColor(UPCGSettings* Settings) const
{
	if (!Settings)
	{
		return DefaultNodeColor;
	}
	// First: check if there's an override
	else if (const FLinearColor* Override = OverrideNodeColorByClass.Find(Settings->GetClass()))
	{
		return *Override;
	}
	// Otherwise, check against the classes we know
	else if(Settings->GetType() == EPCGSettingsType::InputOutput)
	{
		return InputOutputNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Spatial)
	{
		return SetOperationNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Density)
	{
		return DensityOperationNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Blueprint)
	{
		return BlueprintNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Metadata)
	{
		return MetadataNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Filter)
	{
		return FilterNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Sampler)
	{
		return SamplerNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Artifact)
	{
		return ArtifactNodeColor;
	}
	else if (Settings->GetType() == EPCGSettingsType::Subgraph)
	{
		return SubgraphNodeColor;
	}
	else
	{
		// Finally, we couldn't find any match, so return the default value
		return DefaultNodeColor;
	}
}
