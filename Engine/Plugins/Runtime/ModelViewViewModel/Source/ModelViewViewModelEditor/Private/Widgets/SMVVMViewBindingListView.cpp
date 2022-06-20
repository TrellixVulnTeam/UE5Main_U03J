// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMViewBindingListView.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Dialog/SCustomDialog.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyAccessEditor.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMPropertyPathHelpers.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "SEnumCombo.h"
#include "SSimpleButton.h" 
#include "Styling/MVVMEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/SMVVMConversionPath.h"
#include "Widgets/SMVVMFieldSelector.h"
#include "Widgets/SMVVMPropertyPath.h"
#include "Widgets/SMVVMSourceSelector.h"
#include "Widgets/SMVVMViewBindingPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "BindingListView"

struct FMVVMViewBindingListEntry
{
	FMVVMViewBindingListEntry(int32 InIndex) : Index(InIndex) {}
	int32 Index;
};

class SMVVMViewBindingListEntryRow : public SMultiColumnTableRow<FMVVMViewBindingListEntryPtr>
{
public:
	static FName EnabledColumnName;
	static FName ErrorColumnName;
	static FName ViewModelColumnName;
	static FName ViewModelPropertyColumnName;
	static FName ModeColumnName;
	static FName WidgetColumnName;
	static FName WidgetPropertyColumnName;
	static FName UpdateColumnName;
	static FName ConversionFunctionColumnName;
	static FName DropDownOptionsColumnName;

public:
	SLATE_BEGIN_ARGS(SMVVMViewBindingListEntryRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView, const FMVVMViewBindingListEntryPtr& InEntry, UMVVMBlueprintView* InBlueprintView, UWidgetBlueprint* InWidgetBlueprint)
	{
		Entry = InEntry;
		BlueprintView = InBlueprintView;
		WidgetBlueprint = InWidgetBlueprint;

		OnBlueprintChangedHandle = WidgetBlueprint->OnChanged().AddSP(this, &SMVVMViewBindingListEntryRow::HandleBlueprintChanged);

		FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();

		ViewModelHelper = MakeUnique<UE::MVVM::FViewModelFieldPathHelper>(&ViewModelBinding->ViewModelPath, WidgetBlueprint);
		WidgetHelper = MakeUnique<UE::MVVM::FWidgetFieldPathHelper>(&ViewModelBinding->WidgetPath, WidgetBlueprint);

		SMultiColumnTableRow<FMVVMViewBindingListEntryPtr>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			OwnerTableView
		);
	}

	~SMVVMViewBindingListEntryRow()
	{
		WidgetBlueprint->OnChanged().Remove(OnBlueprintChangedHandle);
	}

	FMVVMBlueprintViewBinding* GetThisViewBinding() const
	{
		if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
		{
			FMVVMBlueprintViewBinding* ViewBinding = BlueprintViewPtr->GetBindingAt(Entry->Index);
			return ViewBinding;
		}

		return nullptr;
	}

	TArray<FMVVMBlueprintViewBinding*> GetThisViewBindingAsArray() const
	{
		TArray<FMVVMBlueprintViewBinding*> Result;

		FMVVMBlueprintViewBinding* ViewBinding = GetThisViewBinding();
		if (ViewBinding != nullptr)
		{
			Result.Add(ViewBinding);
		}

		return Result;
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();

		if (ColumnName == EnabledColumnName)
		{
			return SNew(SCheckBox)
				.IsChecked(this, &SMVVMViewBindingListEntryRow::IsBindingEnabled)
				.OnCheckStateChanged(this, &SMVVMViewBindingListEntryRow::OnIsBindingEnableChanged);
		}
		else if (ColumnName == ErrorColumnName)
		{
			return SNew(SSimpleButton)
				.Icon(FAppStyle::Get().GetBrush("Icons.Error"))
				.Visibility(this, &SMVVMViewBindingListEntryRow::GetErrorVisibility)
				.ToolTipText(this, &SMVVMViewBindingListEntryRow::GetErrorToolTip)
				.OnClicked(this, &SMVVMViewBindingListEntryRow::OnErrorButtonClicked);
		}
		else if (ColumnName == ViewModelColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SAssignNew(ViewModelSourceSelector, SMVVMSourceSelector)
					.PathHelpers(this, &SMVVMViewBindingListEntryRow::GetViewModelHelpers)
					.OnSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnViewModelSelectionChanged)
				];
		}
		else if (ColumnName == ViewModelPropertyColumnName)
		{

			return SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SAssignNew(ViewModelFieldSelector, SMVVMFieldSelector)
					.PathHelpers(GetViewModelHelpers())
					.CounterpartHelpers(GetWidgetHelpers())
					.BindingMode(this, &SMVVMViewBindingListEntryRow::GetCurrentBindingMode)
					.IsSource(true)
					.OnSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnViewModelPropertySelectionChanged)
				];
		}
		else if (ColumnName == ModeColumnName)
		{
			UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();
			for (int32 Index = 0; Index < ModeEnum->NumEnums() - 1; ++Index)
			{
				const bool bIsHidden = ModeEnum->HasMetaData(TEXT("Hidden"), Index);
				if (!bIsHidden)
				{
					ModeNames.Add(ModeEnum->GetNameByIndex(Index));
				}
			}

			return 
				SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SNew(SComboBox<FName>)
					.OptionsSource(&ModeNames)
					.InitiallySelectedItem(ModeEnum->GetNameByValue((int64) ViewModelBinding->BindingType))
					.OnSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnModeSelectionChanged)
					.OnGenerateWidget(this, &SMVVMViewBindingListEntryRow::GenerateModeWidget)
					.Content()
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SImage)
							.Image(this, &SMVVMViewBindingListEntryRow::GetCurrentModeBrush)
						]
					]
				];
		}
		else if (ColumnName == WidgetColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SAssignNew(WidgetSourceSelector, SMVVMSourceSelector)
					.PathHelpers(this, &SMVVMViewBindingListEntryRow::GetWidgetHelpers)
					.OnSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnWidgetSelectionChanged)
				];
		}
		else if (ColumnName == WidgetPropertyColumnName)
		{
			return SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SAssignNew(WidgetFieldSelector, SMVVMFieldSelector)
					.PathHelpers(GetWidgetHelpers())
					.CounterpartHelpers(GetViewModelHelpers())
					.BindingMode(this, &SMVVMViewBindingListEntryRow::GetCurrentBindingMode)
					.IsSource(false)
					.OnSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnWidgetPropertySelectionChanged)
				];
		}
		else if (ColumnName == UpdateColumnName)
		{
			const UEnum* UpdateModeEnum = StaticEnum<EMVVMViewBindingUpdateMode>();
			return SNew(SBox)
				.Padding(FMargin(2, 0))
				.VAlign(VAlign_Center)
				[
					SNew(SEnumComboBox, UpdateModeEnum)
					.ContentPadding(FMargin(4, 0))
					.OnEnumSelectionChanged(this, &SMVVMViewBindingListEntryRow::OnUpdateModeSelectionChanged)
					.CurrentValue(this, &SMVVMViewBindingListEntryRow::GetUpdateModeValue)
				];
		}
		else if (ColumnName == ConversionFunctionColumnName)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SMVVMConversionPath, WidgetBlueprint, false)
					.Bindings(this, &SMVVMViewBindingListEntryRow::GetThisViewBindingAsArray)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.AutoWidth()
				[
					SNew(SMVVMConversionPath, WidgetBlueprint, true)
					.Bindings(this, &SMVVMViewBindingListEntryRow::GetThisViewBindingAsArray)
				];
		}
		else if (ColumnName == DropDownOptionsColumnName)
		{
			return SAssignNew(ContextMenuOptionHelper, SButton)
				.ToolTipText(LOCTEXT("DropDownOptionsToolTip", "Context Menu for Binding"))
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.OnClicked(this, &SMVVMViewBindingListEntryRow::HandleDropDownOptionsPressed)
				[
					SNew(SBox)
					.Padding(FMargin(3, 0))
					[
						SNew(SImage)
						.Image(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SegmentedCombo.Right").DownArrowImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				];
		}
		
		return SNullWidget::NullWidget;
	}

private:

	ECheckBoxState IsBindingEnabled() const
	{
		if (const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			return ViewModelBinding->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		return ECheckBoxState::Undetermined;
	}

	EVisibility GetErrorVisibility() const
	{
		return GetThisViewBinding()->Errors.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible;
	}

	FText GetErrorToolTip() const
	{
		static const FText NewLineText = FText::FromString(TEXT("\n"));
		FText HintText = LOCTEXT("ErrorButtonText", "Errors: (Click to show in a separate window)");
		FText ErrorsText = FText::Join(NewLineText, GetThisViewBinding()->Errors);

		return FText::Join(NewLineText, HintText, ErrorsText);
	}

	FReply OnErrorButtonClicked()
	{
		ErrorDialog.Reset();
		ErrorItems.Reset();

		if (const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			for (const FText& ErrorText : ViewModelBinding->Errors)
			{
				ErrorItems.Add(MakeShared<FText>(ErrorText));
			}

			ErrorDialog = SNew(SCustomDialog)
				.Buttons({
					SCustomDialog::FButton(LOCTEXT("OK", "OK"))
				})
				.Content()
				[
					SNew(SListView<TSharedPtr<FText>>)
					.ListItemsSource(&ErrorItems)
					.OnGenerateRow(this, &SMVVMViewBindingListEntryRow::OnGenerateErrorRow)
				];

			ErrorDialog->Show();
		}

		return FReply::Handled();
	}

	EMVVMBindingMode GetCurrentBindingMode() const
	{
		const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();
		return ViewModelBinding->BindingType;
	}

	UMVVMSubsystem::FConstDirectionalBindingArgs GetCurrentBindingArgs(bool bIsGetter) const
	{
		UE::MVVM::FMVVMConstFieldVariant ViewModelField = ViewModelHelper->GetSelectedField();
		UE::MVVM::FMVVMConstFieldVariant WidgetField = WidgetHelper->GetSelectedField();

		const FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding();

		UMVVMSubsystem::FConstDirectionalBindingArgs Args;
		if (bIsGetter)
		{
			Args.SourceBinding = ViewModelField;
			Args.DestinationBinding = WidgetField;
		}
		else
		{
			Args.SourceBinding = WidgetField;
			Args.DestinationBinding = ViewModelField;
		}

		return MoveTemp(Args);
	}

	TSharedRef<ITableRow> OnGenerateErrorRow(TSharedPtr<FText> Text, const TSharedRef<STableViewBase>& TableView) const
	{
		return SNew(STableRow<TSharedPtr<FText>>, TableView)
			.Content()
			[
				SNew(SEditableTextBox)
				.IsReadOnly(true)
				.Text(*Text.Get())
			];
	}

	void OnViewModelSelectionChanged(UE::MVVM::FBindingSource Source)
	{
		OnSourceSelectionChanged(Source, ViewModelHelper.Get());
		if (ViewModelFieldSelector.IsValid())
		{
			ViewModelFieldSelector->Refresh();
		}
	}

	void OnWidgetSelectionChanged(UE::MVVM::FBindingSource Source)
	{
		OnSourceSelectionChanged(Source, WidgetHelper.Get());
		if (WidgetFieldSelector.IsValid())
		{
			WidgetFieldSelector->Refresh();
		}
	}

	void OnSourceSelectionChanged(UE::MVVM::FBindingSource SelectedSource, UE::MVVM::IFieldPathHelper* PathHelper)
	{
		if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
		{
			BlueprintViewPtr->PreEditChange(UMVVMBlueprintView::StaticClass()->FindPropertyByName("Bindings"));

			PathHelper->SetSelectedSource(SelectedSource);
			PathHelper->ResetBinding(); // Might make sense to keep this around in case we retarget to a compatible widget or switch back.

			BlueprintViewPtr->PostEditChange();
		}
	}

	void OnViewModelPropertySelectionChanged(UE::MVVM::FMVVMConstFieldVariant SelectedField)
	{
		UE::MVVM::FMVVMConstFieldVariant CurrentField = ViewModelHelper->GetSelectedField();
		if (CurrentField != SelectedField)
		{
			OnPropertySelectionChanged(SelectedField, ViewModelHelper.Get());

			if (WidgetFieldSelector.IsValid())
			{
				WidgetFieldSelector->Refresh();
			}
		}
	}

	void OnWidgetPropertySelectionChanged(UE::MVVM::FMVVMConstFieldVariant SelectedField)
	{
		UE::MVVM::FMVVMConstFieldVariant CurrentField = WidgetHelper->GetSelectedField();
		if (CurrentField != SelectedField)
		{
			OnPropertySelectionChanged(SelectedField, WidgetHelper.Get());

			if (ViewModelFieldSelector.IsValid())
			{
				ViewModelFieldSelector->Refresh();
			}
		}
	}

	void OnPropertySelectionChanged(UE::MVVM::FMVVMConstFieldVariant SelectedField, UE::MVVM::IFieldPathHelper* PathHelper)
	{
		if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
		{
			BlueprintViewPtr->PreEditChange(UMVVMBlueprintView::StaticClass()->FindPropertyByName("Bindings"));

			PathHelper->SetBindingReference(SelectedField);

			BlueprintViewPtr->PostEditChange();
		}
	}

	void OnUpdateModeSelectionChanged(int32 Value, ESelectInfo::Type)
	{
		if (FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			if (ViewModelBinding->UpdateMode != (EMVVMViewBindingUpdateMode) Value)
			{
				if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
				{
					BlueprintViewPtr->PreEditChange(UMVVMBlueprintView::StaticClass()->FindPropertyByName("Bindings"));

					ViewModelBinding->UpdateMode = (EMVVMViewBindingUpdateMode) Value;

					BlueprintViewPtr->PostEditChange();
				}
			}
		}
	}

	int32 GetUpdateModeValue() const
	{
		return (int32) GetThisViewBinding()->UpdateMode;
	}

	void OnIsBindingEnableChanged(ECheckBoxState NewState)
	{
		if (NewState == ECheckBoxState::Undetermined)
		{
			return;
		}

		if (FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			bool bNewEnabled = (NewState == ECheckBoxState::Checked);
			if (ViewModelBinding->bEnabled != bNewEnabled)
			{
				if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
				{
					BlueprintViewPtr->PreEditChange(UMVVMBlueprintView::StaticClass()->FindPropertyByName("Bindings"));

					ViewModelBinding->bEnabled = bNewEnabled;

					BlueprintViewPtr->PostEditChange();
				}
			}
		}
	}

	const FSlateBrush* GetModeBrush(EMVVMBindingMode BindingMode) const
	{
		switch (BindingMode)
		{
		case EMVVMBindingMode::OneTimeToDestination:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneTime");
		case EMVVMBindingMode::OneWayToDestination:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneWay");
		case EMVVMBindingMode::OneWayToSource:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.OneWayToSource");
		case EMVVMBindingMode::OneTimeToSource:
			return nullptr;
		case EMVVMBindingMode::TwoWay:
			return FMVVMEditorStyle::Get().GetBrush("BindingMode.TwoWay");
		default:
			return nullptr;
		}
	}

	const FSlateBrush* GetCurrentModeBrush() const
	{
		return GetModeBrush(GetThisViewBinding()->BindingType);
	}

	const FText& GetModeLabel(EMVVMBindingMode BindingMode) const
	{
		static FText OneTimeToDestinationLabel = LOCTEXT("OneTimeToDestinationLabel", "One Time To Widget");
		static FText OneWayToDestinationLabel = LOCTEXT("OneWayToDestinationLabel", "One Way To Widget");
		static FText OneWayToSourceLabel = LOCTEXT("OneWayToSourceLabel", "One Way To View Model");
		static FText OneTimeToSourceLabel = LOCTEXT("OneTimeToSourceLabel", "One Time To View Model");
		static FText TwoWayLabel = LOCTEXT("TwoWayLabel", "Two Way");

		switch (BindingMode)
		{
		case EMVVMBindingMode::OneTimeToDestination:
			return OneTimeToDestinationLabel;
		case EMVVMBindingMode::OneWayToDestination:
			return OneWayToDestinationLabel;
		case EMVVMBindingMode::OneWayToSource:
			return OneWayToSourceLabel;
		case EMVVMBindingMode::OneTimeToSource:
			return OneTimeToSourceLabel;
		case EMVVMBindingMode::TwoWay:
			return TwoWayLabel;
		default:
			return FText::GetEmpty();
		}
	}

	TSharedRef<SWidget> GenerateModeWidget(FName ValueName) const
	{
		const UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();
		int32 Index = ModeEnum->GetIndexByName(ValueName);
		EMVVMBindingMode MVVMBindingMode = EMVVMBindingMode(Index);
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.WidthOverride(16)
				.HeightOverride(16)
				[
					SNew(SImage)
					.Image(GetModeBrush(MVVMBindingMode))
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(GetModeLabel(MVVMBindingMode))
				.ToolTipText(ModeEnum->GetToolTipTextByIndex(Index))
			];
	}

	void OnModeSelectionChanged(FName ValueName, ESelectInfo::Type)
	{
		if (FMVVMBlueprintViewBinding* ViewModelBinding = GetThisViewBinding())
		{
			const UEnum* ModeEnum = StaticEnum<EMVVMBindingMode>();
			EMVVMBindingMode NewMode = (EMVVMBindingMode) ModeEnum->GetValueByName(ValueName);

			if (ViewModelBinding->BindingType != NewMode)
			{
				if (UMVVMBlueprintView* BlueprintViewPtr = BlueprintView.Get())
				{
					BlueprintViewPtr->PreEditChange(UMVVMBlueprintView::StaticClass()->FindPropertyByName("Bindings"));

					ViewModelBinding->BindingType = NewMode;

					BlueprintViewPtr->PostEditChange();
				}

				ViewModelFieldSelector->Refresh();
				WidgetFieldSelector->Refresh();
			}
		}
	}

	FReply HandleDropDownOptionsPressed() 
	{
		if (TSharedPtr<ITypedTableView<FMVVMViewBindingListEntryPtr>> ListView = OwnerTablePtr.Pin())
		{
			if (TSharedPtr<SMVVMViewBindingListView> ParentList = StaticCastSharedPtr<SMVVMViewBindingListView>(ListView->AsWidget()->GetParentWidget()))
			{
				// Get the context menu content. If invalid, don't open a menu.
				ListView->Private_SetItemSelection(Entry, true);
				TSharedPtr<SWidget> MenuContent = ParentList->OnSourceConstructContextMenu();

				if (MenuContent.IsValid())
				{
					const FVector2D& SummonLocation = ContextMenuOptionHelper->GetCachedGeometry().GetRenderBoundingRect().GetBottomLeft();
					FWidgetPath WidgetPath;
					FSlateApplication::Get().PushMenu(ParentList->AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
				}
			}
		}

		return FReply::Handled();
	}

	TArray<UE::MVVM::IFieldPathHelper*> GetWidgetHelpers() const
	{
		return TArray<UE::MVVM::IFieldPathHelper*> { WidgetHelper.Get() };
	}

	TArray<UE::MVVM::IFieldPathHelper*> GetViewModelHelpers() const
	{
		return TArray<UE::MVVM::IFieldPathHelper*> { ViewModelHelper.Get() };
	}

	void HandleBlueprintChanged(UBlueprint* Blueprint)
	{
		ViewModelSourceSelector->Refresh();
		ViewModelFieldSelector->Refresh();
		WidgetSourceSelector->Refresh();
		WidgetFieldSelector->Refresh();
	}

private:
	FMVVMViewBindingListEntryPtr Entry;
	TWeakObjectPtr<UMVVMBlueprintView> BlueprintView;
	UWidgetBlueprint* WidgetBlueprint { nullptr };
	TUniquePtr<UE::MVVM::FViewModelFieldPathHelper> ViewModelHelper;
	TUniquePtr<UE::MVVM::FWidgetFieldPathHelper> WidgetHelper;
	TSharedPtr<SMVVMSourceSelector> ViewModelSourceSelector;
	TSharedPtr<SMVVMFieldSelector> ViewModelFieldSelector;
	TSharedPtr<SMVVMSourceSelector> WidgetSourceSelector;
	TSharedPtr<SMVVMFieldSelector> WidgetFieldSelector;
	TSharedPtr<SWidget> ContextMenuOptionHelper;
	TSharedPtr<SCustomDialog> ErrorDialog;
	TSharedPtr<SMenuAnchor> GetterConversionFunctionAnchor;
	TSharedPtr<SMenuAnchor> SetterConversionFunctionAnchor;
	TArray<TSharedPtr<FText>> ErrorItems;
	TArray<FName> ModeNames;
	FDelegateHandle OnBlueprintChangedHandle;
};

FName SMVVMViewBindingListEntryRow::EnabledColumnName = "Enabled";
FName SMVVMViewBindingListEntryRow::ErrorColumnName = "Error";
FName SMVVMViewBindingListEntryRow::ViewModelColumnName = "ViewModel";
FName SMVVMViewBindingListEntryRow::ViewModelPropertyColumnName = "ViewModelProperty";
FName SMVVMViewBindingListEntryRow::ModeColumnName = "Mode";
FName SMVVMViewBindingListEntryRow::WidgetColumnName = "Widget";
FName SMVVMViewBindingListEntryRow::WidgetPropertyColumnName = "WidgetProperty";
FName SMVVMViewBindingListEntryRow::UpdateColumnName = "Update";
FName SMVVMViewBindingListEntryRow::ConversionFunctionColumnName = "ConversionFunction";
FName SMVVMViewBindingListEntryRow::DropDownOptionsColumnName = "DropdownOptions";

/** */
void SMVVMViewBindingListView::Construct(const FArguments& InArgs, TSharedPtr<SMVVMViewBindingPanel> Owner, UMVVMWidgetBlueprintExtension_View* InMVVMExtension)
{
	BindingPanel = Owner;
	MVVMExtension = InMVVMExtension;
	check(InMVVMExtension);

	MVVMExtension->OnBlueprintViewChangedDelegate().AddSP(this, &SMVVMViewBindingListView::RequestListRefresh);
	MVVMExtension->GetBlueprintView()->OnBindingsUpdated.AddSP(this, &SMVVMViewBindingListView::RequestListRefresh);
	MVVMExtension->GetBlueprintView()->OnViewModelsUpdated.AddSP(this, &SMVVMViewBindingListView::RequestListRefresh);

	RequestListRefresh();

	ChildSlot
	[
		SAssignNew(ListView, SListView<FMVVMViewBindingListEntryPtr>)
		.ListItemsSource(&SourceData)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SMVVMViewBindingListView::MakeSourceListViewWidget)
		.OnContextMenuOpening(this, &SMVVMViewBindingListView::OnSourceConstructContextMenu)
		.OnSelectionChanged(this, &SMVVMViewBindingListView::OnSourceListSelectionChanged)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::EnabledColumnName)
			.DefaultLabel(FText::GetEmpty())
			.FixedWidth(25.f)
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::ErrorColumnName)
			.DefaultLabel(FText::GetEmpty())
			.FixedWidth(25.f)
			.HeaderContent()
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Error"))
				]
			]
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::ViewModelColumnName)
			.FillWidth(0.125f)
			.DefaultLabel(LOCTEXT("ViewModel", "View Model"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::ViewModelPropertyColumnName)
			.FillWidth(0.125f)
			.DefaultLabel(LOCTEXT("ViewModelProperty", "View Model Property"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::ModeColumnName)
			.FixedWidth(52)
			.DefaultLabel(LOCTEXT("Mode", "Mode"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::WidgetColumnName)
			.FillWidth(0.125f)
			.DefaultLabel(LOCTEXT("Widget", "Widget"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::WidgetPropertyColumnName)
			.FillWidth(0.125f)
			.DefaultLabel(LOCTEXT("Widget", "Widget Property"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::UpdateColumnName)
			.FillWidth(0.05f)
			.DefaultLabel(LOCTEXT("Update", "Update"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::ConversionFunctionColumnName)
			.FillWidth(0.05f)
			.DefaultLabel(LOCTEXT("Conversion", "Conversion"))
			+ SHeaderRow::Column(SMVVMViewBindingListEntryRow::DropDownOptionsColumnName)
			.FixedWidth(25.f)
			.DefaultLabel(FText::GetEmpty())
		)
	];
}


SMVVMViewBindingListView::~SMVVMViewBindingListView()
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		MVVMExtensionPtr->OnBlueprintViewChangedDelegate().RemoveAll(this);
		MVVMExtensionPtr->GetBlueprintView()->OnBindingsUpdated.RemoveAll(this);
		MVVMExtensionPtr->GetBlueprintView()->OnViewModelsUpdated.RemoveAll(this);
	}
}

void SMVVMViewBindingListView::RequestListRefresh()
{
	SourceData.Reset();
	if (const UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		if (const UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView())
		{
			const int32 Count = BlueprintView->GetBindings().Num();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				SourceData.Add(MakeShared<FMVVMViewBindingListEntry>(Index));
			}
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}


TSharedRef<ITableRow> SMVVMViewBindingListView::MakeSourceListViewWidget(FMVVMViewBindingListEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable) const
{
	if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
	{
		return SNew(SMVVMViewBindingListEntryRow, OwnerTable, Entry, MVVMExtensionPtr->GetBlueprintView(), MVVMExtensionPtr->GetWidgetBlueprint());
	}
	return SNew(SMVVMViewBindingListEntryRow, OwnerTable, Entry, nullptr, nullptr);
}

TSharedPtr<SWidget> SMVVMViewBindingListView::OnSourceConstructContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	TArray<FMVVMViewBindingListEntryPtr> Selection = ListView->GetSelectedItems();
	if (Selection.Num() > 0)
	{
		FUIAction RemoveAction;
		RemoveAction.ExecuteAction = FExecuteAction::CreateLambda([this, ToRemove = Selection[0]->Index]()
			{
				if (UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = MVVMExtension.Get())
				{
					if (TSharedPtr<SMVVMViewBindingPanel> BindingPanelPtr = BindingPanel.Pin())
					{
						BindingPanelPtr->OnBindingListSelectionChanged(INDEX_NONE);
					}

					UMVVMBlueprintView* BlueprintView = MVVMExtensionPtr->GetBlueprintView();
					BlueprintView->RemoveBindingAt(ToRemove);
				}
			});
		MenuBuilder.AddMenuEntry(LOCTEXT("RemoveBinding", "Remove Binding"), LOCTEXT("RemoveBindingTooltip", "Remove this binding."), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"), RemoveAction);
	}

	return MenuBuilder.MakeWidget();
}

void SMVVMViewBindingListView::OnSourceListSelectionChanged(FMVVMViewBindingListEntryPtr Entry, ESelectInfo::Type SelectionType) const
{
	if (bSelectionChangedGuard)
	{
		return;
	}
	TGuardValue<bool> ReentrantGuard(bSelectionChangedGuard, true);

	const int32 SelectionIndex = Entry ? Entry->Index : INDEX_NONE;
	if (TSharedPtr<SMVVMViewBindingPanel> BindingPanelPtr = BindingPanel.Pin())
	{
		BindingPanelPtr->OnBindingListSelectionChanged(SelectionIndex);
	}
}

#undef LOCTEXT_NAMESPACE
