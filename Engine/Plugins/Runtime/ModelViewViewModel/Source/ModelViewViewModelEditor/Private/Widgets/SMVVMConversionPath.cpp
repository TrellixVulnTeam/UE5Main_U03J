// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMConversionPath.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMPropertyPathHelpers.h"
#include "MVVMSubsystem.h"
#include "Styling/MVVMEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Types/MVVMFieldVariant.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "MVVMConversionPath"

void SMVVMConversionPath::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint, bool bInSourceToDestination)
{
	bSourceToDestination = bInSourceToDestination;
	WidgetBlueprint = InWidgetBlueprint;
	OnFunctionChanged = InArgs._OnFunctionChanged;
	Bindings = InArgs._Bindings;
	check(Bindings.IsSet());

	ChildSlot
	[
		SAssignNew(Anchor, SMenuAnchor)
		.ToolTipText(this, &SMVVMConversionPath::GetFunctionToolTip)
		.OnGetMenuContent(this, &SMVVMConversionPath::GetFunctionMenuContent)
		.Visibility(this, &SMVVMConversionPath::IsFunctionVisible)
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(3, 0, 3, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SMVVMConversionPath::OnButtonClicked)
				[
					SNew(SImage)
					.Image(FMVVMEditorStyle::Get().GetBrush(bSourceToDestination ? "ConversionFunction.SourceToDest" : "ConversionFunction.DestToSource"))
					.ColorAndOpacity(this, &SMVVMConversionPath::GetFunctionColor)
				]
			]
		]
	];
}

EVisibility SMVVMConversionPath::IsFunctionVisible() const
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return EVisibility::Hidden;
	}

	for (const FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		if (bSourceToDestination)
		{
			bool bShouldBeVisible = Binding->BindingType == EMVVMBindingMode::OneTimeToDestination ||
				Binding->BindingType == EMVVMBindingMode::OneWayToDestination ||
				Binding->BindingType == EMVVMBindingMode::TwoWay;

			if (bShouldBeVisible)
			{
				return EVisibility::Visible;
			}
		}
		else
		{
			bool bShouldBeVisible = Binding->BindingType == EMVVMBindingMode::OneTimeToSource ||
				Binding->BindingType == EMVVMBindingMode::OneWayToSource ||
				Binding->BindingType == EMVVMBindingMode::TwoWay;

			if (bShouldBeVisible)
			{
				return EVisibility::Visible;
			}
		}
	}
	
	return EVisibility::Hidden;
}

FString SMVVMConversionPath::GetFunctionPath() const
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());

	bool bFirst = true;
	FString FunctionPath;
	for (const FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		FString ThisPath = bSourceToDestination ? Binding->Conversion.SourceToDestinationFunctionPath : Binding->Conversion.DestinationToSourceFunctionPath;
		if (bFirst)
		{
			FunctionPath = ThisPath;
		}
		else if (FunctionPath != ThisPath)
		{
			return TEXT("Multiple Values");
		}
	}

	return FunctionPath;
}

FText SMVVMConversionPath::GetFunctionToolTip() const
{
	FString FunctionPath = GetFunctionPath();
	if (!FunctionPath.IsEmpty())
	{
		if (FunctionPath == TEXT("Multiple Values"))
		{
			return LOCTEXT("MultipleValues", "Multiple Values");
		}
		return FText::FromString(FunctionPath);
	}

	return bSourceToDestination ?
		LOCTEXT("AddSourceToDestinationFunction", "Add conversion function to be used when converting the source value to the destination value.") :
		LOCTEXT("AddDestinationToSourceFunction", "Add conversion function to be used when converting the destination value to the source value.");
}

FSlateColor SMVVMConversionPath::GetFunctionColor() const
{
	FString FunctionPath = GetFunctionPath();
	if (FunctionPath.IsEmpty())
	{
		return FStyleColors::Foreground;
	}

	return FStyleColors::AccentGreen;
}

FReply SMVVMConversionPath::OnButtonClicked() const
{
	Anchor->SetIsOpen(!Anchor->IsOpen());
	return FReply::Handled();
}

void SMVVMConversionPath::SetConversionFunction(const UFunction* Function)
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return;
	}

	for (FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		if (bSourceToDestination)
		{
			Binding->Conversion.SourceToDestinationFunctionPath = Function != nullptr ? Function->GetPathName() : FString();
		}
		else
		{
			Binding->Conversion.DestinationToSourceFunctionPath = Function != nullptr ? Function->GetPathName() : FString();
		}
	}

	if (OnFunctionChanged.IsBound())
	{
		OnFunctionChanged.Execute(Function != nullptr ? Function->GetPathName() : FString());
	}
}

TSharedRef<SWidget> SMVVMConversionPath::GetFunctionMenuContent()
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSet<const UFunction*> ConversionFunctions;

	for (FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		UE::MVVM::FViewModelFieldPathHelper ViewModelHelper(&Binding->ViewModelPath, WidgetBlueprint);
		UE::MVVM::FMVVMConstFieldVariant ViewModelField = ViewModelHelper.GetSelectedField();

		UE::MVVM::FWidgetFieldPathHelper WidgetHelper(&Binding->WidgetPath, WidgetBlueprint);
		UE::MVVM::FMVVMConstFieldVariant WidgetField = WidgetHelper.GetSelectedField();

		UMVVMSubsystem::FConstDirectionalBindingArgs Args;
		Args.SourceBinding = bSourceToDestination ? ViewModelField : WidgetField;
		Args.DestinationBinding = bSourceToDestination ? WidgetField : ViewModelField;

		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		TArray<const UFunction*> FunctionsForThis = EditorSubsystem->GetAvailableConversionFunctions(Args.SourceBinding, Args.DestinationBinding);

		if (ConversionFunctions.Num() > 0)
		{
			ConversionFunctions = ConversionFunctions.Intersect(TSet<const UFunction*>(FunctionsForThis));
		}
		else
		{
			ConversionFunctions = TSet<const UFunction*>(FunctionsForThis);
		}
	}

	FMenuBuilder MenuBuilder(true, TSharedPtr<const FUICommandList>());

	if (ConversionFunctions.Num() == 0)
	{
		MenuBuilder.AddWidget(
			SNew(SBox)
			.Padding(10,0)
			[
				SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "HintText")
					.Text(LOCTEXT("NoCompatibleFunctions", "No compatible functions found."))
			],
			FText::GetEmpty(),
			true, // no indent
			true // searchable
		);
	}

	for (const UFunction* Function : ConversionFunctions)
	{
		FUIAction Action(FExecuteAction::CreateSP(this, &SMVVMConversionPath::SetConversionFunction, Function));
		MenuBuilder.AddMenuEntry(
			Function->GetDisplayNameText(),
			Function->GetToolTipText(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.FunctionIcon"),
			Action);
	}

	const FString Path = GetFunctionPath();
	if (!Path.IsEmpty())
	{
		FUIAction ClearAction(FExecuteAction::CreateSP(this, &SMVVMConversionPath::SetConversionFunction, (const UFunction*) nullptr));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Clear", "Clear"),
			LOCTEXT("ClearToolTip", "Clear this conversion function."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
			ClearAction);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE