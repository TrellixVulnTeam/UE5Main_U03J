// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogGlobal.h"

#include "ConcertSyncSessionDatabase.h"
#include "HistoryEdition/HistoryAnalysis.h"
#include "RenameEditAndDeleteMapsFlow.h"
#include "ScopedSessionDatabase.h"

#include "Algo/AllOf.h"

#include "Core/Tests/Containers/TestUtils.h"

#include "HistoryEdition/DebugDependencyGraph.h"
#include "HistoryEdition/DependencyGraphBuilder.h"

#include "Misc/AutomationTest.h"

namespace UE::ConcertSyncTests
{
	bool ValidateRequirements(
		const FString& TestBaseName,
		FAutomationTestBase& Test,
		const RenameEditAndDeleteMapsFlowTest::TTestActivityArray<FActivityID> Activities,
		const ConcertSyncCore::FHistoryDeletionRequirements& ToValidate = {},
		const TSet<RenameEditAndDeleteMapsFlowTest::ETestActivity>& ExpectedHardDependencies = {},
		const TSet<RenameEditAndDeleteMapsFlowTest::ETestActivity>& ExpectedPossibleDependencies = {}
		)
	{
		using namespace RenameEditAndDeleteMapsFlowTest;

		const bool bHardDependenciesAreCorrect = Algo::AllOf(ExpectedHardDependencies, [&TestBaseName, &Test, &Activities, &ToValidate](ETestActivity Activity)
		{
			const bool bContained = ToValidate.HardDependencies.Contains(Activities[Activity]);
			Test.TestTrue(FString::Printf(TEXT("%s: %s is a hard dependency"), *TestBaseName, *LexToString(Activity)), bContained);
			return bContained;
		});
		Test.TestTrue(FString::Printf(TEXT("%s: Hard dependencies are correct"), *TestBaseName), bHardDependenciesAreCorrect);
		
		const bool bPossibleDependenciesAreCorrect = Algo::AllOf(ExpectedPossibleDependencies, [&TestBaseName, &Test, &Activities, &ToValidate](ETestActivity Activity)
		{
			const bool bContained = ToValidate.PossibleDependencies.Contains(Activities[Activity]);
			Test.TestTrue(FString::Printf(TEXT("%s: %s is a possible dependency"), *TestBaseName, *LexToString(Activity)), bContained);
			return bContained;
		});
		Test.TestTrue(FString::Printf(TEXT("%s: Possible dependencies are correct"), *TestBaseName), bPossibleDependenciesAreCorrect);
		
		const TSet<ETestActivity> ExpectedExcludedActivities = AllActivities().Difference(ExpectedHardDependencies.Union(ExpectedPossibleDependencies));
		const bool bAllOtherActivitiesExcluded = Algo::AllOf(ExpectedExcludedActivities, [&TestBaseName, &Test, &Activities, &ToValidate](ETestActivity Activity)
		{
			const bool bNotContained = !ToValidate.HardDependencies.Contains(Activities[Activity]) && !ToValidate.PossibleDependencies.Contains(Activities[Activity]);
			Test.TestTrue(FString::Printf(TEXT("%s: %s is no dependency"), *TestBaseName, *LexToString(Activity)), bNotContained);
			return bNotContained;
		});
		Test.TestTrue(FString::Printf(TEXT("%s: No unexpected dependencies"), *TestBaseName), bAllOtherActivitiesExcluded);

		return bHardDependenciesAreCorrect && bPossibleDependenciesAreCorrect && bAllOtherActivitiesExcluded;
	}
}

namespace UE::ConcertSyncTests::AnalysisTests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnalyseDeletionDependencies, "Concert.History.Analysis.AnalyseDeletionDependencies", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
	bool FAnalyseDeletionDependencies::RunTest(const FString& Parameters)
	{
		using namespace RenameEditAndDeleteMapsFlowTest;
		using namespace ConcertSyncCore;
		
		FScopedSessionDatabase SessionDatabase(*this);
		const TTestActivityArray<FActivityID> Activities = CreateActivityHistory(SessionDatabase, SessionDatabase.GetEndpoint());
		const FActivityDependencyGraph DependencyGraph = BuildDependencyGraphFrom(SessionDatabase);
		UE_LOG(LogConcert, Log, TEXT("%s tested graph in Graphviz format:\n\n%s"), *GetTestFullName(), *ConcertSyncCore::Graphviz::ExportToGraphviz(DependencyGraph, SessionDatabase));

		// Delete /Game/Foo > Nearly everything has hard dependency
		{
			const FHistoryDeletionRequirements DeleteFooRequirements = AnalyseActivityDeletion({ Activities[_1_NewPackageFoo] }, DependencyGraph);
			/* _1_NewPackageFoo: is what we're "deleting".
			 * _5_SavePackageBar: Bar is created as result of a rename but has no dependency to _1_NewPackageFoo.
			 * All other activities transitively depend on _1_NewPackageFoo (put above log into GraphViz to visualise).
			 * 
			 * Note: The transaction activities (_3_EditActor, _4_EditActor) have possible dependencies BUT they do have hard dependencies to _2_AddActor. This is why they must be in HardDependencies, too.
			 */
			const TSet<ETestActivity> ExcludedActivities{ _1_NewPackageFoo, _5_SavePackageBar };
			const TSet<ETestActivity> HardDependencies = AllActivities().Difference(ExcludedActivities);
			const bool bDeleteAllCorrect = ValidateRequirements(TEXT("Delete /Game/Foo"), *this, Activities, DeleteFooRequirements, HardDependencies);
			TestTrue(TEXT("Delete /Game/Foo is correct"), bDeleteAllCorrect);
		}

		// Delete rename transaction > No dependencies
		{
			const FHistoryDeletionRequirements DeleteRenameRequirements = AnalyseActivityDeletion({ Activities[_3_RenameActor] }, DependencyGraph);
			TestEqual(TEXT("Delete renaming actor: HardDependencies.Num() == 0"), DeleteRenameRequirements.HardDependencies.Num(), 0);
			TestEqual(TEXT("Delete renaming actor: PossibleDependencies.Num() == 1"), DeleteRenameRequirements.PossibleDependencies.Num(), 1);
			TestTrue(TEXT("Delete renaming actor: Edit activity may depend on deleted activity"), DeleteRenameRequirements.PossibleDependencies.Contains(Activities[_4_EditActor]));
		}

		// Delete actor creation > All transactions operating on actor are hard dependencies
		{
			const FHistoryDeletionRequirements DeleteCreateActorRequirements = AnalyseActivityDeletion({ Activities[_2_AddActor] }, DependencyGraph);
			TestEqual(TEXT("Delete actor creation: HardDependencies.Num() == 2"), DeleteCreateActorRequirements.HardDependencies.Num(), 2);
			TestEqual(TEXT("Delete actor creation: PossibleDependencies.Num() == 0"), DeleteCreateActorRequirements.PossibleDependencies.Num(), 0);
			TestTrue(TEXT("Delete actor creation: Rename depends on created actor"), DeleteCreateActorRequirements.HardDependencies.Contains(Activities[_3_RenameActor]));
			TestTrue(TEXT("Delete actor creation: Edit depends on created actor"), DeleteCreateActorRequirements.HardDependencies.Contains(Activities[_4_EditActor]));
		}

		return true;
	}

	/**
	 * Suppose:
	 *
	 *		R
	 *	   / \
	 *	  A   B
	 *	   \ /
	 *	    L
	 *
	 * The edges L -> A -> R are possible dependencies.
	 * The edges L -> B -> R are hard dependencies.
	 *
	 * The test: delete R.
	 * We want L to be marked has a hard dependency.
	 */
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPossibleDependencyOrderedBeforeHardDependency, "Concert.History.Analysis.PossibleDependencyOrderedBeforeHardDependency", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
	bool FPossibleDependencyOrderedBeforeHardDependency::RunTest(const FString& Parameters)
	{
		using namespace ConcertSyncCore;

		constexpr FActivityID RootActivityID = 1;
		constexpr FActivityID AActivityID = 2;
		constexpr FActivityID BActivityID = 3;
		constexpr FActivityID LeafActivityID  = 4;
		
		FActivityDependencyGraph DependencyGraph;
		const FActivityNodeID RootNodeID = DependencyGraph.AddActivity(RootActivityID);
		const FActivityNodeID ANodeID = DependencyGraph.AddActivity(AActivityID);
		const FActivityNodeID BNodeID = DependencyGraph.AddActivity(BActivityID);
		const FActivityNodeID LeafNodeID = DependencyGraph.AddActivity(LeafActivityID);

		// Add the weak dependency first so the algorithm finds it first when iterating
		DependencyGraph.AddDependency(LeafNodeID, FActivityDependencyEdge(ANodeID, EActivityDependencyReason::EditAfterPreviousPackageEdit, EDependencyStrength::PossibleDependency));
		DependencyGraph.AddDependency(ANodeID, FActivityDependencyEdge(RootNodeID, EActivityDependencyReason::EditAfterPreviousPackageEdit, EDependencyStrength::PossibleDependency));
		DependencyGraph.AddDependency(LeafNodeID, FActivityDependencyEdge(BNodeID, EActivityDependencyReason::EditAfterPreviousPackageEdit, EDependencyStrength::HardDependency));
		DependencyGraph.AddDependency(BNodeID, FActivityDependencyEdge(RootNodeID, EActivityDependencyReason::EditAfterPreviousPackageEdit, EDependencyStrength::HardDependency));

		const FHistoryDeletionRequirements DeleteFooRequirements = AnalyseActivityDeletion({ RootActivityID }, DependencyGraph);

		TestEqual(TEXT("HardDependencies.Num() == 2"), DeleteFooRequirements.HardDependencies.Num(), 2);
		TestTrue(TEXT("HardDependencies.Contains(B)"), DeleteFooRequirements.HardDependencies.Contains(BActivityID));
		TestTrue(TEXT("HardDependencies.Contains(L)"), DeleteFooRequirements.HardDependencies.Contains(LeafActivityID));
		
		TestEqual(TEXT("PossibleDependencies.Num() == 1"), DeleteFooRequirements.PossibleDependencies.Num(), 1);
		TestTrue(TEXT("PossibleDependencies.Contains(A)"), DeleteFooRequirements.PossibleDependencies.Contains(AActivityID));

		return true;
	}
}