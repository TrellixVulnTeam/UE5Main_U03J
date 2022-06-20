// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FrameTypes.h"
#include "GeometryBase.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "Selection/UVToolSelection.h"
#include "Selection/UVToolSelectionAPI.h" // IUVToolSupportsSelection

#include "UVSelectTool.generated.h"

class UCombinedTransformGizmo;
class UTransformProxy;
class UUVEditorToolMeshInput;
class UUVToolEmitChangeAPI;
class UUVToolViewportButtonsAPI;

UCLASS()
class UVEDITORTOOLS_API UUVSelectToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	// This is a pointer so that it can be updated under the builder without
	// having to set it in the mode after initializing targets.
	const TArray<TObjectPtr<UUVEditorToolMeshInput>>* Targets = nullptr;
};

/**
 * The tool in the UV editor that secretly runs when other tools are not running. It uses the
 * selection api to allow the user to select elements, and has a gizmo that can be used to
 * transform these elements.
 */
UCLASS()
class UVEDITORTOOLS_API UUVSelectTool : public UInteractiveTool, public IUVToolSupportsSelection
{
	GENERATED_BODY()

public:
	
	/**
	 * The tool will operate on the meshes given here.
	 */
	virtual void SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
	{
		Targets = TargetsIn;
	}

	// Used by undo/redo.
	FTransform GetGizmoTransform() const;
	void SetGizmoTransform(const FTransform& NewTransform);

	// UInteractiveTool
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }

protected:
	virtual void OnSelectionChanged(bool bEmitChangeAllowed);

	// Callbacks we'll receive from the gizmo proxy
	virtual void GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	virtual void GizmoTransformStarted(UTransformProxy* Proxy);
	virtual void GizmoTransformEnded(UTransformProxy* Proxy);

	virtual void ApplyGizmoTransform();
	virtual void UpdateGizmo();

	UPROPERTY()
	TArray<TObjectPtr<UUVEditorToolMeshInput>> Targets;

	UPROPERTY()
	TObjectPtr<UUVToolViewportButtonsAPI> ViewportButtonsAPI = nullptr;
	
	UPROPERTY()
	TObjectPtr<UUVToolEmitChangeAPI> EmitChangeAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVToolSelectionAPI> SelectionAPI = nullptr;

	UPROPERTY()
	TObjectPtr<UUVEditorMeshSelectionMechanic> SelectionMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UCombinedTransformGizmo> TransformGizmo = nullptr;

	UE::Geometry::FFrame3d InitialGizmoFrame;
	FTransform UnappliedGizmoTransform;
	bool bInDrag = false;
	bool bUpdateGizmoOnCanonicalChange = true;
	bool bGizmoTransformNeedsApplication = false;

	TArray<UE::Geometry::FUVToolSelection> CurrentSelections;
	// The outer arrays are 1:1 with the Selections array obtained from the Selection API
	TArray<TArray<int32>> RenderUpdateTidsPerSelection;
	// Inner arrays for these two are 1:1 with each other
	TArray<TArray<int32>> MovingVidsPerSelection;
	TArray<TArray<FVector3d>> MovingVertOriginalPositionsPerSelection;
};

