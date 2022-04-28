// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "UObject/NameTypes.h"
#include "Serialization/StructuredArchive.h"

class UObject;
class CORE_API FString;

/* 
 * A struct that can reference a top level asset such as '/Path/To/Package.AssetName'
 * Stores two FNames internally to avoid
 *  a) storing a concatenated FName that bloats global FName storage
 *  b) storing an empty FString for a subobject path as FSoftObjectPath allows
 * Can also be used to reference the package itself in which case the second name is NAME_None
 * and the object resolves to the string `/Path/To/Package`
 * This struct is mirrored and exposed to the UE reflection system in NoExportTypes.h
*/
struct COREUOBJECT_API FTopLevelAssetPath
{
	FTopLevelAssetPath() { }
	FTopLevelAssetPath(TYPE_OF_NULLPTR) { }

	/** Construct directly from components */
	FTopLevelAssetPath(FName InPackageName, FName InAssetName)
	{
		TrySetPath(InPackageName, InAssetName);
	}

	UE_DEPRECATED(5.0, "FNames containing full asset paths have been replaced by FTopLevelAssetPath/FSoftLevelObjectPath."
		"This function is only for temporary use interfacing with APIs that still produce an FName."
		"Those APIS should be updated to use FTopLevelAssetPath or FSoftLevelObjectPath.")
	FTopLevelAssetPath(FName InPath)
	{
		TrySetPath(InPath.ToString());
	}

	/** Construct from string / string view / raw string of a supported character type. */
	explicit FTopLevelAssetPath(const FString& Path) { TrySetPath(FStringView(Path)); }
	template<typename CharType>
	explicit FTopLevelAssetPath(TStringView<CharType> Path) { TrySetPath(Path); }
	template<typename CharType>
	explicit FTopLevelAssetPath(const CharType* Path) { TrySetPath(TStringView<CharType>(Path)); }

	/** Construct from an existing object in memory */
	explicit FTopLevelAssetPath(const UObject* InObject);

	/** Assign from the same types we can construct from */
	FTopLevelAssetPath& operator=(const FString& Path) { TrySetPath(FStringView(Path)); return *this; }
	template<typename CharType>
	FTopLevelAssetPath& operator=(TStringView<CharType> Path) { TrySetPath(Path); return *this; }
	template<typename CharType>
	FTopLevelAssetPath& operator=(const CharType* Path) { TrySetPath(TStringView<CharType>(Path)); return *this; }
	FTopLevelAssetPath& operator=(TYPE_OF_NULLPTR) { Reset(); return *this; }

	/** Sets asset path of this reference based on components. */
	bool TrySetPath(FName InPackageName, FName InAssetName);
	/** Sets asset path of this reference based on a string path. Resets this object and returns false if the string is empty or does not represent a top level asset path. */
	bool TrySetPath(FWideStringView Path);
	bool TrySetPath(FAnsiStringView Path);
	template<typename CharType>
	bool TrySetPath(const CharType* Path) { return TrySetPath(TStringView<CharType>(Path)); }
	bool TrySetPath(const FString& Path) { return TrySetPath(FStringView(Path)); }

	/** Return the package name part e.g. /Path/To/Package as an FName. */
	FName GetPackageName() const { return PackageName; }

	/** Return the asset name part e.g. AssetName as an FName. */
	FName GetAssetName() const { return AssetName; }

	/** Append the full asset path (e.g. '/Path/To/Package.AssetName') to the string builder. */
	void AppendString(FStringBuilderBase& Builder) const;
	/** Append the full asset path (e.g. '/Path/To/Package.AssetName') to the string. */
	void AppendString(FString& OutString) const;

	/** Return the full asset path (e.g. '/Path/To/Package.AssetName') as a string. */
	FString ToString() const;
	/** Copy the full asset path (e.g. '/Path/To/Package.AssetName') into the provided string. */
	void ToString(FString& OutString) const;

	// Return the full asset path (e.g. '/Path/To/Package.AssetName') as an FName.
	UE_DEPRECATED(5.1, "FNames containing full asset paths have been replaced by FTopLevelAssetPath/FSoftLevelObjectPath."
		"This function is only for temporary use interfacing with APIs that still expect an FName."
		"Those APIS should be updated to use FTopLevelAssetPath or FSoftLevelObjectPath.")
	FName ToFName() const { return *ToString(); }

	/** Check if this could possibly refer to a real object */
	bool IsValid() const
	{
		return !PackageName.IsNone();
	}

	/** Checks to see if this is initialized to null */
	bool IsNull() const
	{
		return PackageName.IsNone();
	}

	/** Resets reference to point to null */
	void Reset()
	{		
		PackageName = AssetName = FName();
	}
	/** Compares two paths for non-case-sensitive equality. */
	bool operator==(FTopLevelAssetPath const& Other) const
	{
		return PackageName == Other.PackageName && AssetName == Other.AssetName;
	}

	/** Compares two paths for non-case-sensitive inequality. */
	bool operator!=(FTopLevelAssetPath const& Other) const
	{
		return !(*this == Other);
	}

	/** Serializes the internal path. Unlike FSoftObjectPath, does not handle any PIE or redirector fixups. */
	friend FArchive& operator<<(FArchive& Ar, FTopLevelAssetPath& Path)
	{
		return Ar << Path.PackageName << Path.AssetName;
	}

	/** Serializes the internal path. Unlike FSoftObjectPath, does not handle any PIE or redirector fixups. */
	friend void operator<<(FStructuredArchive::FSlot Slot, FTopLevelAssetPath& Path)
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		Record << SA_VALUE(TEXT("PackageName"), Path.PackageName) << SA_VALUE(TEXT("AssetName"), Path.AssetName);
	}

	/** Lexically compares two paths. */
	int32 Compare(const FTopLevelAssetPath& Other) const
	{
		if (int32 Diff = PackageName.Compare(Other.PackageName))
		{
			return Diff;
		}
		return AssetName.Compare(Other.AssetName);
	}

	/** Compares two paths in a fast non-lexical order that is only valid for process lifetime. */
	int32 CompareFast(const FTopLevelAssetPath& Other) const
	{
		if (int32 Diff = PackageName.CompareIndexes(Other.PackageName))
		{
			return Diff;
		}
		return AssetName.CompareIndexes(Other.AssetName);
	}

	friend uint32 GetTypeHash(FTopLevelAssetPath const& This)
	{
		return HashCombineFast(GetTypeHash(This.PackageName), GetTypeHash(This.AssetName));
	}

private:
	/** Name of the package containing the asset e.g. /Path/To/Package */
	FName PackageName;
	/** Name of the asset within the package e.g. 'AssetName' */
	FName AssetName;
};


inline FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FTopLevelAssetPath& Path)
{
	Path.AppendString(Builder);
	return Builder;
}
