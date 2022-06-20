﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/Browser/SConcertSessionBrowser.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMessageDialog;
class FConcertServerSessionBrowserController;
class SSearchBox;

/** Shows a list of server sessions */
class SConcertServerSessionBrowser : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SConcertServerSessionBrowser) { }
		SLATE_EVENT(FSessionDelegate, DoubleClickLiveSession)
		SLATE_EVENT(FSessionDelegate, DoubleClickArchivedSession)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FConcertServerSessionBrowserController> InController);

	void RequestRefreshListNextTick() { bRequestedRefresh = true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (bRequestedRefresh)
		{
			SessionBrowser->RefreshSessionList();
			bRequestedRefresh = false;
		}

		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
	
private:

	/** We can ask the controller about information and notify it about UI events. */
	TWeakPtr<FConcertServerSessionBrowserController> Controller;

	bool bRequestedRefresh = false;

	TSharedPtr<FText> SearchText;
	TSharedPtr<SConcertSessionBrowser> SessionBrowser;
	
	TSharedRef<SWidget> MakeSessionTableView(const FArguments& InArgs);

	void RequestDeleteSession(const TSharedPtr<FConcertSessionItem>& SessionItem);
	void DeleteArchivedSessionWithFakeModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem);
	void DeleteActiveSessionWithFakeModalQuestion(const TSharedPtr<FConcertSessionItem>& SessionItem);
};
