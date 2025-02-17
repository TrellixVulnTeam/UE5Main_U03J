﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWebAPISchemaPropertyRow.h"

#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

void SWebAPISchemaPropertyRow::Construct(const FArguments& InArgs, const TSharedRef<FWebAPIPropertyViewModel>& InViewModel, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	const FSlateBrush* ArrayIcon = FEditorStyle::GetBrush(TEXT("Graph.ArrayPin.Connected"));
	const FSlateBrush* Icon = FEditorStyle::GetBrush(TEXT("Graph.Pin.Connected"));
	const FSlateColor TypeColor = InViewModel->GetPinColor();
	
	SWebAPISchemaTreeTableRow::Construct(
	SWebAPISchemaTreeTableRow::FArguments()
	.Content()
	[
		SNew(SBorder)
		.ToolTipText(InViewModel->GetTooltip())
		.BorderBackgroundColor(FLinearColor(0,0,0,0))		
		.Padding(4)
		[
			SNew(SBox)
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.Padding(0)
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(20)
					.HeightOverride(20)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image_Lambda([&, ArrayIcon, Icon]
						{
							return ViewModel->IsArray()
									? ArrayIcon
									: Icon;
						})
						.ColorAndOpacity(TypeColor)
					]
				]
				
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.Padding(0)
				[
					SNew(SBox)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(9, 0, 0, 1)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "PlacementBrowser.Asset.Name")
							.Text(InViewModel->GetLabel())
						]
					]
				]
			]
		]
	],
	InViewModel,
	InOwnerTableView);
}
