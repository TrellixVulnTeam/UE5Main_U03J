// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerTrainingModel.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerVizSettings.h"
#include "IHasPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaViewport.h"
#include "PersonaAssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "Math/Color.h"
#include "Widgets/Notifications/SNotificationList.h"

class IDetailsView;
class UMLDeformerAsset;
class SSimpleTimeSlider;

namespace UE::MLDeformer
{
	namespace MLDeformerEditorModes
	{
		extern const FName Editor;
	}

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorToolkit :
		public FPersonaAssetEditorToolkit,
		public IHasPersonaToolkit,
		public FGCObject,
		public FEditorUndoClient,
		public FTickableEditorObject
	{
	public:
		friend class FMLDeformerApplicationMode;
		friend struct FMLDeformerVizSettingsTabSummoner;

		/** Initialize the asset editor. This will register the application mode, init the preview scene, etc. */
		void InitAssetEditor(
			const EToolkitMode::Type Mode,
			const TSharedPtr<IToolkitHost>& InitToolkitHost,
			UMLDeformerAsset* InDeformerAsset);

		// FAssetEditorToolkit overrides.
		virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
		virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
		virtual FName GetToolkitFName() const override;
		virtual FText GetBaseToolkitName() const override;
		virtual FText GetToolkitName() const override;
		virtual FLinearColor GetWorldCentricTabColorScale() const override;
		virtual FString GetWorldCentricTabPrefix() const override;
		// ~END FAssetEditorToolkit overrides.

		// FGCObject overrides.
		virtual FString GetReferencerName() const override { return TEXT("FMLDeformerEditorToolkit"); }
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		// ~END FGCObject overrides.

		// FTickableEditorObject overrides.
		virtual void Tick(float DeltaTime) override {};
		virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
		virtual TStatId GetStatId() const override;
		// ~END FTickableEditorObject overrides.

		// IHasPersonaToolkit overrides.
		virtual TSharedRef<IPersonaToolkit> GetPersonaToolkit() const override;
		IPersonaToolkit* GetPersonaToolkitPointer() const { return PersonaToolkit.Get(); }
		// ~END IHasPersonaToolkit overrides.

		void SetVizSettingsDetailsView(TSharedPtr<IDetailsView> InDetailsView) { VizSettingsDetailsView = InDetailsView; }

		IDetailsView* GetModelDetailsView() const;
		IDetailsView* GetVizSettingsDetailsView() const;

		void SetTimeSlider(TSharedPtr<SSimpleTimeSlider> InTimeSlider);
		SSimpleTimeSlider* GetTimeSlider() const;

		UMLDeformerAsset* GetDeformerAsset() const { return DeformerAsset.Get(); }
		FMLDeformerEditorModel* GetActiveModel() { return ActiveModel.Get(); }

		double CalcTimelinePosition() const;
		void OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing);
		void UpdateTimeSliderRange();
		void SetTimeSliderRange(double StartTime, double EndTime);

	private:
		/* Toolbar related. */
		void ExtendToolbar();
		void FillToolbar(FToolBarBuilder& ToolbarBuilder);

		/** Preview scene setup. */
		void HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
		void HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport);
		void HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView);
		void OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent);

		/** Helpers. */
		void ShowNotification(const FText& Message, SNotificationItem::ECompletionState State, bool PlaySound) const;
		FText GetOverlayText() const;
		void OnSwitchedVisualizationMode();
		bool HandleTrainingResult(ETrainingResult TrainingResult, double TrainingDuration);
		void OnModelChanged(int Index);
		void OnVizModeChanged(EMLDeformerVizMode Mode);
		FText GetActiveModelName() const;
		FText GetCurrentVizModeName() const;
		FText GetVizModeName(EMLDeformerVizMode Mode) const;
		void ZoomOnActors();
		void ShowNoModelsWarningIfNeeded();

		TSharedRef<SWidget> GenerateModelButtonContents(TSharedRef<FUICommandList> InCommandList);
		TSharedRef<SWidget> GenerateVizModeButtonContents(TSharedRef<FUICommandList> InCommandList);

	private:
		/** The persona toolkit. */	
		TSharedPtr<IPersonaToolkit> PersonaToolkit;

		/** Model details view. */
		TSharedPtr<IDetailsView> ModelDetailsView;

		/** Model viz settings details view. */
		TSharedPtr<IDetailsView> VizSettingsDetailsView;

		/** The timeline slider widget. */
		TSharedPtr<SSimpleTimeSlider> TimeSlider;

		/** The currently active editor model. */
		TSharedPtr<FMLDeformerEditorModel> ActiveModel;

		// Persona viewport.
		TSharedPtr<IPersonaViewport> PersonaViewport;

		/** The ML Deformer Asset. */
		TObjectPtr<UMLDeformerAsset> DeformerAsset;

		/** Has the asset editor been initialized? */
		bool bIsInitialized = false;
	};
}	// namespace UE::MLDeformer
