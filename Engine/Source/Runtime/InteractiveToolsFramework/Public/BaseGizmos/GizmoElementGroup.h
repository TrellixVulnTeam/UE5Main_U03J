// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "InputState.h"
#include "GizmoElementGroup.generated.h"

/**
 * Simple group object intended to be used as part of 3D Gizmos.
 * Contains multiple gizmo objects.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementGroup : public UGizmoElementBase
{
	GENERATED_BODY()

public:

	//~ Begin UGizmoElementBase Interface.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) override;
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End UGizmoElementBase Interface.

	// Add object to group.
	virtual void Add(UGizmoElementBase* InElement);

	// Remove object from group, if it exists
	virtual void Remove(UGizmoElementBase* InElement);

	// Reset cached render state
	virtual void ResetCachedRenderState();

	// When true, maintains view-dependent constant scale for this gizmo object hierarchy
	virtual void SetConstantScale(bool InConstantScale);
	virtual bool GetConstantScale() const;

protected:

	// When true, maintains view-dependent constant scale for this gizmo object hierarchy
	UPROPERTY()
	bool bConstantScale = false;

	// When true, this group is treated as a single element such that when LineTrace is called, if any of its sub-elements is hit, 
	// this group will be returned as the owner of the hit. This should be used when a group of elements should be treated as a single handle.
	UPROPERTY()
	bool bHitOwner = false;
		
	// Gizmo elements within this group
	UPROPERTY()
	TArray<TObjectPtr<UGizmoElementBase>> Elements;
};
