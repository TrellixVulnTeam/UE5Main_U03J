﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstancedStruct.h"
#include "StructView.h"
#include "Templates/ValueOrError.h"
#include "PropertyBag.generated.h"

/** Property bag property type, loosely based on BluePrint pin types. */
UENUM()
enum class EPropertyBagPropertyType : uint8
{
	None UMETA(Hidden),
	Bool,
	Byte,
	Int32,
	Int64,
	Float,
	Double,
	Name,
	String,
	Text,
	Enum UMETA(Hidden),
	Struct UMETA(Hidden),
	Object UMETA(Hidden),
	SoftObject UMETA(Hidden),
	Class UMETA(Hidden),
	SoftClass UMETA(Hidden),
};

/** Getter and setter result code. */
UENUM()
enum class EPropertyBagResult : uint8
{
	Success,			// Operation succeeded.
	TypeMismatch,		// Tried to access mismatching type (e.g. setting a struct to bool)
	PropertyNotFound,	// Could not find property of specified name.
};

/** Describes a property in the property bag. */
USTRUCT()
struct STRUCTUTILS_API FPropertyBagPropertyDesc
{
	GENERATED_BODY()

	FPropertyBagPropertyDesc() = default;
	FPropertyBagPropertyDesc(const FName InName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr)
		: ValueTypeObject(InValueTypeObject)
		, Name(InName)
		, ValueType(InValueType)
	{
	}

	/** @return true if the two descriptors have the same type. Object types are compatible if Other can be cast to this type. */
	bool CompatibleType(const FPropertyBagPropertyDesc& Other) const;

	/** @return true if the property type is numeric (bool, int32, int64, float, double, enum) */
	bool IsNumericType() const;
	
	/** @return true if the property type is floating point numeric (float, double) */
	bool IsNumericFloatType() const;

	/** @return true if the property type is object or soft object */
	bool IsObjectType() const;

	/** @return true if the property type is class or soft class */
	bool IsClassType() const;

	
	/** Pointer to object that defines the Enum, Struct, or Class. */
	UPROPERTY(EditAnywhere, Category="Default")
	TObjectPtr<const UObject> ValueTypeObject = nullptr;

	/** Unique ID for this property. Used as main identifier when copying values over. */
	UPROPERTY(EditAnywhere, Category="Default")
	FGuid ID;

	/** Name for the property. */
	UPROPERTY(EditAnywhere, Category="Default")
	FName Name;

	/** Type of the value described by this property. */
	UPROPERTY(EditAnywhere, Category="Default")
	EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;

	/** Cached property pointer, set in UPropertyBag::GetOrCreateFromDescs. */
	const FProperty* CachedProperty = nullptr;
};

/**
 * Instanced property bag allows to create and store a bag of properties.
 *
 * When used as editable property, the UI allows properties to be added and removed, and values to be set.
 * The value is stored as a struct, the type of the value is never serialized, instead the composition of the properties
 * is saved with the instance, and the type is recreated on load. The types with same composition of properties share same type (based on hashing).
 *
 * NOTE: Adding or removing properties to the instance is quite expensive as it will create new UPropertyBag, reallocate memory, and copy all values over. 
 *
 * Example usage, this allows the bag to be configured in the UI:
 *
 *		UPROPERTY(EditDefaultsOnly, Category = Common)
 *		FInstancedPropertyBag Bag;
 *
 * Changing the layout from code:
 *
 *		static const FName TemperatureName(TEXT("Temperature"));
 *		static const FName IsHotName(TEXT("bIsHot"));
 *
 *		FInstancedPropertyBag Bag;
 *
 *		// Add properties to the bag, and set their values.
 *		// Adding or removing properties is not cheap, so better do it in batches.
 *		Bag.AddProperties({
 *			{ TemperatureName, EPropertyBagPropertyType::Float },
 *			{ CountName, EPropertyBagPropertyType::Int32 }
 *		});
 *
 *		// Amend the bag with a new property.
 *		Bag.AddProperty(IsHotName, EPropertyBagPropertyType::Bool);
 *		Bag.SetValueBool(IsHotName, true);
 *
 *		// Get value and use the result
 *		if (auto Temperature = Bag.GetValueFloat(TemperatureName); Temperature.IsValid())
 *		{
 *			float Val = Temperature.GetValue();
 *		}
 */

class UPropertyBag;

USTRUCT()
struct STRUCTUTILS_API FInstancedPropertyBag
{
	GENERATED_BODY()

	FInstancedPropertyBag() = default;
	FInstancedPropertyBag(const FInstancedPropertyBag& Other) = default;
	FInstancedPropertyBag(FInstancedPropertyBag&& Other) = default;

	FInstancedPropertyBag& operator=(const FInstancedPropertyBag& InOther) = default;
	FInstancedPropertyBag& operator=(FInstancedPropertyBag&& InOther) = default;

	/** Resets the instance to empty. */
	void Reset()
	{
		Value.Reset();
	}

	/** Initializes the instance from an bag struct. */
	void InitializeFromBagStruct(const UPropertyBag* NewBagStruct);
	
	/**
	 * Copies matching property values from another bag of potentially mismatching layout.
	 * The properties are matched between the bags based on the property ID.
	 * @param Other Reference to the bag to copy the values from
	 */
	void CopyMatchingValuesByID(const FInstancedPropertyBag& NewDescs) const;

	/**
	 * Adds properties to the bag. If property of same name already exists, it will be replaced with the new type.
	 * Numeric property values will be converted if possible, when a property's type changes.
	 * @param Descs Descriptors of new properties to add.
	 */
	void AddProperties(const TConstArrayView<FPropertyBagPropertyDesc> Descs);
	
	/**
	 * Adds a new property to the bag. If property of same name already exists, it will be replaced with the new type.
	 * Numeric property values will be converted if possible, when a property's type changes.
	 * @param Descs Descriptors of new properties to add.
	 */
	void AddProperty(const FName InName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr);

	/**
	 * Removes properties from the bag by name if they exists.
	 */
	void RemovePropertiesByName(const TConstArrayView<FName> PropertiesToRemove);
	
	/**
	 * Removes a property from the bag by name if it exists.
	 */
	void RemovePropertyByName(const FName PropertyToRemove);
	
	/**
	 * Changes the type of this bag and migrates existing values.
	 * The properties are matched between the bags based on the property ID.
	 * @param NewBagStruct Pointer to the new type.
	 */
	void MigrateToNewBagStruct(const UPropertyBag* NewBagStruct);

	/** @return pointer to the property bag struct. */ 
	const UPropertyBag* GetPropertyBagStruct() const;
	
	/** Returns property descriptor by specified name. */
	const FPropertyBagPropertyDesc* FindPropertyDescByID(const FGuid ID) const;
	
	/** Returns property descriptor by specified ID. */
	const FPropertyBagPropertyDesc* FindPropertyDescByName(const FName Name) const;

	/** @return const view to the struct that holds the values. NOTE: The returned value/view cannot be serialized, use this to access the struct only temporarily. */
	FConstStructView GetValue() const { return Value; };

	/** @return const view to the struct that holds the values. NOTE: The returned value/view cannot be serialized, use this to access the struct only temporarily. */
	FStructView GetMutableValue() const { return Value; };
	
	/**
	 * Getters
	 * Numeric types (bool, int32, int64, float, double) support type conversion.
	 */

	TValueOrError<bool, EPropertyBagResult> GetValueBool(const FName Name) const;
	TValueOrError<uint8, EPropertyBagResult> GetValueByte(const FName Name) const;
	TValueOrError<int32, EPropertyBagResult> GetValueInt32(const FName Name) const;
	TValueOrError<int64, EPropertyBagResult> GetValueInt64(const FName Name) const;
	TValueOrError<float, EPropertyBagResult> GetValueFloat(const FName Name) const;
	TValueOrError<double, EPropertyBagResult> GetValueDouble(const FName Name) const;
	TValueOrError<FName, EPropertyBagResult> GetValueName(const FName Name) const;
	TValueOrError<FString, EPropertyBagResult> GetValueString(const FName Name) const;
	TValueOrError<FText, EPropertyBagResult> GetValueText(const FName Name) const;
	TValueOrError<uint8, EPropertyBagResult> GetValueEnum(const FName Name, const UEnum* RequestedEnum) const;
	TValueOrError<FStructView, EPropertyBagResult> GetValueStruct(const FName Name, const UScriptStruct* RequestedStruct = nullptr) const;
	TValueOrError<UObject*, EPropertyBagResult> GetValueObject(const FName Name, const UClass* RequestedClass = nullptr) const;
	TValueOrError<UClass*, EPropertyBagResult> GetValueClass(const FName Name) const;

	/** @return enum value of specified type. */
	template <typename T>
	TValueOrError<T, EPropertyBagResult> GetValueEnum(const FName Name) const
	{
		static_assert(TIsEnum<T>::Value, "Should only call this with enum types");
		
		TValueOrError<uint8, EPropertyBagResult> Result = GetValueEnum(Name, StaticEnum<T>());
		if (!Result.IsValid())
		{
			return MakeError(Result.GetError());
		}
		return MakeValue((T)Result.GetValue());
	}
	
	/** @return struct reference of specified type. */
	template <typename T>
	TValueOrError<T*, EPropertyBagResult> GetValueStruct(const FName Name) const
	{
		TValueOrError<FStructView, EPropertyBagResult> Result = GetValueStruct(Name, TBaseStructure<T>::Get());
		if (!Result.IsValid())
		{
			return MakeError(Result.GetError());
		}
		if (T* ValuePtr = Result.GetValue().GetMutablePtr<T>())
		{
			return MakeValue(ValuePtr);
		}
		return MakeError(EPropertyBagResult::TypeMismatch);
	}
	
	/** @return object pointer value of specified type. */
	template <typename T>
	TValueOrError<T*, EPropertyBagResult> GetValueObject(const FName Name) const
	{
		static_assert(TIsDerivedFrom<T, UObject>::Value, "Should only call this with object types");
		
		TValueOrError<UObject*, EPropertyBagResult> Result = GetValueObject(Name, T::StaticClass());
		if (!Result.IsValid())
		{
			return MakeError(Result.GetError());
		}
		if (Result.GetValue() == nullptr)
		{
			return MakeValue(nullptr);
		}
		if (T* Object = Cast<T>(Result.GetValue()))
		{
			return MakeValue(Object);
		}
		return MakeError(EPropertyBagResult::TypeMismatch);
	}

	/**
	 * Value Setters. A property must exists in that bag before it can be set.  
	 * Numeric types (bool, int32, int64, float, double) support type conversion.
	 */
	EPropertyBagResult SetValueBool(const FName Name, const bool bInValue) const;
	EPropertyBagResult SetValueByte(const FName Name, const uint8 InValue) const;
	EPropertyBagResult SetValueInt32(const FName Name, const int32 InValue) const;
	EPropertyBagResult SetValueInt64(const FName Name, const int64 InValue) const;
	EPropertyBagResult SetValueFloat(const FName Name, const float InValue) const;
	EPropertyBagResult SetValueDouble(const FName Name, const double InValue) const;
	EPropertyBagResult SetValueName(const FName Name, const FName InValue) const;
	EPropertyBagResult SetValueString(const FName Name, const FString& InValue) const;
	EPropertyBagResult SetValueText(const FName Name, const FText& InValue) const;
	EPropertyBagResult SetValueEnum(const FName Name, const uint8 InValue, const UEnum* Enum) const;
	EPropertyBagResult SetValueStruct(const FName Name, FConstStructView InValue) const;
	EPropertyBagResult SetValueObject(const FName Name, UObject* InValue) const;
	EPropertyBagResult SetValueClass(const FName Name, UClass* InValue) const;

	/** Sets enum value specified type. */
	template <typename T>
	EPropertyBagResult SetValueEnum(const FName Name, const T InValue) const
	{
		static_assert(TIsEnum<T>::Value, "Should only call this with enum types");
		return SetValueEnum(Name, (uint8)InValue, StaticEnum<T>());
	}

	/** Sets struct value specified type. */
	template <typename T>
	EPropertyBagResult SetValueStruct(const FName Name, const T& InValue) const
	{
		return SetValueStruct(Name, FConstStructView::Make(InValue));
	}

	/** Sets object pointer value specified type. */
	template <typename T>
	EPropertyBagResult SetValueObject(const FName Name, T* InValue) const
	{
		static_assert(TIsDerivedFrom<T, UObject>::Value, "Should only call this with object types");
		return SetValueObject(Name, (UObject*)InValue);
	}

	bool Serialize(FArchive& Ar);

protected:
	UPROPERTY(EditAnywhere, Category="")
	FInstancedStruct Value;
};

template<> struct TStructOpsTypeTraits<FInstancedPropertyBag> : public TStructOpsTypeTraitsBase2<FInstancedPropertyBag>
{
	enum
	{
		WithSerializer = true
	};
};

/**
 * A script struct that is used to store the value of the property bag instance.
 * References to UPropertyBag cannot be serialized, instead the array of the properties
 * is serialized and new class is create on load based on the composition of the properties.
 *
 * Note: Should not be used directly.
 */
UCLASS(Transient)
class STRUCTUTILS_API UPropertyBag : public UScriptStruct
{
public:
	GENERATED_BODY()

	/**
	 * Creates new UPropertyBag struct based on the properties passed in.
	 * If there are multiple properties that have the same name, only the first one is added.
	 */
	static const UPropertyBag* GetOrCreateFromDescs(const TConstArrayView<FPropertyBagPropertyDesc> InPropertyDescs);

	/** Returns property descriptions that specify this struct. */
	TConstArrayView<FPropertyBagPropertyDesc> GetPropertyDescs() const { return PropertyDescs; }

	/** @return property description based on ID. */
	const FPropertyBagPropertyDesc* FindPropertyDescByID(const FGuid ID) const;

	/** @return property description based on name. */
	const FPropertyBagPropertyDesc* FindPropertyDescByName(const FName Name) const;

protected:
	UPROPERTY()
	TArray<FPropertyBagPropertyDesc> PropertyDescs;

	friend struct FInstancedPropertyBag;
};
