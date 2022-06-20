// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertHeaderRowUtils.h"

#include "Algo/AllOf.h"
#include "Algo/Find.h"

#include "Dom/JsonObject.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Serialization/BufferArchive.h"
#include "Serialization/JsonSerializer.h"

#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "ConcertHeaderRowUtils"

namespace UE::ConcertSharedSlate
{
	TSharedRef<SWidget> MakeHideColumnContextMenu(const TSharedRef<SHeaderRow>& HeaderRow, const FName ForColumnID)
	{
		constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);
		AddHideColumnEntry(HeaderRow, ForColumnID, MenuBuilder);
		return MenuBuilder.MakeWidget();
	}

	void AddHideColumnEntry(const TSharedRef<SHeaderRow>& HeaderRow, const FName ForColumnID, FMenuBuilder& MenuBuilder)
	{
		const SHeaderRow::FColumn* FoundColumn = Algo::FindByPredicate(HeaderRow->GetColumns(), [ForColumnID](const SHeaderRow::FColumn& Column){ return  Column.ColumnId == ForColumnID; });
		check(FoundColumn);

		TWeakPtr<SHeaderRow> WeakHeaderRow = HeaderRow;
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HideColumn", "Hide column"),
			FText::Format(LOCTEXT("HideColumn_Tooltip", "Hides the {0} column. You can unhide it using the eye-icon."), FoundColumn->DefaultText.Get()),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakHeaderRow, ForColumnID](){ WeakHeaderRow.Pin()->SetShowGeneratedColumn(ForColumnID, !WeakHeaderRow.Pin()->IsColumnVisible(ForColumnID)); }),
				FCanExecuteAction::CreateLambda([] { return true; })),
			NAME_None,
			EUserInterfaceActionType::Button
			);
	}

	void AddEntriesForShowingHiddenRows(const TSharedRef<SHeaderRow>& HeaderRow, FMenuBuilder& MenuBuilder)
	{
		for (int32 ColumnIndex = 0; ColumnIndex < HeaderRow->GetColumns().Num(); ++ColumnIndex)
		{
			const SHeaderRow::FColumn& Column = HeaderRow->GetColumns()[ColumnIndex];
			TWeakPtr<SHeaderRow> WeakHeaderRow = HeaderRow;
			if (!Column.ShouldGenerateWidget.IsSet())
			{
				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("UnhideColumn", "Show \"{0}\" column"), Column.DefaultText.Get()),
					FText::Format(LOCTEXT("UnhideColumn_Tooltip", "Show the {0} column."), Column.DefaultText.Get()),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([WeakHeaderRow, ColumnID = Column.ColumnId, ColumnIndex](){ WeakHeaderRow.Pin()->SetShowGeneratedColumn(ColumnID, !WeakHeaderRow.Pin()->GetColumns()[ColumnIndex].bIsVisible); }),
						FCanExecuteAction::CreateLambda([] { return true; }),
						FIsActionChecked::CreateLambda([WeakHeaderRow, ColumnIndex] { return WeakHeaderRow.Pin()->GetColumns()[ColumnIndex].bIsVisible; })
						),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}
		}
	}

	static const FString JsonField_ColumnId("ColumnID");
	static const FString JsonField_IsVisible("bIsVisible");
	static const FString JsonField_Values("Values");

	FColumnVisibilitySnapshot SnapshotColumnVisibilityState(const TSharedRef<SHeaderRow>& HeaderRow)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> Array;
		for (const SHeaderRow::FColumn& Column : HeaderRow->GetColumns())
		{
			TSharedPtr<FJsonObject> ColumnObject = MakeShared<FJsonObject>();
			ColumnObject->SetStringField(JsonField_ColumnId, Column.ColumnId.ToString());
			ColumnObject->SetBoolField(JsonField_IsVisible, Column.bIsVisible);
			Array.Add(MakeShared<FJsonValueObject>(ColumnObject));
		}
		JsonObject->SetArrayField(JsonField_Values, Array);

		FString Snapshot;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Snapshot);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
		return { Snapshot };
	}

	void RestoreColumnVisibilityState(const TSharedRef<SHeaderRow>& HeaderRow, const FColumnVisibilitySnapshot& Snapshot)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Snapshot.Snapshot);
		const bool bIsValid = FJsonSerializer::Deserialize(Reader, JsonObject);
		// This is a legal state, e.g. trying to restore a column view for the first time when no data is yet available
		if (!bIsValid)
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> ColumnObjects = JsonObject->GetArrayField(JsonField_Values);
		for (const TSharedPtr<FJsonValue>& ColumnValue : ColumnObjects)
		{
			TSharedPtr<FJsonObject>* PossibleColumnObject;
			if (!ColumnValue->TryGetObject(PossibleColumnObject))
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& ColumnObject = *PossibleColumnObject;
			
			FString ColumnId;
			bool bIsVisible;
			if (!ColumnObject->TryGetStringField(JsonField_ColumnId, ColumnId) || !ColumnObject->TryGetBoolField(JsonField_IsVisible, bIsVisible))
			{
				continue;
			}

			HeaderRow->SetShowGeneratedColumn(*ColumnId, bIsVisible);
		}
	}
}

#undef LOCTEXT_NAMESPACE