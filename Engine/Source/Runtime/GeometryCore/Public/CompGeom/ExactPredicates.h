// Copyright Epic Games, Inc. All Rights Reserved.

// Interface for exact predicates w/ Unreal Engine vector types

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"

namespace UE {
namespace Geometry {
namespace ExactPredicates {

using namespace UE::Math;

/**
 * Must be called once for exact predicates to work.
 * Will be called by GeometryAlgorithmsModule startup, so you don't need to manually call this.
 */
void GEOMETRYCORE_API GlobalInit();

double GEOMETRYCORE_API Orient2DInexact(double* PA, double* PB, double* PC);
double GEOMETRYCORE_API Orient2D(double* PA, double* PB, double* PC);

double GEOMETRYCORE_API Orient3DInexact(double* PA, double* PB, double* PC, double* PD);
double GEOMETRYCORE_API Orient3D(double* PA, double* PB, double* PC, double* PD);

double GEOMETRYCORE_API InCircleInexact(double* PA, double* PB, double* PC, double* PD);
double GEOMETRYCORE_API InCircle(double* PA, double* PB, double* PC, double* PD);

float GEOMETRYCORE_API Orient2DInexact(float* PA, float* PB, float* PC);
float GEOMETRYCORE_API Orient2D(float* PA, float* PB, float* PC);

float GEOMETRYCORE_API Orient3DInexact(float* PA, float* PB, float* PC, float* PD);
float GEOMETRYCORE_API Orient3D(float* PA, float* PB, float* PC, float* PD);

float GEOMETRYCORE_API InCircleInexact(float* PA, float* PB, float* PC, float* PD);
float GEOMETRYCORE_API InCircle(float* PA, float* PB, float* PC, float* PD);

/**
 * Fully generic version; always computes in double precision
 * @return value indicating which side of line AB point C is on, or 0 if ABC are collinear
 */
template<typename VectorType>
double Orient2D(const VectorType& A, const VectorType& B, const VectorType& C)
{
	double PA[2]{ A.X, A.Y };
	double PB[2]{ B.X, B.Y };
	double PC[2]{ C.X, C.Y };
	return Orient2D(PA, PB, PC);
}

/**
 * Fully generic version; always computes in double precision
 * @return value indicating which side of triangle ABC point D is on, or 0 if ABCD are coplanar
 */
template<typename VectorType>
double Orient3D(const VectorType& A, const VectorType& B, const VectorType& C, const VectorType& D)
{
	double PA[3]{ A.X, A.Y, A.Z };
	double PB[3]{ B.X, B.Y, B.Z };
	double PC[3]{ C.X, C.Y, C.Z };
	double PD[3]{ D.X, D.Y, D.Z };
	return Orient3D(PA, PB, PC, PD);
}

// Note: Fully generic version of InCircle not provided; favor InCircle2<RealType>
//template<typename VectorType>
//double InCircle(const VectorType& A, const VectorType& B, const VectorType& C, const VectorType& D)

/**
 * TVector2-only version that can run in float or double
 * @return value indicating which side of line AB point C is on, or 0 if ABC are collinear
 */
template<typename RealType>
RealType Orient2(const TVector2<RealType>& A, const TVector2<RealType>& B, const TVector2<RealType>& C)
{
	RealType PA[2]{ A.X, A.Y };
	RealType PB[2]{ B.X, B.Y };
	RealType PC[2]{ C.X, C.Y };
	return Orient2D(PA, PB, PC);
}

/**
 * TVector-only version that can run in float or double
 * @return value indicating which side of triangle ABC point D is on, or 0 if ABCD are coplanar
 */
template<typename RealType>
RealType Orient3(const TVector<RealType>& A, const TVector<RealType>& B, const TVector<RealType>& C, const TVector<RealType>& D)
{
	RealType PA[3]{ A.X, A.Y, A.Z };
	RealType PB[3]{ B.X, B.Y, B.Z };
	RealType PC[3]{ C.X, C.Y, C.Z };
	RealType PD[3]{ D.X, D.Y, D.Z };
	return Orient3D(PA, PB, PC, PD);
}

/**
 * TVector2-only version that can run in float or double
 * @return value indicating whether point D is inside, outside, or exactly on the circle passing through ABC
 * Note: Sign of the result depends on the orientation of triangle ABC --
 *	Counterclockwise: Inside is positive
 *	Clockwise: Inside is negative
 */
template<typename RealType>
RealType InCircle2(const TVector2<RealType>& A, const TVector2<RealType>& B, const TVector2<RealType>& C, const TVector2<RealType>& D)
{
	RealType PA[2]{ A.X, A.Y };
	RealType PB[2]{ B.X, B.Y };
	RealType PC[2]{ C.X, C.Y };
	RealType PD[2]{ D.X, D.Y };
	return InCircle(PA, PB, PC, PD);
}


// TODO: insphere predicates (currently disabled because they have a huge stack allocation that scares static analysis)

}}} // namespace UE::Geometry::ExactPredicates
