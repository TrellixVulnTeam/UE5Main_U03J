// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMCompiledBindingLibraryCompiler.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Bindings/MVVMFieldPathHelper.h"
#include "Engine/Engine.h"
#include "MVVMSubsystem.h"
#include "Types/MVVMObjectVariant.h"
#include <limits>


namespace UE::MVVM::Private
{

static const FName NAME_BlueprintGetter = "BlueprintGetter";


/** */
struct FRawFieldId
{
	UClass* NotifyFieldValudChangedClass;
	UE::FieldNotification::FFieldId FieldId;

	int32 LoadedFieldIdIndex = INDEX_NONE;
	FCompiledBindingLibraryCompiler::FFieldIdHandle IdHandle;
	FMVVMVCompiledFieldId CompiledFieldId;
};


/** */
struct FRawField
{
	FMVVMConstFieldVariant Field;
	bool bPropertyIsObjectProperty = false; // either the FProperty or the return value of the UFunction
	bool bPropertyIsStructProperty = false;
	int32 LoadedPropertyOrFunctionIndex = INDEX_NONE;

public:
	bool IsSameField(const FRawField& Other) const
	{
		return Field == Other.Field;
	}
};


/** */
struct FRawFieldPath
{
	TArray<int32> RawFieldIndexes;
	bool bIsReadable = false;
	bool bIsWritable = false;

	FCompiledBindingLibraryCompiler::FFieldPathHandle PathHandle;
	FMVVMVCompiledFieldPath CompiledFieldPath;

public:
	bool IsSameFieldPath(const FRawFieldPath& Other) const
	{
		return Other.RawFieldIndexes == RawFieldIndexes;
	}
};


/** */
struct FRawBinding
{
	TArray<FCompiledBindingLibraryCompiler::FFieldPathHandle> SourcePathHandles;
	FCompiledBindingLibraryCompiler::FFieldPathHandle DestinationPathHandle;
	FCompiledBindingLibraryCompiler::FFieldPathHandle ConversionFunctionPathHandle;

	FCompiledBindingLibraryCompiler::FBindingHandle BindingHandle;
	FMVVMVCompiledBinding CompiledBinding;

public:
	bool IsSameBinding(const FRawBinding& Binding) const
	{
		return Binding.SourcePathHandles == SourcePathHandles
			&& Binding.DestinationPathHandle == DestinationPathHandle
			&& Binding.ConversionFunctionPathHandle == ConversionFunctionPathHandle;
	}
};

/** */
class FCompiledBindingLibraryCompilerImpl
{
public:
	TArray<FRawFieldId> FieldIds;
	TArray<FRawField> Fields;
	TArray<FRawFieldPath> FieldPaths;
	TArray<FRawBinding> Bindings;
	bool bCompiled = false;

public:
	int32 AddUniqueField(FMVVMConstFieldVariant InFieldVariant)
	{
		int32 FoundFieldPath = Fields.IndexOfByPredicate([InFieldVariant](const Private::FRawField& Other)
			{
				return Other.Field == InFieldVariant;
			});
		if (FoundFieldPath == INDEX_NONE)
		{
			FRawField RawField;
			RawField.Field = InFieldVariant;
			check(!InFieldVariant.IsEmpty());
			const FProperty* FieldProperty = InFieldVariant.IsProperty() ? InFieldVariant.GetProperty() : BindingHelper::GetReturnProperty(InFieldVariant.GetFunction());
			// FieldProperty can be null if it's a setter function
			RawField.bPropertyIsObjectProperty = CastField<FObjectPropertyBase>(FieldProperty) != nullptr;
			RawField.bPropertyIsStructProperty = CastField<FStructProperty>(FieldProperty) != nullptr;

			FoundFieldPath = Fields.Add(RawField);
		}
		return FoundFieldPath;
	}
};

} //namespace



namespace UE::MVVM
{


int32 FCompiledBindingLibraryCompiler::FBindingHandle::IdGenerator = 0;
int32 FCompiledBindingLibraryCompiler::FFieldPathHandle::IdGenerator = 0;
int32 FCompiledBindingLibraryCompiler::FFieldIdHandle::IdGenerator = 0;


/**
 *
 */
FCompiledBindingLibraryCompiler::FCompiledBindingLibraryCompiler()
	: Impl(MakePimpl<Private::FCompiledBindingLibraryCompilerImpl>())
{

}

TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FString> FCompiledBindingLibraryCompiler::AddFieldId(TSubclassOf<UObject> SourceClass, FName FieldId)
{
	Impl->bCompiled = false;

	if (!SourceClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
	{
		return MakeError(FString::Printf(TEXT("'%s' doesn't implement the NotifyFieldValueChanged interface.")
			, *SourceClass->GetName()));
	}

	const TScriptInterface<INotifyFieldValueChanged> ScriptObject = SourceClass->GetDefaultObject();
	if (ensure(ScriptObject.GetInterface()))
	{
		UE::FieldNotification::FFieldId FoundFieldId = ScriptObject->GetFieldNotificationDescriptor().GetField(SourceClass, FieldId);
		if (!FoundFieldId.IsValid())
		{
			return MakeError(FString::Printf(TEXT("The FieldNotify '%s' is not support by '%s'.")
				, *FieldId.ToString()
				, *SourceClass->GetName()));
		}

		int32 FoundFieldIdIndex = Impl->FieldIds.IndexOfByPredicate([FoundFieldId, SourceClass](const Private::FRawFieldId& Other)
			{
				return Other.FieldId == FoundFieldId && Other.NotifyFieldValudChangedClass == SourceClass.Get();
			});
		if (FoundFieldIdIndex == INDEX_NONE)
		{
			Private::FRawFieldId RawFieldId;
			RawFieldId.NotifyFieldValudChangedClass = SourceClass.Get();
			RawFieldId.FieldId = FoundFieldId;
			RawFieldId.IdHandle = FFieldIdHandle::MakeHandle();
			FoundFieldIdIndex = Impl->FieldIds.Add(MoveTemp(RawFieldId));
		}

		return MakeValue(Impl->FieldIds[FoundFieldIdIndex].IdHandle);
	}

	return MakeError(TEXT("Unexpected case with AddFieldId."));
}


TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FCompiledBindingLibraryCompiler::AddFieldPath(TSubclassOf<UObject> InSourceClass, FStringView InFieldPath, bool bInRead)
{
	Impl->bCompiled = false;

	TValueOrError<TArray<FMVVMFieldVariant>, FString> GeneratedField = FieldPathHelper::GenerateFieldPathList(InSourceClass, InFieldPath, bInRead);
	if (GeneratedField.HasError())
	{
		return MakeError(GeneratedField.StealError());
	}

	return AddFieldPath(MakeArrayView(GeneratedField.GetValue()), bInRead);
}


TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FCompiledBindingLibraryCompiler::AddFieldPath(TArrayView<FMVVMFieldVariant> InFieldPath, bool bInRead)
{
	Impl->bCompiled = false;

	auto ValidateContainer = [](const FProperty* Property, bool bShouldBeInsideContainer, bool bIsObjectOrScriptStruct) -> FString
	{	
		const UStruct* OwnerStruct = Property->GetOwnerStruct();
		if (OwnerStruct == nullptr)
		{
			return FString::Printf(TEXT("The field %s has an invalid owner struct."), *Property->GetName());
		}

		if (bShouldBeInsideContainer)
		{
			if (!Cast<UScriptStruct>(OwnerStruct) && !Cast<UClass>(OwnerStruct))
			{
				return FString::Printf(TEXT("The field %s doesn't have a valid owner for that path."), *Property->GetName());
			}
		}

		if (bIsObjectOrScriptStruct)
		{
			if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
			{
				return FString();
			}
			else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
			{
				if (StructProperty->HasGetter() || Property->HasMetaData(Private::NAME_BlueprintGetter))
				{
					return FString::Printf(TEXT("Property %s has getter accessor. Accessor not supported on FStructProperty since it would create a temporary structure and we would not able to return a valid container from that structure."), *StructProperty->GetName());
				}
				return FString();
			}
			return FString::Printf(TEXT("Field can only be object properties or struct properties. %s is a %s"), *Property->GetName(), *Property->GetClass()->GetName());
		}

		return FString();
	};

	TArray<int32> RawFieldIndexes;
	RawFieldIndexes.Reserve(InFieldPath.Num());

	for (int32 Index = 0; Index < InFieldPath.Num(); ++Index)
	{
		FMVVMConstFieldVariant FieldVariant = InFieldPath[Index];
		const bool bIsLast = Index == InFieldPath.Num() - 1;
		if (FieldVariant.IsProperty())
		{
			// They must all be readable except the last item if we are writing to the property.
			if (bIsLast && !bInRead)
			{
				if (!BindingHelper::IsValidForDestinationBinding(FieldVariant.GetProperty()))
				{
					return MakeError(FString::Printf(TEXT("Property %s is not writable at runtime."), *FieldVariant.GetProperty()->GetName()));
				}
			}
			else if (!BindingHelper::IsValidForSourceBinding(FieldVariant.GetProperty()))
			{
				return MakeError(FString::Printf(TEXT("Property %s is not readable at runtime."), *FieldVariant.GetProperty()->GetName()));
			}

			FString ValidatedStr = ValidateContainer(FieldVariant.GetProperty(), true, !bIsLast);
			if (!ValidatedStr.IsEmpty())
			{
				return MakeError(ValidatedStr);
			}

			RawFieldIndexes.Add(Impl->AddUniqueField(FieldVariant));
		}
		else if (FieldVariant.IsFunction())
		{
			if (bIsLast && !bInRead)
			{
				if (!BindingHelper::IsValidForDestinationBinding(FieldVariant.GetFunction()))
				{
					return MakeError(FString::Printf(TEXT("Function %s is not writable at runtime."), *FieldVariant.GetFunction()->GetName()));
				}
			}
			else if (!BindingHelper::IsValidForSourceBinding(FieldVariant.GetFunction()))
			{
				return MakeError(FString::Printf(TEXT("Function %s is not readable at runtime."), *FieldVariant.GetFunction()->GetName()));
			}

			if (bIsLast && !bInRead)
			{
				const FProperty* FirstProperty = BindingHelper::GetFirstArgumentProperty(FieldVariant.GetFunction());
				ValidateContainer(FirstProperty, false, bIsLast);
				RawFieldIndexes.Add(Impl->AddUniqueField(FieldVariant));
			}
			else
			{
				const FProperty* ReturnProperty = BindingHelper::GetReturnProperty(FieldVariant.GetFunction());
				ValidateContainer(ReturnProperty, false, bIsLast);
				RawFieldIndexes.Add(Impl->AddUniqueField(FieldVariant));
			}
		}
		else
		{
			return MakeError(TEXT("There is an invalid field in the field path."));
		}
	}

	int32 FoundFieldPath = Impl->FieldPaths.IndexOfByPredicate([&RawFieldIndexes](const Private::FRawFieldPath& Other)
		{
			return Other.RawFieldIndexes == RawFieldIndexes;
		});
	if (FoundFieldPath != INDEX_NONE)
	{
		Impl->FieldPaths[FoundFieldPath].bIsReadable = Impl->FieldPaths[FoundFieldPath].bIsReadable || bInRead;
		Impl->FieldPaths[FoundFieldPath].bIsWritable = Impl->FieldPaths[FoundFieldPath].bIsWritable || !bInRead;
		return MakeValue(Impl->FieldPaths[FoundFieldPath].PathHandle);
	}

	Private::FRawFieldPath RawFieldPath;
	RawFieldPath.RawFieldIndexes = RawFieldIndexes;
	RawFieldPath.PathHandle = FFieldPathHandle::MakeHandle();
	RawFieldPath.bIsReadable = bInRead;
	RawFieldPath.bIsWritable = !bInRead;
	FoundFieldPath = Impl->FieldPaths.Add(MoveTemp(RawFieldPath));

	return MakeValue(Impl->FieldPaths[FoundFieldPath].PathHandle);
}


TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FCompiledBindingLibraryCompiler::AddObjectFieldPath(TSubclassOf<UObject> InSourceClass, FStringView InFieldPath, UClass* ExpectedType, bool bInRead)
{
	Impl->bCompiled = false;

	check(ExpectedType);

	TValueOrError<TArray<FMVVMFieldVariant>, FString> GeneratedField = FieldPathHelper::GenerateFieldPathList(InSourceClass, InFieldPath, bInRead);
	if (GeneratedField.HasError())
	{
		return MakeError(GeneratedField.StealError());
	}
	if (GeneratedField.GetValue().Num() == 0)
	{
		return MakeError(FString::Printf(TEXT("The field does not returned a '%s'."), *ExpectedType->GetName()));
	}

	const FObjectPropertyBase* ObjectPropertyBase = nullptr;
	if (GeneratedField.GetValue().Last().IsProperty())
	{
		ObjectPropertyBase = CastField<const FObjectPropertyBase>(GeneratedField.GetValue().Last().GetProperty());
	}
	else if (GeneratedField.GetValue().Last().IsFunction())
	{
		ObjectPropertyBase = CastField<const FObjectPropertyBase>(BindingHelper::GetReturnProperty(GeneratedField.GetValue().Last().GetFunction()));
	}

	if (ObjectPropertyBase == nullptr)
	{
		return MakeError(FString::Printf(TEXT("The field does not returned a '%s'."), *ExpectedType->GetName()));
	}
	if (ObjectPropertyBase->PropertyClass == nullptr || !ExpectedType->IsChildOf(ObjectPropertyBase->PropertyClass))
	{
		return MakeError(FString::Printf(TEXT("The field does not returned a '%s'."), *ExpectedType->GetName()));
	}

	return AddFieldPath(MakeArrayView(GeneratedField.GetValue()), bInRead);
}


TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FCompiledBindingLibraryCompiler::AddConversionFunctionFieldPath(TSubclassOf<UObject> SourceClass, FStringView FieldPath)
{
	Impl->bCompiled = false;

	if (SourceClass == nullptr)
	{
		return MakeError(TEXT("The source class is invalid."));
	}
	if (FieldPath.Len() == 0)
	{
		return MakeError(TEXT("The function path is empty."));
	}

	FMVVMFieldVariant FoundFunction = FMVVMFieldVariant(FindObject<UFunction>(nullptr, FieldPath.GetData(), true));
	if (FoundFunction.IsEmpty() || !FoundFunction.IsFunction() || FoundFunction.GetFunction() == nullptr)
	{
		FoundFunction = UE::MVVM::BindingHelper::FindFieldByName(SourceClass.Get(), FMVVMBindingName(FieldPath.GetData()));
	}

	if (FoundFunction.IsEmpty() || !FoundFunction.IsFunction() || FoundFunction.GetFunction() == nullptr)
	{
		return MakeError(FString::Printf(TEXT("The function %s could not be found."), FieldPath.GetData()));
	}

	if (!BindingHelper::IsValidForRuntimeConversion(FoundFunction.GetFunction()))
	{
		return MakeError(FString::Printf(TEXT("Function %s cannot be used as a runtime conversion function."), *FoundFunction.GetFunction()->GetName()));
	}

	if (!FoundFunction.GetFunction()->HasAllFunctionFlags(FUNC_Static))
	{
		if (!SourceClass->IsChildOf(FoundFunction.GetFunction()->GetOuterUClass()))
		{
			return MakeError(FString::Printf(TEXT("Function %s is going to be executed with an invalid self."), *FoundFunction.GetFunction()->GetName()));
		}
	}

	TArray<int32> RawFieldIndexes;
	RawFieldIndexes.Add(Impl->AddUniqueField(FoundFunction));
	const int32 FoundFieldPath = Impl->FieldPaths.IndexOfByPredicate([&RawFieldIndexes](const Private::FRawFieldPath& Other)
		{
			return Other.RawFieldIndexes == RawFieldIndexes;
		});
	if (FoundFieldPath != INDEX_NONE)
	{
		return MakeValue(Impl->FieldPaths[FoundFieldPath].PathHandle);
	}

	Private::FRawFieldPath RawFieldPath;
	RawFieldPath.RawFieldIndexes = MoveTemp(RawFieldIndexes);
	RawFieldPath.PathHandle = FFieldPathHandle::MakeHandle();
	RawFieldPath.bIsReadable = false;
	RawFieldPath.bIsWritable = false;

	const int32 NewFieldPathIndex = Impl->FieldPaths.Add(MoveTemp(RawFieldPath));
	return MakeValue(Impl->FieldPaths[NewFieldPathIndex].PathHandle);
}


TValueOrError<FCompiledBindingLibraryCompiler::FBindingHandle, FString> FCompiledBindingLibraryCompiler::AddBinding(FFieldPathHandle InSourceHandle, FFieldPathHandle InDestinationHandle)
{
	return AddBinding(InSourceHandle, InDestinationHandle, FFieldPathHandle());
}


TValueOrError<FCompiledBindingLibraryCompiler::FBindingHandle, FString> FCompiledBindingLibraryCompiler::AddBinding(FFieldPathHandle InSourceHandle, FFieldPathHandle InDestinationHandle, FFieldPathHandle InConversionFunctionHandle)
{
	return AddBinding(MakeArrayView(&InSourceHandle, 1), InDestinationHandle, InConversionFunctionHandle);
}


TValueOrError<FCompiledBindingLibraryCompiler::FBindingHandle, FString> FCompiledBindingLibraryCompiler::AddBinding(TArrayView<FFieldPathHandle> InSourceHandles, FFieldPathHandle InDestinationHandle, FFieldPathHandle InConversionFunctionHandle)
{
	Impl->bCompiled = false;

	UMVVMSubsystem::FConstDirectionalBindingArgs DirectionBindingArgs;

	ensureMsgf(InSourceHandles.Num() == 1, TEXT("Conversion function with more than one arguements is not yet supported."));

	{
		FFieldPathHandle FirstSourceHandle = InSourceHandles[0];
		const int32 FoundSourceFieldPath = Impl->FieldPaths.IndexOfByPredicate([FirstSourceHandle](const Private::FRawFieldPath& Other)
			{
				return Other.PathHandle == FirstSourceHandle;
			});
		if (FoundSourceFieldPath == INDEX_NONE)
		{
			return MakeError(TEXT("The source handle is invalid."));
		}

		Private::FRawFieldPath& SourceRawFieldPath = Impl->FieldPaths[FoundSourceFieldPath];
		if (!SourceRawFieldPath.bIsReadable)
		{
			return MakeError(TEXT("The source handle was not constructed as a readable path."));
		}
		if (SourceRawFieldPath.RawFieldIndexes.Num() == 0)
		{
			return MakeError(TEXT("The source handle was not registered correctly."));
		}

		Private::FRawField& RawField = Impl->Fields[SourceRawFieldPath.RawFieldIndexes.Last()];
		if (RawField.Field.IsEmpty())
		{
			return MakeError(TEXT("The source handle was not registered correctly."));
		}

		DirectionBindingArgs.SourceBinding = RawField.Field;
	}

	{
		const int32 FoundDestinationFieldPath = Impl->FieldPaths.IndexOfByPredicate([InDestinationHandle](const Private::FRawFieldPath& Other)
			{
				return Other.PathHandle == InDestinationHandle;
			});
		if (FoundDestinationFieldPath == INDEX_NONE)
		{
			return MakeError(TEXT("The destination handle is invalid."));
		}

		Private::FRawFieldPath& DestinationRawFieldPath = Impl->FieldPaths[FoundDestinationFieldPath];
		if (!DestinationRawFieldPath.bIsWritable)
		{
			return MakeError(TEXT("The destination handle was not constructed as a writable path."));
		}
		if (DestinationRawFieldPath.RawFieldIndexes.Num() == 0)
		{
			return MakeError(TEXT("The destination handle was not registered correctly."));
		}

		Private::FRawField& RawField = Impl->Fields[DestinationRawFieldPath.RawFieldIndexes.Last()];
		if (RawField.Field.IsEmpty())
		{
			return MakeError(TEXT("The destination handle was not registered correctly."));
		}

		DirectionBindingArgs.DestinationBinding = RawField.Field;
	}

	if (InConversionFunctionHandle.IsValid())
	{
		const int32 FoundFunctionFieldPath = Impl->FieldPaths.IndexOfByPredicate([InConversionFunctionHandle](const Private::FRawFieldPath& Other)
			{
				return Other.PathHandle == InConversionFunctionHandle;
			});
		if (FoundFunctionFieldPath == INDEX_NONE)
		{
			return MakeError(TEXT("The function handle is invalid."));
		}

		Private::FRawFieldPath& ConversionFunctinRawFieldPath = Impl->FieldPaths[FoundFunctionFieldPath];
		if (ConversionFunctinRawFieldPath.RawFieldIndexes.Num() == 0)
		{
			return MakeError(TEXT("The function handle was not registered as a function."));
		}

		Private::FRawField& RawField = Impl->Fields[ConversionFunctinRawFieldPath.RawFieldIndexes.Last()];
		if (!RawField.Field.IsFunction())
		{
			return MakeError(TEXT("The function handle was not registered as a function."));
		}

		DirectionBindingArgs.ConversionFunction = RawField.Field.GetFunction();
	}

	TValueOrError<bool, FString> IsValidBinding = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->IsBindingValid(DirectionBindingArgs);
	if (IsValidBinding.HasError())
	{
		return MakeError(IsValidBinding.StealError());
	}

	Private::FRawBinding NewBinding;
	NewBinding.SourcePathHandles = InSourceHandles;
	NewBinding.DestinationPathHandle = InDestinationHandle;
	NewBinding.ConversionFunctionPathHandle = InConversionFunctionHandle;
	const int32 FoundIndex = Impl->Bindings.IndexOfByPredicate([&NewBinding](const Private::FRawBinding& Binding)
		{
			return NewBinding.IsSameBinding(Binding);
		});

	if (FoundIndex != INDEX_NONE)
	{
		return MakeValue(Impl->Bindings[FoundIndex].BindingHandle);
	}
	else
	{
		FCompiledBindingLibraryCompiler::FBindingHandle ResultBindingHandle = FBindingHandle::MakeHandle();
		NewBinding.BindingHandle = ResultBindingHandle;
		Impl->Bindings.Add(MoveTemp(NewBinding));
		return MakeValue(ResultBindingHandle);
	}
}


TValueOrError<FCompiledBindingLibraryCompiler::FCompileResult, FString> FCompiledBindingLibraryCompiler::Compile()
{
	Impl->bCompiled = false;

	struct FCompiledClassInfo
	{
		TArray<int32> RawFieldIndex;
		TArray<int32> RawFieldIdIndex;
	};

	// create the list of UClass
	TMap<const UStruct*, FCompiledClassInfo> MapOfFieldInClass;
	{
		for (int32 Index = 0; Index < Impl->Fields.Num(); ++Index)
		{
			const Private::FRawField& RawField = Impl->Fields[Index];
			check(!RawField.Field.IsEmpty());

			const UStruct* Owner = RawField.Field.IsProperty() ? RawField.Field.GetProperty()->GetOwnerStruct() : RawField.Field.GetFunction()->GetOwnerClass();
			check(Owner);
			FCompiledClassInfo& ClassInfo = MapOfFieldInClass.FindOrAdd(Owner);

			// Test if the Field is not there more than one
			{
				FMVVMConstFieldVariant FieldToTest = RawField.Field;
				const TArray<Private::FRawField>& ListOfFields = Impl->Fields;
				const bool bContains = ClassInfo.RawFieldIndex.ContainsByPredicate([FieldToTest, &ListOfFields](int32 OtherIndex)
					{
						return ListOfFields[OtherIndex].Field == FieldToTest;
					});
				check(!bContains);
			}

			ClassInfo.RawFieldIndex.Add(Index);
		}
	}
	{
		for (int32 Index = 0; Index < Impl->FieldIds.Num(); ++Index)
		{
			const Private::FRawFieldId& RawFieldId = Impl->FieldIds[Index];
			check(RawFieldId.FieldId.IsValid());
			check(RawFieldId.NotifyFieldValudChangedClass);

			FCompiledClassInfo& ClassInfo = MapOfFieldInClass.FindOrAdd(RawFieldId.NotifyFieldValudChangedClass);

			// Test if the Field is not there more than one
			{
				UE::FieldNotification::FFieldId FieldIdToTest = RawFieldId.FieldId;
				const TArray<Private::FRawFieldId>& ListOfFieldIds = Impl->FieldIds;
				const bool bContains = ClassInfo.RawFieldIdIndex.ContainsByPredicate([FieldIdToTest, &ListOfFieldIds](int32 OtherIndex)
					{
						return ListOfFieldIds[OtherIndex].FieldId == FieldIdToTest;
					});
				check(!bContains);
			}

			ClassInfo.RawFieldIdIndex.Add(Index);
		}
	}


	// Todo optimize that list to group common type. ie UWidget::ToolTip == UProgressBar::ToolTip. We can merge UWidget in UProgressBar.
	//The difficulty is with type that may be not loaded at runtime and would create runtime issue with type that would be loaded otherwise.

	FCompileResult Result;

	// Create FMVVMCompiledBindingLibrary::CompiledFields and FMVVMCompiledBindingLibrary::CompiledFieldNames
	int32 TotalNumberOfProperties = 0;
	int32 TotalNumberOfFunctions = 0;
	int32 TotalNumberOfFieldIds = 0;
	for (TPair<const UStruct*, FCompiledClassInfo>& StructCompiledFields : MapOfFieldInClass)
	{
		FMVVMVCompiledFields CompiledFields;
		CompiledFields.ClassOrScriptStruct = StructCompiledFields.Key;
		check(StructCompiledFields.Key);

		TArray<FName> PropertyNames;
		TArray<FName> FunctionNames;
		TArray<FName> FieldIdNames;
		for (const int32 FieldIndex : StructCompiledFields.Value.RawFieldIndex)
		{
			check(Impl->Fields.IsValidIndex(FieldIndex));
			Private::FRawField& RawField = Impl->Fields[FieldIndex];
			const FMVVMConstFieldVariant& Field = RawField.Field;

			if (Field.IsProperty())
			{
				Result.Library.LoadedProperties.Add(const_cast<FProperty*>(Field.GetProperty()));
				PropertyNames.Add(Field.GetName());
				RawField.LoadedPropertyOrFunctionIndex = TotalNumberOfProperties;
				++TotalNumberOfProperties;
			}
			else
			{
				check(Field.IsFunction());
				Result.Library.LoadedFunctions.Add(const_cast<UFunction*>(Field.GetFunction()));
				FunctionNames.Add(Field.GetName());
				RawField.LoadedPropertyOrFunctionIndex = TotalNumberOfFunctions;
				++TotalNumberOfFunctions;
			}
		}

		for (const int32 FieldIdIndex : StructCompiledFields.Value.RawFieldIdIndex)
		{
			check(Impl->FieldIds.IsValidIndex(FieldIdIndex));
			Private::FRawFieldId& RawFieldId = Impl->FieldIds[FieldIdIndex];
			
			Result.Library.LoadedFieldIds.Add(RawFieldId.FieldId);
			FieldIdNames.Add(RawFieldId.FieldId.GetName());
			RawFieldId.LoadedFieldIdIndex = TotalNumberOfFieldIds;
			++TotalNumberOfFieldIds;
		}

		if (PropertyNames.Num() > std::numeric_limits<FMVVMVCompiledBinding::IndexType>::max())
		{
			return MakeError(FString::Printf(TEXT("There are too many properties binded to struct '%s'"), *StructCompiledFields.Key->GetName()));
		}
		CompiledFields.NumberOfProperties = static_cast<int16>(PropertyNames.Num());

		if (FunctionNames.Num() > std::numeric_limits<FMVVMVCompiledBinding::IndexType>::max())
		{
			return MakeError(FString::Printf(TEXT("There are too many functions binded to struct '%s'"), *StructCompiledFields.Key->GetName()));
		}
		CompiledFields.NumberOfFunctions = static_cast<int16>(FunctionNames.Num());

		if (FieldIdNames.Num() > std::numeric_limits<FMVVMVCompiledBinding::IndexType>::max())
		{
			return MakeError(FString::Printf(TEXT("There are too many field ids binded to struct '%s'"), *StructCompiledFields.Key->GetName()));
		}
		CompiledFields.NumberOfFieldIds = static_cast<int16>(FieldIdNames.Num());

		int32 LibraryStartIndex = Result.Library.CompiledFieldNames.Num();
		if (LibraryStartIndex > std::numeric_limits<FMVVMVCompiledBinding::IndexType>::max())
		{
			return MakeError(FString::Printf(TEXT("There are too many properties and functions binded for the library")));
		}
		CompiledFields.LibraryStartIndex = static_cast<int16>(LibraryStartIndex);
			
		Result.Library.CompiledFieldNames.Append(PropertyNames);
		PropertyNames.Reset();
		Result.Library.CompiledFieldNames.Append(FunctionNames);
		FunctionNames.Reset();
		Result.Library.CompiledFieldNames.Append(FieldIdNames);
		FieldIdNames.Reset();
		if (Result.Library.CompiledFieldNames.Num() > std::numeric_limits<FMVVMVCompiledBinding::IndexType>::max())
		{
			return MakeError(FString::Printf(TEXT("There are too many properties binded for the library")));
		}

		Result.Library.CompiledFields.Add(CompiledFields);

		check(Result.Library.LoadedProperties.Num() + Result.Library.LoadedFunctions.Num() + Result.Library.LoadedFieldIds.Num()  == Result.Library.CompiledFieldNames.Num());
		check(Result.Library.LoadedProperties.Num() == TotalNumberOfProperties);
		check(Result.Library.LoadedFunctions.Num() == TotalNumberOfFunctions);
		check(Result.Library.LoadedFieldIds.Num() == TotalNumberOfFieldIds);
	}

	// Create FMVVMCompiledBindingLibrary::FieldPaths
	for (Private::FRawFieldPath& FieldPath : Impl->FieldPaths)
	{
		FieldPath.CompiledFieldPath.CompiledBindingLibraryId = Result.Library.CompiledBindingLibraryId;
		FieldPath.CompiledFieldPath.StartIndex = INDEX_NONE;
		FieldPath.CompiledFieldPath.Num = FieldPath.RawFieldIndexes.Num();
		if (FieldPath.RawFieldIndexes.Num())
		{
			FieldPath.CompiledFieldPath.StartIndex = Result.Library.FieldPaths.Num();
			for (const int32 RawFieldIndex : FieldPath.RawFieldIndexes)
			{
				const Private::FRawField& RawField = Impl->Fields[RawFieldIndex];
				check(!RawField.Field.IsEmpty());

				FMVVMCompiledLoadedPropertyOrFunctionIndex FieldIndex;
				FieldIndex.Index = RawField.LoadedPropertyOrFunctionIndex;
				FieldIndex.bIsObjectProperty = RawField.bPropertyIsObjectProperty;
				FieldIndex.bIsScriptStructProperty = RawField.bPropertyIsStructProperty;
				FieldIndex.bIsProperty = RawField.Field.IsProperty();
				Result.Library.FieldPaths.Add(FieldIndex);

				if (FieldIndex.bIsProperty)
				{
					check(Result.Library.LoadedProperties.IsValidIndex(FieldIndex.Index));
				}
				else
				{
					check(Result.Library.LoadedFunctions.IsValidIndex(FieldIndex.Index));
				}
			}
		}

		Result.FieldPaths.Add(FieldPath.PathHandle, FieldPath.CompiledFieldPath);
	}

	// Create FieldId
	for (Private::FRawFieldId& FieldId: Impl->FieldIds)
	{
		FieldId.CompiledFieldId.CompiledBindingLibraryId = Result.Library.CompiledBindingLibraryId;
		FieldId.CompiledFieldId.FieldIdIndex = FieldId.LoadedFieldIdIndex;

		Result.FieldIds.Add(FieldId.IdHandle, FieldId.CompiledFieldId);
	}

	auto GetCompiledFiledPath = [this](const FFieldPathHandle Handle)
	{
		const Private::FRawFieldPath* FoundBinding = Impl->FieldPaths.FindByPredicate([Handle](const Private::FRawFieldPath& Other)
			{
				return Other.PathHandle == Handle;
			});
		if (FoundBinding)
		{
			return FoundBinding->CompiledFieldPath;
		}
		return FMVVMVCompiledFieldPath();
	};


	// Create the requested FMVVMVCompiledBinding
	for (Private::FRawBinding& Binding : Impl->Bindings)
	{
		Binding.CompiledBinding.CompiledBindingLibraryId = Result.Library.CompiledBindingLibraryId;
		check(Binding.CompiledBinding.CompiledBindingLibraryId.IsValid());

		Binding.CompiledBinding.SourceFieldPath = GetCompiledFiledPath(Binding.SourcePathHandles[0]);
		check(Binding.CompiledBinding.SourceFieldPath.IsValid());

		Binding.CompiledBinding.DestinationFieldPath = GetCompiledFiledPath(Binding.DestinationPathHandle);
		check(Binding.CompiledBinding.DestinationFieldPath.IsValid());

		Binding.CompiledBinding.ConversionFunctionFieldPath = GetCompiledFiledPath(Binding.ConversionFunctionPathHandle);

		Result.Bindings.Add(Binding.BindingHandle, Binding.CompiledBinding);
	}

	Result.Library.LoadedProperties.Reset();
	Result.Library.LoadedFunctions.Reset();
	Result.Library.LoadedFieldIds.Reset();

	Impl->bCompiled = true;
	return MakeValue(MoveTemp(Result));
}

} //namespace
