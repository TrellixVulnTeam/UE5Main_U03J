// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Selection/UVToolSelectionAPI.h"

#include "UVToolViewportButtonsAPI.generated.h"

/**
 * Allows tools to interact with buttons in the viewport
 */
UCLASS()
class UVEDITORTOOLS_API UUVToolViewportButtonsAPI : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	enum class EGizmoMode
	{
		Select,
		Transform
	};

	using ESelectionMode = UUVToolSelectionAPI::EUVEditorSelectionMode;

	void SetGizmoButtonsEnabled(bool bOn)
	{
		bGizmoButtonsEnabled = bOn;
	}

	bool AreGizmoButtonsEnabled()
	{
		return bGizmoButtonsEnabled;
	}

	void SetGizmoMode(EGizmoMode ModeIn, bool bBroadcast = true)
	{
		GizmoMode = ModeIn;
		if (bBroadcast)
		{
			OnGizmoModeChange.Broadcast(GizmoMode);
		}
	}

	EGizmoMode GetGizmoMode()
	{
		return GizmoMode;
	}


	void SetSelectionButtonsEnabled(bool bOn)
	{
		bSelectionButtonsEnabled = bOn;
	}

	bool AreSelectionButtonsEnabled()
	{
		return bSelectionButtonsEnabled;
	}

	void SetSelectionMode(ESelectionMode ModeIn, bool bBroadcast = true)
	{
		SelectionMode = ModeIn;
		if (bBroadcast)
		{
			OnSelectionModeChange.Broadcast(SelectionMode);
		}
	}

	ESelectionMode GetSelectionMode()
	{
		return SelectionMode;
	}

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGizmoModeChange, EGizmoMode NewGizmoMode);
	FOnGizmoModeChange OnGizmoModeChange;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionModeChange, ESelectionMode NewSelectionMode);
	FOnSelectionModeChange OnSelectionModeChange;

	virtual void OnToolEnded(UInteractiveTool* DeadTool) override 
	{
		OnGizmoModeChange.RemoveAll(DeadTool);
		OnSelectionModeChange.RemoveAll(DeadTool);
	}

protected:

	bool bGizmoButtonsEnabled = false;
	EGizmoMode GizmoMode = EGizmoMode::Select;
	bool bSelectionButtonsEnabled = false;
	ESelectionMode SelectionMode = ESelectionMode::Island;
};