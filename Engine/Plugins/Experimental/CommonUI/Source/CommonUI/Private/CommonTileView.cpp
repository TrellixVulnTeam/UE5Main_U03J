// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonTileView.h"
#include "CommonUIPrivatePCH.h"
#include "SCommonButtonTableRow.h"
#include "SCommonTileView.h"

UCommonTileView::UCommonTileView(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEnableScrollAnimation = true;
}

TSharedRef<STableViewBase> UCommonTileView::RebuildListWidget()
{
	return ConstructTileView<SCommonTileView>();
}

UUserWidget& UCommonTileView::OnGenerateEntryWidgetInternal(UObject* Item, TSubclassOf<UUserWidget> DesiredEntryClass, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (DesiredEntryClass->IsChildOf<UCommonButtonBase>())
	{
		return GenerateTypedEntry<UUserWidget, SCommonButtonTableRow<UObject*>>(DesiredEntryClass, OwnerTable);
	}
	return GenerateTypedEntry(DesiredEntryClass, OwnerTable);
}