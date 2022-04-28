// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMFunction.h"
#include "UObject/Object.h"
#include "RigVMTypeUtils.h"

/**
 * The template argument represents a single parameter
 * in a function call and all of its possible types
 */
struct RIGVM_API FRigVMTemplateArgument
{
	struct FType
	{
		FString CPPType;
		UObject* CPPTypeObject;

		FType()
			: CPPType()
			, CPPTypeObject(nullptr)
		{
			CPPType = RigVMTypeUtils::GetWildCardCPPType();
			CPPTypeObject = RigVMTypeUtils::GetWildCardCPPTypeObject();
		}

		FType(const FString& InCPPType, UObject* InCPPTypeObject = nullptr)
			: CPPType(InCPPType)
			, CPPTypeObject(InCPPTypeObject)
		{
			check(!CPPType.IsEmpty());
		}

		static FType Array()
		{
			return FType(RigVMTypeUtils::GetWildCardArrayCPPType(), RigVMTypeUtils::GetWildCardCPPTypeObject());
		}

		bool operator == (const FType& InOther) const
		{
			return CPPType == InOther.CPPType;
		}

		bool operator != (const FType& InOther) const
		{
			return CPPType != InOther.CPPType;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FType& InType)
		{
			return GetTypeHash(InType.CPPType);
		}

		FORCEINLINE bool Matches(const FString& InCPPType, bool bAllowFloatingPointCasts = true) const
		{
			if(CPPType == InCPPType)
			{
				return true;
			}
			if(bAllowFloatingPointCasts)
			{
				if(InCPPType == RigVMTypeUtils::FloatType && CPPType == RigVMTypeUtils::DoubleType)
				{
					return true;
				}
				if(InCPPType == RigVMTypeUtils::DoubleType && CPPType == RigVMTypeUtils::FloatType)
				{
					return true;
				}
				if(InCPPType == RigVMTypeUtils::FloatArrayType && CPPType == RigVMTypeUtils::DoubleArrayType)
				{
					return true;
				}
				if(InCPPType == RigVMTypeUtils::DoubleArrayType && CPPType == RigVMTypeUtils::FloatArrayType)
				{
					return true;
				}
			}
			return false;
		}

		FName GetCPPTypeObjectPath() const
		{
			if(CPPTypeObject)
			{
				return *CPPTypeObject->GetPathName();
			}
			return NAME_None;
		}

		bool IsWildCard() const
		{
			return CPPTypeObject == RigVMTypeUtils::GetWildCardCPPTypeObject() ||
				CPPType == RigVMTypeUtils::GetWildCardCPPType() ||
				CPPType == RigVMTypeUtils::GetWildCardArrayCPPType();
		}

		bool IsArray() const
		{
			return RigVMTypeUtils::IsArrayType(CPPType);
		}

		FString GetBaseCPPType() const
		{
			if(IsArray())
			{
				return RigVMTypeUtils::BaseTypeFromArrayType(CPPType);
			}
			return CPPType;
		}
	};
	
	enum EArrayType
	{
		EArrayType_SingleValue,
		EArrayType_ArrayValue,
		EArrayType_Mixed,
		EArrayType_Invalid
	};

	enum ETypeCategory
	{
		ETypeCategory_SingleAnyValue,
		ETypeCategory_ArrayAnyValue,
		ETypeCategory_SingleSimpleValue,
		ETypeCategory_ArraySimpleValue,
		ETypeCategory_SingleMathStructValue,
		ETypeCategory_ArrayMathStructValue,
		ETypeCategory_SingleScriptStructValue,
		ETypeCategory_ArrayScriptStructValue,
		ETypeCategory_SingleEnumValue,
		ETypeCategory_ArrayEnumValue,
		ETypeCategory_SingleObjectValue,
		ETypeCategory_ArrayObjectValue,
		ETypeCategory_Invalid
	};

	FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, const FType& InType);
	FRigVMTemplateArgument(const FName& InName, ERigVMPinDirection InDirection, const TArray<FType>& InTypes);

	// returns the name of the argument
	const FName& GetName() const { return Name; }

	// returns the direction of the argument
	ERigVMPinDirection GetDirection() const { return Direction; }

	// returns true if this argument supports a given type across a set of permutations
	bool SupportsType(const FString& InCPPType, const TArray<int32>& InPermutationIndices = TArray<int32>(), FType* OutType = nullptr) const;

	// returns the flat list of types (including duplicates) of this argument
	const TArray<FType>& GetTypes() const;

	// returns an array of all of the supported types
	TArray<FType> GetSupportedTypes(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// returns an array of all supported types as strings
	TArray<FString> GetSupportedTypeStrings(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// returns true if an argument is singleton (same type for all variants)
	bool IsSingleton(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// returns true if the argument uses an array container
	EArrayType GetArrayType() const;
	
	// Returns all compatible types given a category
	static const TArray<FType> GetCompatibleTypes(ETypeCategory InCategory);

protected:

	int32 Index;
	FName Name;
	ERigVMPinDirection Direction;
	bool bSingleton;
	TArray<FType> Types;

	// default constructor
	FRigVMTemplateArgument();

	// constructor from a property
	FRigVMTemplateArgument(FProperty* InProperty);

	friend struct FRigVMTemplate;
	friend class URigVMController;
	friend struct FRigVMRegistry;
};

/**
 * The template is used to group multiple rigvm functions
 * that share the same notation. Templates can then be used
 * to build polymorphic nodes (RigVMTemplateNode) that can
 * take on any of the permutations supported by the template.
 */
struct RIGVM_API FRigVMTemplate
{
public:

	typedef TMap<FName, FRigVMTemplateArgument::FType> FTypeMap;
	typedef TPair<FName, FRigVMTemplateArgument::FType> FTypePair;

	// returns true if this is a valid template
	bool IsValid() const;

	// Returns the notation of this template
	const FName& GetNotation() const;

	// Returns the name of the template
	FName GetName() const;

	// returns true if this template is compatible with another one
	bool IsCompatible(const FRigVMTemplate& InOther) const;

	// returns true if this template can merge another one
	bool Merge(const FRigVMTemplate& InOther);

	// returns the number of args of this template
	int32 NumArguments() const { return Arguments.Num(); }

	// returns an argument for a given index
	const FRigVMTemplateArgument* GetArgument(int32 InIndex) const { return &Arguments[InIndex]; }

		// returns an argument given a name (or nullptr)
	const FRigVMTemplateArgument* FindArgument(const FName& InArgumentName) const;

	// returns true if a given arg supports a type
	bool ArgumentSupportsType(const FName& InArgumentName, const FString& InCPPType, const FTypeMap& InTypes = FTypeMap(), FRigVMTemplateArgument::FType* OutType = nullptr) const;

	// returns the number of permutations supported by this template
	int32 NumPermutations() const { return Permutations.Num(); }

	// returns a permutation given an index
	const FRigVMFunction* GetPermutation(int32 InIndex) const;

	// returns true if a given function is a permutation of this template
	bool ContainsPermutation(const FRigVMFunction* InPermutation) const;

	// returns the index of the permutation within the template of a given function (or INDEX_NONE)
	int32 FindPermutation(const FRigVMFunction* InPermutation) const;

	// returns true if the template was able to resolve to single permutation
	bool FullyResolve(FTypeMap& InOutTypes, int32& OutPermutationIndex) const;

	// returns true if the template was able to resolve to at least one permutation
	bool Resolve(FTypeMap& InOutTypes, TArray<int32> & OutPermutationIndices, bool bAllowFloatingPointCasts) const;

	// returns true if the template can resolve an argument to a new type
	bool ResolveArgument(const FName& InArgumentName, const FRigVMTemplateArgument::FType& InType, FTypeMap& InOutTypes) const;

	// returns true if a given argument is valid for a template
	static bool IsValidArgumentForTemplate(const FRigVMTemplateArgument& InArgument);

	// returns the prefix for an argument in the notation
	static const FString& GetArgumentNotationPrefix(const FRigVMTemplateArgument& InArgument);

	// returns the notation of an argument
	static FString GetArgumentNotation(const FRigVMTemplateArgument& InArgument);

#if WITH_EDITOR

	// Returns the color based on the permutation's metadata
	FLinearColor GetColor(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// Returns the tooltip based on the permutation's metadata
	FText GetTooltipText(const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	// Returns the display name text for an argument 
	FText GetDisplayNameForArgument(const FName& InArgumentName, const TArray<int32>& InPermutationIndices = TArray<int32>()) const;

	FString GetCategory() const;
	FString GetKeywords() const;

#endif

private:

	// Default constructor
	FRigVMTemplate();

	// Constructor from a struct, a template name and a function index
	FRigVMTemplate(UScriptStruct* InStruct, const FString& InTemplateName, int32 InFunctionIndex);

	// Constructor from a template name, arguments and a function index
	FRigVMTemplate(const FName& InTemplateName, const TArray<FRigVMTemplateArgument>& InArguments, int32 InFunctionIndex);

	int32 Index;
	FName Notation;
	TArray<FRigVMTemplateArgument> Arguments;
	TArray<int32> Permutations;

	friend struct FRigVMRegistry;
};

