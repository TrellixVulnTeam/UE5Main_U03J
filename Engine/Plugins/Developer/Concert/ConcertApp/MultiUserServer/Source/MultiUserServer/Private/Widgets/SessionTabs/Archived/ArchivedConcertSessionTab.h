﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SessionTabs/ConcertSessionTabBase.h"
#include "Types/SlateAttribute.h"

struct FConcertSessionActivity;
class FArchivedSessionHistoryController;
class SConcertArchivedSessionInspector;

/** Manages the tab for an archived session.  */
class FArchivedConcertSessionTab : public FConcertSessionTabBase
{
public:

	FArchivedConcertSessionTab(FGuid InspectedSessionID, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow);

protected:

	//~ Begin FAbstractConcertSessionTab Interface
	virtual FGuid GetSessionID() const override;
	virtual void CreateDockContent(const TSharedRef<SDockTab>& InDockTab) override;
	virtual void OnOpenTab() override {}
	//~ End FAbstractConcertSessionTab Interface
	
private:

	/** The inspected session's ID */
	const FGuid InspectedSessionID;

	/** Used later to construct Inspector */
	const TSharedRef<IConcertSyncServer> SyncServer;
	
	/** Used later to obtain the window into which to add the tab */
	const TAttribute<TSharedRef<SWindow>> ConstructUnderWindow;
	
	TSharedPtr<FArchivedSessionHistoryController> HistoryController;
	
	/** Displays session */
	TSharedPtr<SConcertArchivedSessionInspector> Inspector;

	void OnRequestDeleteActivity(const TSharedRef<FConcertSessionActivity>& DeleteActivity) const;
	bool CanDeleteActivity(const TSharedRef<FConcertSessionActivity>& DeleteActivity) const;
};
