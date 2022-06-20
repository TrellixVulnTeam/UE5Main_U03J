// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/UVToolSelectionAPI.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "Selection/UVEditorMeshSelectionMechanic.h"
#include "Selection/UVToolSelectionHighlightMechanic.h"
#include "UVEditorMechanicAdapterTool.h"

#define LOCTEXT_NAMESPACE "UUVIslandConformalUnwrapAction"

using namespace UE::Geometry;

namespace UVToolSelectionAPILocals
{
	FText SelectionChangeTransactionName = LOCTEXT("SelectionChangeTransaction", "UV Selection Change");

	auto DoSelectionSetsDiffer(const TArray<FUVToolSelection>& OldSelections, const TArray<FUVToolSelection>& NewSelections)
	{
		if (NewSelections.Num() != OldSelections.Num())
		{
			return true;
		}

		for (const FUVToolSelection& Selection : NewSelections)
		{
			// Find the selection that points to the same target.
			const FUVToolSelection* FoundSelection = OldSelections.FindByPredicate(
				[&Selection](const FUVToolSelection& OldSelection) { return OldSelection.Target == Selection.Target; }
			);
			if (!FoundSelection || *FoundSelection != Selection)
			{
				return true;
			}
		}
		return false;
	};
}

void UUVToolSelectionAPI::Initialize(
	UInteractiveToolManager* ToolManagerIn,
	UWorld* UnwrapWorld, UInputRouter* UnwrapInputRouterIn, 
	UUVToolLivePreviewAPI* LivePreviewAPI,
	UUVToolEmitChangeAPI* EmitChangeAPIIn)
{
	UnwrapInputRouter = UnwrapInputRouterIn;
	EmitChangeAPI = EmitChangeAPIIn;

	MechanicAdapter = NewObject<UUVEditorMechanicAdapterTool>();
	MechanicAdapter->ToolManager = ToolManagerIn;

	HighlightMechanic = NewObject<UUVToolSelectionHighlightMechanic>();
	HighlightMechanic->Setup(MechanicAdapter);
	HighlightMechanic->Initialize(UnwrapWorld, LivePreviewAPI->GetLivePreviewWorld());

	SelectionMechanic = NewObject<UUVEditorMeshSelectionMechanic>();
	SelectionMechanic->Setup(MechanicAdapter);
	SelectionMechanic->Initialize(UnwrapWorld, this);
	UnwrapInputRouter->RegisterSource(MechanicAdapter);
}

void UUVToolSelectionAPI::SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
{
	Targets = TargetsIn;
	SelectionMechanic->SetTargets(Targets);

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->OnCanonicalModified.AddWeakLambda(this, [this](UUVEditorToolMeshInput* Target,
			const UUVEditorToolMeshInput::FCanonicalModifiedInfo ModifiedInfo)
			{
				FUVToolSelection* TargetSelection = CurrentSelections.FindByPredicate(
					[Target](const FUVToolSelection& CandidateSelection) { return CandidateSelection.Target == Target; });
				if (TargetSelection && TargetSelection->Type == FUVToolSelection::EType::Edge)
				{
					TargetSelection->RestoreFromStableEdgeIdentifiers(*Target->UnwrapCanonical);
				}
			});
	}
}

void UUVToolSelectionAPI::Shutdown()
{
	if (UnwrapInputRouter.IsValid())
	{
		// Make sure that we stop any captures that our mechanics may have, then remove them from
		// the input router.
		UnwrapInputRouter->ForceTerminateSource(MechanicAdapter);
		UnwrapInputRouter->DeregisterSource(MechanicAdapter);
		UnwrapInputRouter = nullptr;
	}

	HighlightMechanic->Shutdown();
	SelectionMechanic->Shutdown();

	MechanicAdapter->Shutdown(EToolShutdownType::Completed);
	HighlightMechanic = nullptr;
	SelectionMechanic = nullptr;
	MechanicAdapter = nullptr;

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->OnCanonicalModified.RemoveAll(this);
	}
	Targets.Empty();
}

void UUVToolSelectionAPI::ClearSelections(bool bBroadcast, bool bEmitChange)
{
	SetSelections(TArray<FUVToolSelection>(), bBroadcast, bEmitChange);
}

void UUVToolSelectionAPI::OnToolEnded(UInteractiveTool* DeadTool)
{
	if (SelectionMechanic)
	{
		SetSelectionMechanicOptions(FSelectionMechanicOptions());
		SelectionMechanic->SetIsEnabled(false);
	}
	if (HighlightMechanic)
	{
		SetHighlightVisible(false, false);
		SetHighlightOptions(FHighlightOptions());
	}

	OnSelectionChanged.RemoveAll(DeadTool);
	OnPreSelectionChange.RemoveAll(DeadTool);
	OnDragSelectionChanged.RemoveAll(DeadTool);
	
	if (UnwrapInputRouter.IsValid())
	{
		// Make sure that we stop any captures that our mechanics may have.
		UnwrapInputRouter->ForceTerminateSource(MechanicAdapter);
	}
	
}

void UUVToolSelectionAPI::SetSelections(const TArray<FUVToolSelection>& SelectionsIn, bool bBroadcast, bool bEmitChange)
{
	using namespace UVToolSelectionAPILocals;

	bCachedUnwrapSelectionCentroidValid = false;

	TArray<FUVToolSelection> NewSelections;
	for (const FUVToolSelection& NewSelection : SelectionsIn)
	{
		if (ensure(NewSelection.Target.IsValid() && NewSelection.Target->IsValid())
			// All of the selections should match type
			&& ensure(NewSelections.Num() == 0 || NewSelection.Type == NewSelections[0].Type)
			// Selection must not be empty, unless it's an edge selection stored as stable identifiers
			&& ensure(!NewSelection.IsEmpty() 
				|| (NewSelection.Type == FUVToolSelection::EType::Edge && NewSelection.HasStableEdgeIdentifiers()))
			// Shouldn't have selection objects pointing to same target
			&& ensure(!NewSelections.FindByPredicate(
				[&NewSelection](const FUVToolSelection& ExistingSelectionElement) {
					return NewSelection.Target == ExistingSelectionElement.Target;
				})))
		{
			NewSelections.Add(NewSelection);

			if (NewSelection.Target.IsValid() && ensure(NewSelection.Target->UnwrapCanonical))
			{
				checkSlow(NewSelection.AreElementsPresentInMesh(*NewSelection.Target->UnwrapCanonical));

				if (NewSelection.Type == FUVToolSelection::EType::Edge)
				{
					NewSelections.Last().SaveStableEdgeIdentifiers(*NewSelection.Target->UnwrapCanonical);
				}
			}
		}
	}

	if (!DoSelectionSetsDiffer(CurrentSelections, NewSelections))
	{
		return;
	}

	if (bEmitChange)
	{
		EmitChangeAPI->BeginUndoTransaction(SelectionChangeTransactionName);
	}

	if (bBroadcast)
	{
		OnPreSelectionChange.Broadcast(bEmitChange);
	}

	TUniquePtr<FSelectionChange> SelectionChange;
	if (bEmitChange)
	{
		SelectionChange = MakeUnique<FSelectionChange>();
		SelectionChange->SetBefore(MoveTemp(CurrentSelections));
	}

	CurrentSelections = MoveTemp(NewSelections);

	if (bEmitChange)
	{
		SelectionChange->SetAfter(CurrentSelections);
		EmitChangeAPI->EmitToolIndependentChange(this, MoveTemp(SelectionChange),
			SelectionChangeTransactionName);
	}

	if (HighlightOptions.bAutoUpdateUnwrap)
	{
		FTransform Transform = FTransform::Identity;
		if (HighlightOptions.bUseCentroidForUnwrapAutoUpdate)
		{
			Transform = FTransform(GetUnwrapSelectionCentroid());
		}
		RebuildUnwrapHighlight(Transform);
	}
	if (HighlightOptions.bAutoUpdateApplied)
	{
		RebuildAppliedPreviewHighlight();
	}

	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast(bEmitChange);
	}

	if (bEmitChange)
	{
		EmitChangeAPI->EndUndoTransaction();
	}
}

void UUVToolSelectionAPI::SetSelectionMechanicOptions(const FSelectionMechanicOptions& Options)
{
	SelectionMechanic->SetShowHoveredElements(Options.bShowHoveredElements);
}

void UUVToolSelectionAPI::SetSelectionMechanicEnabled(bool bIsEnabled)
{
	SelectionMechanic->SetIsEnabled(bIsEnabled);
}

void UUVToolSelectionAPI::SetSelectionMechanicMode(EUVEditorSelectionMode Mode,
	const FSelectionMechanicModeChangeOptions& Options)
{
	SelectionMechanic->SetSelectionMode(Mode, Options);
}

FVector3d UUVToolSelectionAPI::GetUnwrapSelectionCentroid(bool bForceRecalculate)
{
	if (bCachedUnwrapSelectionCentroidValid && !bForceRecalculate)
	{
		return CachedUnwrapSelectionCentroid;
	}

	FVector3d& Centroid = CachedUnwrapSelectionCentroid;

	Centroid = FVector3d::Zero();
	double Divisor = 0;
	for (const FUVToolSelection& Selection : GetSelections())
	{
		FDynamicMesh3* Mesh = Selection.Target->UnwrapCanonical.Get();
		if (Selection.Type == FUVToolSelection::EType::Edge)
		{
			for (int32 Eid : Selection.SelectedIDs)
			{
				Centroid += Mesh->GetEdgePoint(Eid, 0.5);
			}
		}
		else if (Selection.Type == FUVToolSelection::EType::Triangle)
		{
			for (int32 Tid : Selection.SelectedIDs)
			{
				Centroid += Mesh->GetTriCentroid(Tid);
			}
		}
		else if (Selection.Type == FUVToolSelection::EType::Vertex)
		{
			for (int32 Vid : Selection.SelectedIDs)
			{
				Centroid += Mesh->GetVertex(Vid);
			}
		}
		else
		{
			ensure(false);
		}

		Divisor += Selection.SelectedIDs.Num();
	}

	if (Divisor > 0)
	{
		Centroid /= Divisor;
	}

	bCachedUnwrapSelectionCentroidValid = true;
	return Centroid;
}


void UUVToolSelectionAPI::SetHighlightVisible(bool bUnwrapHighlightVisible, bool bAppliedHighlightVisible, bool bRebuild)
{
	HighlightMechanic->SetIsVisible(bUnwrapHighlightVisible, bAppliedHighlightVisible);
	if (bRebuild)
	{
		ClearHighlight(!bUnwrapHighlightVisible, !bAppliedHighlightVisible);
		if (bUnwrapHighlightVisible)
		{
			FTransform UnwrapTransform = FTransform::Identity;
			if (HighlightOptions.bUseCentroidForUnwrapAutoUpdate)
			{
				UnwrapTransform = FTransform(GetUnwrapSelectionCentroid());
			}
			RebuildUnwrapHighlight(UnwrapTransform);
		}
		if (bAppliedHighlightVisible)
		{
			RebuildAppliedPreviewHighlight();
		}
	}
}

void UUVToolSelectionAPI::SetHighlightOptions(const FHighlightOptions& Options)
{
	HighlightOptions = Options;

	HighlightMechanic->SetEnablePairedEdgeHighlights(Options.bShowPairedEdgeHighlights);
}

void UUVToolSelectionAPI::ClearHighlight(bool bClearForUnwrap, bool bClearForAppliedPreview)
{
	if (bClearForUnwrap)
	{
		HighlightMechanic->RebuildUnwrapHighlight(TArray<FUVToolSelection>(), FTransform::Identity);
	}
	if (bClearForAppliedPreview)
	{
		HighlightMechanic->RebuildAppliedHighlightFromUnwrapSelection(TArray<FUVToolSelection>());
	}
}

void UUVToolSelectionAPI::RebuildUnwrapHighlight(const FTransform& StartTransform)
{
	// Unfortunately even when tids and vids correspond between unwrap and canonical unwraps, eids
	// may differ, so we need to convert these over to preview unwrap eids if we're using previews.
	if (HighlightOptions.bBaseHighlightOnPreviews 
		&& GetSelectionsType() == FUVToolSelection::EType::Edge)
	{
		// TODO: We should probably have some function like "GetSelectionsInPreview(bool bForceRebuild)" 
		// that is publicly available and does caching.
		TArray<FUVToolSelection> ConvertedSelections = CurrentSelections;
		for (FUVToolSelection& ConvertedSelection : ConvertedSelections)
		{
			ConvertedSelection.SaveStableEdgeIdentifiers(*ConvertedSelection.Target->UnwrapCanonical);
			ConvertedSelection.RestoreFromStableEdgeIdentifiers(*ConvertedSelection.Target->UnwrapPreview->PreviewMesh->GetMesh());
		}
		HighlightMechanic->RebuildUnwrapHighlight(ConvertedSelections, StartTransform,
			HighlightOptions.bBaseHighlightOnPreviews);
	}
	else
	{
		HighlightMechanic->RebuildUnwrapHighlight(CurrentSelections, StartTransform,
			HighlightOptions.bBaseHighlightOnPreviews);
	}
}

void UUVToolSelectionAPI::SetUnwrapHighlightTransform(const FTransform& NewTransform)
{
	HighlightMechanic->SetUnwrapHighlightTransform(NewTransform, 
		HighlightOptions.bShowPairedEdgeHighlights, HighlightOptions.bBaseHighlightOnPreviews);
}

FTransform UUVToolSelectionAPI::GetUnwrapHighlightTransform()
{
	return HighlightMechanic->GetUnwrapHighlightTransform();
}

void UUVToolSelectionAPI::RebuildAppliedPreviewHighlight()
{
	// Unfortunately even when tids and vids correspond between unwrap and canonical unwraps, eids
	// may differ, so we need to convert these over to preview unwrap eids if we're using previews.
	if (HighlightOptions.bBaseHighlightOnPreviews
		&& GetSelectionsType() == FUVToolSelection::EType::Edge)
	{
		TArray<FUVToolSelection> ConvertedSelections = CurrentSelections;
		for (FUVToolSelection& ConvertedSelection : ConvertedSelections)
		{
			ConvertedSelection.SaveStableEdgeIdentifiers(*ConvertedSelection.Target->UnwrapCanonical);
			ConvertedSelection.RestoreFromStableEdgeIdentifiers(*ConvertedSelection.Target->UnwrapPreview->PreviewMesh->GetMesh());
		}
		HighlightMechanic->RebuildAppliedHighlightFromUnwrapSelection(ConvertedSelections,
			HighlightOptions.bBaseHighlightOnPreviews);
	}
	else
	{
		HighlightMechanic->RebuildAppliedHighlightFromUnwrapSelection(CurrentSelections,
			HighlightOptions.bBaseHighlightOnPreviews);
	}
}

void UUVToolSelectionAPI::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (SelectionMechanic)
	{
		SelectionMechanic->Render(RenderAPI);
	}
}
void UUVToolSelectionAPI::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (SelectionMechanic)
	{
		SelectionMechanic->DrawHUD(Canvas, RenderAPI);
	}
}

void UUVToolSelectionAPI::BeginChange()
{
	PendingSelectionChange = MakeUnique<FSelectionChange>();
	FSelectionChange* CastSelectionChange = static_cast<FSelectionChange*>(PendingSelectionChange.Get());
	CastSelectionChange->SetBefore(GetSelections());
}

bool UUVToolSelectionAPI::EndChangeAndEmitIfModified(bool bBroadcast)
{
	using namespace UVToolSelectionAPILocals;

	if (!PendingSelectionChange)
	{
		return false;
	}

	FSelectionChange* CastSelectionChange = static_cast<FSelectionChange*>(PendingSelectionChange.Get());
	
	// See if the selection has changed
	if (!DoSelectionSetsDiffer(CastSelectionChange->GetBefore(), GetSelections()))
	{
		PendingSelectionChange.Reset();
		return false;
	}

	EmitChangeAPI->BeginUndoTransaction(SelectionChangeTransactionName);
	if (bBroadcast)
	{
		OnPreSelectionChange.Broadcast(true);
	}
	CastSelectionChange->SetAfter(GetSelections());
	EmitChangeAPI->EmitToolIndependentChange(this, MoveTemp(PendingSelectionChange),
		SelectionChangeTransactionName);
	PendingSelectionChange.Reset();

	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast(true);
	}
	EmitChangeAPI->EndUndoTransaction();

	return true;
}

void UUVToolSelectionAPI::FSelectionChange::SetBefore(TArray<FUVToolSelection> SelectionsIn)
{
	Before = MoveTemp(SelectionsIn);
	if (bUseStableUnwrapCanonicalIDsForEdges)
	{
		for (FUVToolSelection& Selection : Before)
		{
			if (Selection.Type == FUVToolSelection::EType::Edge
				&& Selection.Target.IsValid()
				&& Selection.Target->UnwrapCanonical)
			{
				Selection.SaveStableEdgeIdentifiers(*Selection.Target->UnwrapCanonical);
				Selection.SelectedIDs.Empty(); // Don't need to store since we'll always restore
			}
		}
	}
}

void UUVToolSelectionAPI::FSelectionChange::SetAfter(TArray<FUVToolSelection> SelectionsIn)
{
	After = MoveTemp(SelectionsIn);
	if (bUseStableUnwrapCanonicalIDsForEdges)
	{
		for (FUVToolSelection& Selection : After)
		{
			if (Selection.Type == FUVToolSelection::EType::Edge
				&& Selection.Target.IsValid()
				&& Selection.Target->UnwrapCanonical)
			{
				Selection.SaveStableEdgeIdentifiers(*Selection.Target->UnwrapCanonical);
				Selection.SelectedIDs.Empty(); // Don't need to store since we'll always restore
			}
		}
	}
}

TArray<FUVToolSelection> UUVToolSelectionAPI::FSelectionChange::GetBefore() const
{
	TArray<FUVToolSelection> SelectionsOut;
	for (const FUVToolSelection& Selection : Before)
	{
		if (bUseStableUnwrapCanonicalIDsForEdges
			&& Selection.Type == FUVToolSelection::EType::Edge
			&& Selection.Target.IsValid()
			&& Selection.Target->UnwrapCanonical)
		{
			FUVToolSelection UnpackedSelection = Selection;
			UnpackedSelection.RestoreFromStableEdgeIdentifiers(*Selection.Target->UnwrapCanonical);
			SelectionsOut.Add(UnpackedSelection);
		}
		else
		{
			SelectionsOut.Add(Selection);
		}
	}
	return SelectionsOut;
}

void UUVToolSelectionAPI::FSelectionChange::Apply(UObject* Object) 
{
	UUVToolSelectionAPI* SelectionAPI = Cast<UUVToolSelectionAPI>(Object);
	if (SelectionAPI)
	{
		if (bUseStableUnwrapCanonicalIDsForEdges)
		{
			for (FUVToolSelection& Selection : After)
			{
				if (Selection.Type == FUVToolSelection::EType::Edge
					&& Selection.Target.IsValid()
					&& Selection.Target->UnwrapCanonical)
				{
					Selection.RestoreFromStableEdgeIdentifiers(*Selection.Target->UnwrapCanonical);
				}
			}
		}

		SelectionAPI->SetSelections(After, true, false);
	}
}

void UUVToolSelectionAPI::FSelectionChange::Revert(UObject* Object)
{
	UUVToolSelectionAPI* SelectionAPI = Cast<UUVToolSelectionAPI>(Object);
	if (SelectionAPI)
	{
		if (bUseStableUnwrapCanonicalIDsForEdges)
		{
			for (FUVToolSelection& Selection : Before)
			{
				if (Selection.Type == FUVToolSelection::EType::Edge
					&& Selection.Target.IsValid()
					&& Selection.Target->UnwrapCanonical)
				{
					Selection.RestoreFromStableEdgeIdentifiers(*Selection.Target->UnwrapCanonical);
				}
			}
		}

		SelectionAPI->SetSelections(Before, true, false);
	}
}

FString UUVToolSelectionAPI::FSelectionChange::ToString() const
{
	return TEXT("UUVToolSelectionAPI::FSelectionChange");
}


#undef LOCTEXT_NAMESPACE