// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"
#include "StructTypeBitSet.h"
#include "StructUtilsTestTypes.h"

#if WITH_STRUCTUTILS_DEBUG

#define LOCTEXT_NAMESPACE "StructUtilsTests"

PRAGMA_DISABLE_OPTIMIZATION

DECLARE_STRUCTTYPEBITSET(FTestStructBaseBitSet, FTestStructSimpleBase);
DEFINE_TYPEBITSET(FTestStructBaseBitSet);

namespace FStructTypeBitSetTests
{
struct FTestStructBitSet : public TStructTypeBitSet<FTestStructSimple>
{
	FTestStructBitSet() = default;
	FTestStructBitSet(const TStructTypeBitSet<FTestStructSimple>& Other) 
		: TStructTypeBitSet<FTestStructSimple>(Other)
	{

	}

	void AddBit(const int32 Index)
	{
		DebugGetMutableStructTypesBitArray().PadToNum(Index + 1, false);
		DebugGetMutableStructTypesBitArray()[Index] = true;
	}

	void RemoveBit(const int32 Index)
	{
		DebugGetMutableStructTypesBitArray()[Index] = false;
	}

	bool TestBit(const int32 Index) const
	{
		return Index >= 0 && Index < DebugGetStructTypesBitArray().Num() && DebugGetStructTypesBitArray()[Index];
	}
};

struct FStructUtilsTest_BitSetEquivalence : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBitSet CollectionA;
		FTestStructBitSet CollectionB;

		AITEST_TRUE("Empty collections are equivalent", CollectionA.IsEquivalent(CollectionB));
		AITEST_TRUE("Equivalence check is commutative", CollectionA.IsEquivalent(CollectionB) == CollectionB.IsEquivalent(CollectionA));

		CollectionA.AddBit(1);
		AITEST_FALSE("Given collections are not equivalent", CollectionA.IsEquivalent(CollectionB));
		AITEST_TRUE("Equivalence check is commutative", CollectionA.IsEquivalent(CollectionB) == CollectionB.IsEquivalent(CollectionA));

		CollectionB.AddBit(1);
		AITEST_TRUE("Given collections are equivalent", CollectionA.IsEquivalent(CollectionB));
		AITEST_TRUE("Equivalence check is commutative", CollectionA.IsEquivalent(CollectionB) == CollectionB.IsEquivalent(CollectionA));

		CollectionA.AddBit(124);
		AITEST_FALSE("Given collections are not equivalent", CollectionA.IsEquivalent(CollectionB));
		AITEST_TRUE("Equivalence check is commutative", CollectionA.IsEquivalent(CollectionB) == CollectionB.IsEquivalent(CollectionA));

		CollectionA.RemoveBit(124);
		AITEST_TRUE("Given collections are equivalent", CollectionA.IsEquivalent(CollectionB));
		AITEST_TRUE("Equivalence check is commutative", CollectionA.IsEquivalent(CollectionB) == CollectionB.IsEquivalent(CollectionA));

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetEquivalence, "System.StructUtils.BitSet.Equivalence");

struct FStructUtilsTest_BitSetEmptiness : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBitSet Collection;

		AITEST_TRUE("New collection is empty", Collection.IsEmpty());
				
		Collection.AddBit(125);
		AITEST_FALSE("Extended collection is not empty", Collection.IsEmpty());
		
		Collection.RemoveBit(125);
		AITEST_TRUE("Removing the remote bit should make the collection empty again", Collection.IsEmpty());

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetEmptiness, "System.StructUtils.BitSet.Emptiness");

struct FStructUtilsTest_BitSetComparison : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBitSet CollectionA;
		FTestStructBitSet CollectionNone;
		FTestStructBitSet CollectionAll;
		FTestStructBitSet CollectionSome;

		CollectionA.AddBit(1);
		CollectionA.AddBit(32);
		CollectionSome = CollectionA;
		CollectionSome.AddBit(111);

		CollectionA.AddBit(65);
		CollectionAll = CollectionA;

		CollectionA.AddBit(76);

		CollectionNone.AddBit(2);
		CollectionNone.AddBit(77);

		AITEST_TRUE("Given collection should confirm it has all its element", CollectionA.HasAll(CollectionA));
		AITEST_TRUE("CollectionA has all the elements indicated by CollectionAll set", CollectionA.HasAll(CollectionAll));
		AITEST_FALSE("HasAll is not commutative", CollectionAll.HasAll(CollectionA));

		AITEST_TRUE("CollectionA has none of the elements stored in CollectionNone", CollectionA.HasNone(CollectionNone));
		AITEST_TRUE("HasNone is commutative", CollectionNone.HasNone(CollectionA));

		AITEST_TRUE("", CollectionA.HasAny(CollectionSome));
		AITEST_TRUE("HasAny is commutative", CollectionSome.HasAny(CollectionA));

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetComparison, "System.StructUtils.BitSet.Comparison");

struct FStructUtilsTest_BitSetSubtraction : FAITestBase
{
	virtual bool InstantTest() override
	{
		constexpr int TotalBits = 60;
		constexpr int BitsToClear = 40;

		FTestStructBitSet CollectionA;
		FTestStructBitSet CollectionB;
		
		for (int i = 0; i < TotalBits; ++i)
		{
			CollectionA.AddBit(i);
		}
		for (int i = 0; i < BitsToClear; ++i)
		{
			CollectionB.AddBit(i);
		}
		
		FTestStructBitSet CollectionC = CollectionA - CollectionB;

		for (int i = 0; i < BitsToClear; ++i)
		{
			AITEST_FALSE("Testing expected bit cleared", CollectionC.TestBit(i));
		}
		for (int i = BitsToClear; i < TotalBits; ++i)
		{
			AITEST_TRUE("Testing expected bit remaining", CollectionC.TestBit(i));
		}

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetSubtraction, "System.StructUtils.BitSet.Subtraction");

struct FStructUtilsTest_BitSetOverlap : FAITestBase
{
	virtual bool InstantTest() override
	{
		constexpr int BitsACount = 40;
		constexpr int OverlapBitsCount = 10;
		constexpr int BitsBCount = 30;

		FTestStructBitSet CollectionA;
		FTestStructBitSet CollectionB;

		for (int i = 0; i < BitsACount; ++i)
		{
			CollectionA.AddBit(i);
		}
		for (int i = 0; i < BitsBCount; ++i)
		{
			CollectionB.AddBit(BitsACount - OverlapBitsCount + i);
		}

		FTestStructBitSet CollectionC = CollectionA & CollectionB;
		FTestStructBitSet CollectionD = CollectionB & CollectionA;

		AITEST_TRUE("Overlap operator is commutative", CollectionC.IsEquivalent(CollectionD));

		int j = 0;
		for (int i = 0; i < BitsACount - OverlapBitsCount; ++i, ++j)
		{
			AITEST_FALSE("Testing not-overlapping bits", CollectionC.TestBit(j));
		}
		for (int i = 0; i < OverlapBitsCount; ++i, ++j)
		{
			AITEST_TRUE("Testing overlapping bits", CollectionC.TestBit(j));
		}
		for (int i = 0; i < BitsBCount - OverlapBitsCount; ++i, ++j)
		{
			AITEST_FALSE("Testing remaining non-overlapping bits", CollectionC.TestBit(j));
		}

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetOverlap, "System.StructUtils.BitSet.Overlap");

struct FStructUtilsTest_BitSetHash : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBitSet EmptyCollection;
		FTestStructBitSet CollectionA;
		FTestStructBitSet CollectionB;

		CollectionA.AddBit(9);
		CollectionB.AddBit(9);
		CollectionB.AddBit(1024);

		const uint32 HashA = GetTypeHash(CollectionA);
		const uint32 HashB = GetTypeHash(CollectionB);

		AITEST_NOT_EQUAL("Two distinct bit sets should have distinct hashes", HashA, HashB);

		CollectionB.RemoveBit(1024);
		const uint32 HashB2 = GetTypeHash(CollectionB);
		AITEST_EQUAL("Two bit sets of the same composition should have have identical hashes", HashA, HashB2);
				
		CollectionB.RemoveBit(9);
		const uint32 HashEmpty = GetTypeHash(EmptyCollection);
		const uint32 HashEmptyB = GetTypeHash(CollectionB);
		AITEST_EQUAL("An emptied bit set needs to have the same hash as an empty bit set", HashEmpty, HashEmptyB);

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_BitSetHash, "System.StructUtils.BitSet.Hash");

struct FStructUtilsTest_CompileTimeBitSet : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBaseBitSet::DebugResetStructTypeMappingInfo();

		FTestStructBaseBitSet BitSet1 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple1>();
		FTestStructBaseBitSet BitSet3 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple3>();
		FTestStructBaseBitSet BitSet5 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple5>();
		FTestStructBaseBitSet BitSet4 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple4>();
		FTestStructBaseBitSet BitSet2 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple2>();
		FTestStructBaseBitSet BitSet6 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple6>();
		FTestStructBaseBitSet BitSet7 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple7>();

		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet1.IsEquivalent(BitSet2));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet1.IsEquivalent(BitSet5));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet2.IsEquivalent(BitSet1));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet2.IsEquivalent(BitSet5));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet5.IsEquivalent(BitSet1));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet5.IsEquivalent(BitSet2));
		
		FTestStructBaseBitSet BitSet15;
		BitSet15.Add<FTestStructSimple1>();
		BitSet15.Add<FTestStructSimple5>();
		AITEST_TRUE("BitSets created for a list of types should be equivalent to the combination of BitSets to individual types", BitSet15.IsEquivalent(BitSet1 | BitSet5));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet2));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet3));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet4));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet6));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet7));

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_CompileTimeBitSet, "System.StructUtils.BitSet.CompileTime");

struct FStructUtilsTest_CompileTimeBitSetReversed : FAITestBase
{
	virtual bool InstantTest() override
	{
		FTestStructBaseBitSet::DebugResetStructTypeMappingInfo();

		FTestStructBaseBitSet BitSet15;
		BitSet15.Add<FTestStructSimple1>();
		BitSet15.Add<FTestStructSimple5>();

		FTestStructBaseBitSet BitSet1 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple1>();
		FTestStructBaseBitSet BitSet3 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple3>();
		FTestStructBaseBitSet BitSet5 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple5>();
		FTestStructBaseBitSet BitSet4 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple4>();
		FTestStructBaseBitSet BitSet2 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple2>();
		FTestStructBaseBitSet BitSet6 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple6>();
		FTestStructBaseBitSet BitSet7 = FTestStructBaseBitSet::GetTypeBitSet<FTestStructSimple7>();

		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet1.IsEquivalent(BitSet2));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet1.IsEquivalent(BitSet5));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet2.IsEquivalent(BitSet1));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet2.IsEquivalent(BitSet5));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet5.IsEquivalent(BitSet1));
		AITEST_FALSE("BitSets created for different types should not be equivalent", BitSet5.IsEquivalent(BitSet2));
		
		AITEST_TRUE("BitSets created for a list of types should be equivalent to the combination of BitSets to individual types", BitSet15.IsEquivalent(BitSet1 | BitSet5));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet2));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet3));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet4));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet6));
		AITEST_FALSE("BitSets created for a list of types should not contain bit for other types", BitSet15.HasAny(BitSet7));

		return true;
	}
};

IMPLEMENT_AI_INSTANT_TEST(FStructUtilsTest_CompileTimeBitSetReversed, "System.StructUtils.BitSet.CompileTime_Reversed");

} // namespace FStructTypeBitSetTests

#undef LOCTEXT_NAMESPACE

PRAGMA_ENABLE_OPTIMIZATION

#endif // WITH_STRUCTUTILS_DEBUG
