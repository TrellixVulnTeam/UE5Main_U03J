﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SessionTabs/ConcertSessionTabBase.h"
#include "Types/SlateAttribute.h"

class FConcertSessionPackageViewerController;
class FLiveServerSessionHistoryController;
class IConcertSyncServer;
class IConcertServerSession;
class SDockTab;
class SWindow;

/**
 * Manages the tab that contains the UI for a session.
 * It has access to the controllers and views needed for displaying a session.
 */
class FLiveConcertSessionTab : public FConcertSessionTabBase
{
public:

	FLiveConcertSessionTab(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer, TAttribute<TSharedRef<SWindow>> ConstructUnderWindow);

protected:

	//~ Begin FAbstractConcertSessionTab Interface
	virtual FGuid GetSessionID() const override;
	virtual void CreateDockContent(const TSharedRef<SDockTab>& InDockTab) override;
	virtual void OnOpenTab() override;
	//~ End FAbstractConcertSessionTab Interface
	
private:
	
	/** The session being inspected */
	const TSharedRef<IConcertServerSession> InspectedSession;

	/** Used to determine owning window during construction */
	TAttribute<TSharedRef<SWindow>> ConstructUnderWindow;

	/** Manages the session history widget */
	const TSharedRef<FLiveServerSessionHistoryController> SessionHistoryController;
	/** Manages the package viewer widget */
	const TSharedRef<FConcertSessionPackageViewerController> PackageViewerController;
};
