// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

namespace UE::LegacyVertexDeltaModel
{
	class LEGACYVERTEXDELTAMODELEDITOR_API FLegacyVertexDeltaModelEditorModule
		: public IModuleInterface
	{
	public:
		// IModuleInterface overrides.
		void StartupModule() override;
		void ShutdownModule() override;
		// ~END IModuleInterface overrides.
	};
}	// namespace UE::LegacyVertexDeltaModel