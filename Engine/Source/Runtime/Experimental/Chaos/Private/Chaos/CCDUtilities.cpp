// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/CCDUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ChaosDebugDraw.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	namespace CVars
	{
		// @todo(chaos): most of these cvars should be settings per particle

		// NOTE: With this disabled secondary CCD collisions will often be missed
		// @todo(chaos): resweeping also change contacts so it raises questions about collision modifier callbacks and CCD
		bool bChaosCollisionCCDEnableResweep = true;
		FAutoConsoleVariableRef CVarChaosCollisionCCDEnableResweep(TEXT("p.Chaos.Collision.CCD.EnableResweep"), bChaosCollisionCCDEnableResweep, TEXT("Enable resweep for CCD. Resweeping allows CCD to catch more secondary collisions but also is more costly. Default is true."));

		// NOTE: With this disabled, secondary collisions can be missed. When enabled, velocity will not be visually consistent after CCD collisions (if ChaosCollisionCCDConstraintMaxProcessCount is too low)
		bool bChaosCollisionCCDAllowClipping = true;
		FAutoConsoleVariableRef CVarChaosCollisionCCDAllowClipping(TEXT("p.Chaos.Collision.CCD.AllowClipping"), bChaosCollisionCCDAllowClipping, TEXT("This will clip the CCD object at colliding positions when computation budgets run out. Default is true. Turning this option off might cause tunneling."));

		// By default, we stop processing CCD contacts after a single CCD interaction
		// This will result in a visual velocity glitch when it happens, but usually this doesn't matter since the impact is very high energy anyway
		int32 ChaosCollisionCCDConstraintMaxProcessCount = 1;
		FAutoConsoleVariableRef CVarChaosCollisionCCDConstraintMaxProcessCount(TEXT("p.Chaos.Collision.CCD.ConstraintMaxProcessCount"), ChaosCollisionCCDConstraintMaxProcessCount, TEXT("The max number of times each constraint can be resolved when applying CCD constraints. Default is 2. The larger this number is, the more fully CCD constraints are resolved."));

		FRealSingle CCDEnableThresholdBoundsScale = 0.4f;
		FAutoConsoleVariableRef  CVarCCDEnableThresholdBoundsScale(TEXT("p.Chaos.CCD.EnableThresholdBoundsScale"), CCDEnableThresholdBoundsScale , TEXT("CCD is used when object position is changing > smallest bound's extent * BoundsScale. 0 will always Use CCD. Values < 0 disables CCD."));

		extern int32 ChaosSolverDrawCCDInteractions;

#if CHAOS_DEBUG_DRAW
		extern DebugDraw::FChaosDebugDrawSettings ChaosSolverDebugDebugDrawSettings;	
#endif
	}

	void FCCDParticle::AddOverlappingDynamicParticle(FCCDParticle* const InParticle)
	{
		OverlappingDynamicParticles.Add(InParticle);
	}

	void FCCDParticle::AddConstraint(FCCDConstraint* const Constraint)
	{
		AttachedCCDConstraints.Add(Constraint);
	}

	FReal GetParticleCCDThreshold(const FImplicitObject* Implicit)
	{
		if (Implicit)
		{
			// Trimesh/Heightfield are thin, cannot use bounds. We do not want them to contribute to CCD threshold.
			if (Implicit->IsConvex())
			{
				const FReal MinExtent = Implicit->BoundingBox().Extents().Min();
				return MinExtent * CVars::CCDEnableThresholdBoundsScale;
			}

			return 0;
		}
		return 0;
	}

	int32 FCCDConstraint::GetFastMovingKinematicIndex(const FPBDCollisionConstraint* Constraint, const FVec3 Displacements[]) const
	{
		for (int32 i = 0; i < 2; i++)
		{
			const TPBDRigidParticleHandle<FReal, 3>* Rigid = Constraint->GetParticle(i)->CastToRigidParticle();
			if (Rigid && Rigid->ObjectState() == EObjectStateType::Kinematic)
			{
				// The same computation is carried out in UseCCDImpl function when constructing constraints. But we don't have access to FCCDConstraint at that point. This part could potentially be optimized away. 
				const FVec3 D = Displacements[i];
				const FReal DSizeSquared = D.SizeSquared();
				const FReal CCDThreshold = GetParticleCCDThreshold(Constraint->GetImplicit(i));
				if (DSizeSquared > CCDThreshold * CCDThreshold)
				{
					return i;
				}
			}
		}
		return INDEX_NONE;
	}

	void FCCDManager::ApplyConstraintsPhaseCCD(const FReal Dt, FCollisionConstraintAllocator *CollisionAllocator, const int32 NumDynamicParticles)
	{
		SweptConstraints = CollisionAllocator->GetSweptConstraints();
		if (SweptConstraints.Num() > 0)
		{
			ApplySweptConstraints(Dt, SweptConstraints, NumDynamicParticles);
			UpdateSweptConstraints(Dt, CollisionAllocator);
			OverwriteXUsingV(Dt);
		}
	}

	void FCCDManager::ApplySweptConstraints(const FReal Dt, TArrayView<FPBDCollisionConstraint* const> InSweptConstraints, const int32 NumDynamicParticles)
	{
		const bool bNeedCCDSolve = Init(Dt, NumDynamicParticles);
		if (!bNeedCCDSolve)
		{
			return;
		}

		AssignParticleIslandsAndGroupParticles();
		AssignConstraintIslandsAndRecordConstraintNum();
		GroupConstraintsWithIslands();
		PhysicsParallelFor(IslandNum, [&](const int32 Island)
		{
			ApplyIslandSweptConstraints(Island, Dt);
		});
	}

	bool FCCDManager::Init(const FReal Dt, const int32 NumDynamicParticles)
	{
		CCDParticles.Reset();
		// We store pointers to CCDParticle in CCDConstraint and GroupedCCDParticles, therefore we need to make sure to reserve enough space for TArray CCDParticles so that reallocation does not happen in the for loop. If reallocation happens, some pointers may become invalid and this could cause serious bugs. We know that the number of CCDParticles cannot exceed SweptConstraints.Num() * 2 or NumDynamicParticles. Therefore this makes sure reallocation does not happen.
		CCDParticles.Reserve(FMath::Min(SweptConstraints.Num() * 2, NumDynamicParticles));
		ParticleToCCDParticle.Reset();
		CCDConstraints.Reset();
		CCDConstraints.Reserve(SweptConstraints.Num());
		bool bNeedCCDSolve = false;
		for (FPBDCollisionConstraint* Constraint : SweptConstraints)
		{   
			// A contact can be disabled by a user callback or contact pruning so we need to ignore these.
			// NOTE: It important that we explicitly check for disabled here, rather than for 0 manifold points since we
			// may want to use the contact later if resweeping is enabled. 
			if (!Constraint->IsEnabled())
			{
				continue;
			}

			// Create CCDParticle for all dynamic particles affected by swept constraints (UseCCD() could be either true or false). For static or kinematic particles, this pointer remains to be nullptr.
			FCCDParticle* CCDParticlePair[2] = {nullptr, nullptr};
			bool IsDynamic[2] = {false, false};
			FVec3 Displacements[2] = {FVec3(0.f), FVec3(0.f)};
			for (int i = 0; i < 2; i++)
			{
				TPBDRigidParticleHandle<FReal, 3>* RigidParticle = Constraint->GetParticle(i)->CastToRigidParticle();
				FCCDParticle* CCDParticle = nullptr;
				const bool IsParticleDynamic = RigidParticle && RigidParticle->ObjectState() == EObjectStateType::Dynamic;
				if (IsParticleDynamic)
				{
					FCCDParticle** FoundCCDParticle = ParticleToCCDParticle.Find(RigidParticle);
					if (!FoundCCDParticle)
					{
						CCDParticles.Add(FCCDParticle(RigidParticle));
						CCDParticle = &CCDParticles.Last();
						ParticleToCCDParticle.Add(RigidParticle, CCDParticle);
					}
					else
					{
						CCDParticle = *FoundCCDParticle;
					}
					IsDynamic[i] = IsParticleDynamic;
				}
				CCDParticlePair[i] = CCDParticle; 
				IsDynamic[i] = IsParticleDynamic;

				if (RigidParticle)
				{
					// One can also use P - X for dynamic particles. But notice that for kinematic particles, both P and X are end-frame positions and P - X won't work for kinematic particles.
					Displacements[i] = RigidParticle->V() * Dt;
				}
			}

			// Determine if this particle pair should trigger CCD
			const auto Particle0 = Constraint->GetParticle(0);
			const auto Particle1 = Constraint->GetParticle(1);
			bNeedCCDSolve = CCDHelpers::DeltaExceedsThreshold(*Particle0, *Particle1, Dt);

			// make sure we ignore pairs that don't include any dynamics
			if (CCDParticlePair[0] != nullptr || CCDParticlePair[1] != nullptr)
			{
				const FReal CCDConstraintThreshold = FMath::Min(Particle0->CCDAxisThreshold().GetMin(), Particle1->CCDAxisThreshold().GetMin());
				const FReal PhiThreshold = -CCDConstraintThreshold;
				CCDConstraints.Add(FCCDConstraint(Constraint, CCDParticlePair, Displacements, PhiThreshold));
				for (int32 i = 0; i < 2; i++)
				{
					if (CCDParticlePair[i] != nullptr)
					{
						CCDParticlePair[i]->AddConstraint(&CCDConstraints.Last());
					}
				}

				if (IsDynamic[0] && IsDynamic[1])
				{
					CCDParticlePair[0]->AddOverlappingDynamicParticle(CCDParticlePair[1]);
					CCDParticlePair[1]->AddOverlappingDynamicParticle(CCDParticlePair[0]);
				}
			}
		}
		return bNeedCCDSolve;
	}

	void FCCDManager::AssignParticleIslandsAndGroupParticles()
	{
		// Use DFS to find connected dynamic particles and assign islands for them.
		// In the mean time, record numbers in IslandParticleStart and IslandParticleNum
		// Group particles into GroupedCCDParticles based on islands.
		IslandNum = 0;
		IslandStack.Reset();
		GroupedCCDParticles.Reset();
		IslandParticleStart.Reset();
		IslandParticleNum.Reset();
		for (FCCDParticle &CCDParticle : CCDParticles)
		{
			if (CCDParticle.Island != INDEX_NONE || CCDParticle.Particle->ObjectState() != EObjectStateType::Dynamic)
			{
				continue;
			}
			FCCDParticle* CurrentParticle = &CCDParticle;
			CurrentParticle->Island = IslandNum;
			IslandStack.Push(CurrentParticle);
			IslandParticleStart.Push(GroupedCCDParticles.Num());
			int32 CurrentIslandParticleNum = 0;
			while (IslandStack.Num() > 0)
			{
				CurrentParticle = IslandStack.Pop();
				GroupedCCDParticles.Push(CurrentParticle);
				CurrentIslandParticleNum++;
				for (FCCDParticle* OverlappingParticle : CurrentParticle->OverlappingDynamicParticles)
				{
					if (OverlappingParticle->Island == INDEX_NONE)
					{
						OverlappingParticle->Island = IslandNum;
						IslandStack.Push(OverlappingParticle);
					}
				}
			}
			IslandParticleNum.Push(CurrentIslandParticleNum);
			IslandNum++;
		}
	}

	void FCCDManager::AssignConstraintIslandsAndRecordConstraintNum()
	{
		// Assign island to constraints based on particle islands
		// In the mean time, record IslandConstraintNum
		IslandConstraintNum.SetNum(IslandNum);
		for (int32 i = 0; i < IslandNum; i++)
		{
			IslandConstraintNum[i] = 0;
		}

		for (FCCDConstraint &CCDConstraint : CCDConstraints)
		{
			int32 Island = INDEX_NONE;
			if (CCDConstraint.Particle[0])
			{
				Island = CCDConstraint.Particle[0]->Island;
			}
			if (Island == INDEX_NONE)
			{
				// non-dynamic pairs are already ignored in Init() so if Particle 0 is null the second one should not be 
				ensure(CCDConstraint.Particle[1] != nullptr);

				if (CCDConstraint.Particle[1])
				{
					Island = CCDConstraint.Particle[1]->Island;
				}	
			}
			CCDConstraint.Island = Island;
			IslandConstraintNum[Island]++;
		}
	}

	void FCCDManager::GroupConstraintsWithIslands()
	{
		// Group constraints based on island
		// In the mean time, record IslandConstraintStart, IslandConstraintEnd
		IslandConstraintStart.SetNum(IslandNum + 1);
		IslandConstraintEnd.SetNum(IslandNum);
		IslandConstraintStart[0] = 0;
		for (int32 i = 0; i < IslandNum; i++)
		{
			IslandConstraintEnd[i] = IslandConstraintStart[i];
			IslandConstraintStart[i + 1] = IslandConstraintStart[i] + IslandConstraintNum[i];
		}

		SortedCCDConstraints.SetNum(CCDConstraints.Num());
		for (FCCDConstraint &CCDConstraint : CCDConstraints)
		{
			const int32 Island = CCDConstraint.Island;
			SortedCCDConstraints[IslandConstraintEnd[Island]] = &CCDConstraint;
			IslandConstraintEnd[Island]++;
		}
	}

	bool CCDConstraintSortPredicate(const FCCDConstraint* Constraint0, const FCCDConstraint* Constraint1)
	{
		return Constraint0->SweptConstraint->CCDTimeOfImpact < Constraint1->SweptConstraint->CCDTimeOfImpact;
	}

	void FCCDManager::ApplyIslandSweptConstraints(const int32 Island, const FReal Dt)
	{
		const int32 ConstraintStart = IslandConstraintStart[Island];
		const int32 ConstraintNum = IslandConstraintNum[Island];
		const int32 ConstraintEnd = IslandConstraintEnd[Island];
		check(ConstraintNum > 0);

#if CHAOS_DEBUG_DRAW
		if (CVars::ChaosSolverDrawCCDInteractions)
		{
			// Debugdraw the shape at the TOI=0 (black) and TOI=1 (white)
			for (FCCDConstraint* CCDConstraint : SortedCCDConstraints)
			{
				DebugDraw::DrawCCDCollisionShape(FRigidTransform3::Identity, *CCDConstraint, true, FColor::Black, &CVars::ChaosSolverDebugDebugDrawSettings);
				DebugDraw::DrawCCDCollisionShape(FRigidTransform3::Identity, *CCDConstraint, false, FColor::White, &CVars::ChaosSolverDebugDebugDrawSettings);
			}
		}
#endif

		// Sort constraints based on TOI
		std::sort(SortedCCDConstraints.GetData() + ConstraintStart, SortedCCDConstraints.GetData() + ConstraintStart + ConstraintNum, CCDConstraintSortPredicate);
		FReal IslandTOI = 0.f;
		ResetIslandParticles(Island);
		ResetIslandConstraints(Island);
		int32 ConstraintIndex = ConstraintStart;
		while (ConstraintIndex < ConstraintEnd) 
		{
			FCCDConstraint *CCDConstraint = SortedCCDConstraints[ConstraintIndex];
			FCCDParticle* CCDParticle0 = CCDConstraint->Particle[0];
			FCCDParticle* CCDParticle1 = CCDConstraint->Particle[1];

			IslandTOI = CCDConstraint->SweptConstraint->CCDTimeOfImpact;

			// Constraints whose TOIs are in the range of [0, 1) are resolved for this frame. TOI = 1 means that the two 
			// particles just start touching at the end of the frame and therefore cannot have tunneling this frame. 
			// So this TOI = 1 can be left to normal collisions or CCD in next frame.
			if (IslandTOI > 1) 
			{
				break;
			}

			// If both particles are marked Done (due to clipping), continue
			if (CVars::bChaosCollisionCCDAllowClipping && (!CCDParticle0 || CCDParticle0->Done) && (!CCDParticle1 || CCDParticle1->Done))
			{
				ConstraintIndex++;
				continue;
			}
			
			ensure(CCDConstraint->ProcessedCount < CVars::ChaosCollisionCCDConstraintMaxProcessCount);

			// In UpdateConstraintSwept, InitManifoldPoint requires P, Q to be at TOI=1., but the input of 
			// UpdateConstraintSwept requires transforms at current TOI. So instead of rewinding P, Q, we 
			// advance X, R to current TOI and keep P, Q at TOI=1.
			if (CCDParticle0 && !CCDParticle0->Done)
			{
				AdvanceParticleXToTOI(CCDParticle0, IslandTOI, Dt);
			}
			if (CCDParticle1 && !CCDParticle1->Done)
			{
				AdvanceParticleXToTOI(CCDParticle1, IslandTOI, Dt);
			}

#if CHAOS_DEBUG_DRAW
			// Debugdraw the shape at the current TOI
			if (CVars::ChaosSolverDrawCCDInteractions)
			{
				DebugDraw::DrawCCDCollisionShape(FRigidTransform3::Identity, *CCDConstraint, true, FColor::Magenta, &CVars::ChaosSolverDebugDebugDrawSettings);
			}
#endif

			ApplyImpulse(CCDConstraint);
			CCDConstraint->ProcessedCount++;

			// After applying impulse, constraint TOI need be updated to reflect the new velocities. 
			// Usually the new velocities are separating, and therefore TOI should be infinity.
			// See resweep below which (optionally) updates TOI for all other contacts as a result of handling this one
			CCDConstraint->SweptConstraint->CCDTimeOfImpact = TNumericLimits<FReal>::Max();

			bool bMovedParticle0 = false;
			bool bMovedParticle1 = false;
			if (CCDConstraint->ProcessedCount >= CVars::ChaosCollisionCCDConstraintMaxProcessCount)
			{
				/* Here is how clipping works:
				* Assuming collision detection gives us all the possible collision pairs in the current frame.
				* Because we sort and apply constraints based on their TOIs, at current IslandTOI, the two particles cannot tunnel through other particles in the island. 
				* Now, we run out of the computational budget for this constraint, then we freeze the two particles in place. The current two particles cannot tunnel through each other this frame.
				* The two particles are then treated as static. When resweeping, we update TOIs of other constraints to make sure other particles in the island cannot tunnel through this two particles.
				* Therefore, by clipping, we can avoid tunneling but this is at the cost of reduced momentum.
				* For kinematic particles, we cannot freeze them in place. In this case, we simply offset the particle with the kinematic motion from [IslandTOI, 1] along the collision normal and freeze it there.
				* If collision detection is not perfect and does not give us all the secondary collision pairs, setting ChaosCollisionCCDConstraintMaxProcessCount to 1 will always prevent tunneling.
				*/ 
				if (CVars::bChaosCollisionCCDAllowClipping)
				{
					if (CCDParticle0)
					{
						if (CCDConstraint->FastMovingKinematicIndex != INDEX_NONE)
						{
							// @todo(chaos): can this just get the particle from the CCD constraint?
							const FConstGenericParticleHandle KinematicParticle = FGenericParticleHandle(CCDConstraint->SweptConstraint->GetParticle(CCDConstraint->FastMovingKinematicIndex));
							const FVec3 Normal = CCDConstraint->SweptConstraint->CalculateWorldContactNormal();
							const FVec3 Offset = FVec3::DotProduct(KinematicParticle->V() * ((1.f - IslandTOI) * Dt), Normal) * Normal;
							ClipParticleP(CCDParticle0, Offset);
						}
						else
						{
							ClipParticleP(CCDParticle0);
						}
						CCDParticle0->Done = true;
						bMovedParticle0 = true;
					}
					if (CCDParticle1)
					{
						if (CCDConstraint->FastMovingKinematicIndex != INDEX_NONE)
						{
							// @todo(chaos): can this just get the particle from the CCD constraint?
							const FConstGenericParticleHandle KinematicParticle = FGenericParticleHandle(CCDConstraint->SweptConstraint->GetParticle(CCDConstraint->FastMovingKinematicIndex));
							const FVec3 Normal = CCDConstraint->SweptConstraint->CalculateWorldContactNormal();
							const FVec3 Offset = FVec3::DotProduct(KinematicParticle->V() * ((1.f - IslandTOI) * Dt), Normal) * Normal;
							ClipParticleP(CCDParticle1, Offset);
						}
						else
						{
							ClipParticleP(CCDParticle1);
						}
						CCDParticle1->Done = true;
						bMovedParticle1 = true;
					}
				}
				// If clipping is not allowed, we update particle P (at TOI=1) based on new velocities. 
				else
				{
					if (CCDParticle0)
					{
						UpdateParticleP(CCDParticle0, Dt);
						bMovedParticle0 = true;
					}
					if (CCDParticle1)
					{
						UpdateParticleP(CCDParticle1, Dt);
						bMovedParticle1 = true;
					}
				}
				// Increment ConstraintIndex if we run out of computational budget for this constraint.
				ConstraintIndex ++;
			}
			// If we still have computational budget for this constraint, update particle P and don't clip.
			else
			{
				if (CCDParticle0 && !CCDParticle0->Done)
				{
					UpdateParticleP(CCDParticle0, Dt);
					bMovedParticle0 = true;
				}
				if (CCDParticle1 && !CCDParticle1->Done)
				{
					UpdateParticleP(CCDParticle1, Dt);
					bMovedParticle1 = true;
				}
			}

			// We applied a CCD impulse and updated the particle positions, so we need to update all the constraints involving these particles
			bool bHasResweptConstraint = false;
			if (bMovedParticle0)
			{
				bHasResweptConstraint |= UpdateParticleSweptConstraints(CCDParticle0, IslandTOI, Dt);
			}
			if (bMovedParticle1)
			{
				bHasResweptConstraint |= UpdateParticleSweptConstraints(CCDParticle1, IslandTOI, Dt);
			}

			// If we updated some constraints, we need to sort so that we handle the next TOI event
			if (bHasResweptConstraint)
			{
				std::sort(SortedCCDConstraints.GetData() + ConstraintIndex, SortedCCDConstraints.GetData() + ConstraintStart + ConstraintNum, CCDConstraintSortPredicate);
			}
		}
		
		// Update the constraint with the CCD results
		for (FCCDConstraint* CCDConstraint : SortedCCDConstraints)
		{ 
			CCDConstraint->SweptConstraint->SetCCDResults(CCDConstraint->NetImpulse);
		}

#if CHAOS_DEBUG_DRAW
		// Debugdraw the shapes at the final position
		if (CVars::ChaosSolverDrawCCDInteractions)
		{
			for (FCCDConstraint* CCDConstraint : SortedCCDConstraints)
			{
				DebugDraw::DrawCCDCollisionShape(FRigidTransform3::Identity, *CCDConstraint, false, FColor::Green, &CVars::ChaosSolverDebugDebugDrawSettings);
			}
		}
#endif
	}

	bool FCCDManager::UpdateParticleSweptConstraints(FCCDParticle* CCDParticle, const FReal IslandTOI, const FReal Dt)
	{
		const FReal RestDt = (1.f - IslandTOI) * Dt;
		bool HasResweptConstraint = false;
		if (CCDParticle != nullptr)
		{
			for (int32 AttachedCCDConstraintIndex = 0; AttachedCCDConstraintIndex < CCDParticle->AttachedCCDConstraints.Num(); AttachedCCDConstraintIndex++)
			{
				FCCDConstraint* AttachedCCDConstraint = CCDParticle->AttachedCCDConstraints[AttachedCCDConstraintIndex];
				if (AttachedCCDConstraint->ProcessedCount >= CVars::ChaosCollisionCCDConstraintMaxProcessCount)
				{
					continue;
				}

				// Particle transforms at TOI
				FRigidTransform3 ParticleStartWorldTransforms[2];
				for (int32 j = 0; j < 2; j++)
				{
					FCCDParticle* AffectedCCDParticle = AttachedCCDConstraint->Particle[j];
					if (AffectedCCDParticle != nullptr)
					{
						TPBDRigidParticleHandle<FReal, 3>* AffectedParticle = AffectedCCDParticle->Particle;
						if (!AffectedCCDParticle->Done)
						{
							AdvanceParticleXToTOI(AffectedCCDParticle, IslandTOI, Dt);
						}
						ParticleStartWorldTransforms[j] = FRigidTransform3(AffectedParticle->X(), AffectedParticle->R());
					}
					else
					{
						FGenericParticleHandle AffectedParticle = FGenericParticleHandle(AttachedCCDConstraint->SweptConstraint->GetParticle(j));
						const bool IsKinematic = AffectedParticle->ObjectState() == EObjectStateType::Kinematic;
						if (IsKinematic)
						{
							ParticleStartWorldTransforms[j] = FRigidTransform3(AffectedParticle->P() - AffectedParticle->V() * RestDt, AffectedParticle->Q());
						}
						else // Static case
						{
							ParticleStartWorldTransforms[j] = FRigidTransform3(AffectedParticle->X(), AffectedParticle->R());
						}
					}
				}
				/** When resweeping, we need to recompute TOI for affected constraints and therefore the work (GJKRaycast) used to compute the original TOI is wasted.
				* A potential optimization is to compute an estimate of TOI using the AABB of the particles. Sweeping AABBs to compute an estimated TOI can be very efficient, and this TOI is strictly smaller than the accurate TOI.
				* At each for-loop iteration, we only need the constraint with the smallest TOI in the island. A potential optimized algorithm could be like:
				* 	First, sort constraints based on estimated TOI.
				*	Find the constraint with the smallest accurate TOI:
				*		Walk through the constraint list, change estimated TOI to accurate TOI
				*		If accurate TOI is smaller than estimated TOI of the next constraint, we know we found the constraint.
				*	When resweeping, compute estimated TOI instead of accurate TOI since updated TOI might need to be updated again.
				*/

				FPBDCollisionConstraint* SweptConstraint = AttachedCCDConstraint->SweptConstraint;
				const FConstGenericParticleHandle Particle0 = SweptConstraint->GetParticle0();
				const FConstGenericParticleHandle Particle1 = SweptConstraint->GetParticle1();

				// Initial shape sweep transforms
				const FRigidTransform3 ShapeStartWorldTransform0 = SweptConstraint->GetShapeRelativeTransform0() * ParticleStartWorldTransforms[0];
				const FRigidTransform3 ShapeStartWorldTransform1 = SweptConstraint->GetShapeRelativeTransform1() * ParticleStartWorldTransforms[1];

				// End shape sweep transforms
				const FRigidTransform3 ParticleEndWorldTransform0 = FParticleUtilities::GetActorWorldTransform(Particle0);
				const FRigidTransform3 ParticleEndWorldTransform1 = FParticleUtilities::GetActorWorldTransform(Particle1);
				const FRigidTransform3 ShapeEndWorldTransform0 = SweptConstraint->GetShapeRelativeTransform0() * ParticleEndWorldTransform0;
				const FRigidTransform3 ShapeEndWorldTransform1 = SweptConstraint->GetShapeRelativeTransform1() * ParticleEndWorldTransform1;

				// Update of swept constraint assumes that the constraint holds the end transforms for the sweep
				SweptConstraint->SetShapeWorldTransforms(ShapeEndWorldTransform0, ShapeEndWorldTransform1);

				if (CVars::bChaosCollisionCCDEnableResweep)
				{
					// Resweep the shape. This is the expensive option
					Collisions::UpdateConstraintSwept(*SweptConstraint, ShapeStartWorldTransform0, ShapeStartWorldTransform1, RestDt);
				}
				else
				{
					// Keep the contact as-is but update the depth and TOI based on current transforms
					SweptConstraint->UpdateSweptManifoldPoints(ShapeStartWorldTransform0.GetTranslation(), ShapeStartWorldTransform1.GetTranslation(), Dt);
				}

				const FReal RestDtTOI = AttachedCCDConstraint->SweptConstraint->CCDTimeOfImpact;
				if ((RestDtTOI >= 0) && (RestDtTOI < FReal(1)))
				{
					AttachedCCDConstraint->SweptConstraint->CCDTimeOfImpact = IslandTOI + (FReal(1) - IslandTOI) * RestDtTOI;
				}

				// When bUpdated==true, TOI was modified. When bUpdated==false, TOI was set to be TNumericLimits<FReal>::Max(). In either case, a re-sorting on the constraints is needed.
				HasResweptConstraint = true;
			}
		}

		return HasResweptConstraint;
	}

	void FCCDManager::ResetIslandParticles(const int32 Island)
	{
		const int32 ParticleStart = IslandParticleStart[Island];
		const int32 ParticleNum = IslandParticleNum[Island];
		for (int32 i = ParticleStart; i < ParticleStart + ParticleNum; i++)
		{
			GroupedCCDParticles[i]->TOI = 0.f;
			GroupedCCDParticles[i]->Done = false;
		}
	}

	void FCCDManager::ResetIslandConstraints(const int32 Island)
	{
		const int32 ConstraintStart = IslandConstraintStart[Island];
		const int32 ConstraintEnd = IslandConstraintEnd[Island];
		for (int32 i = ConstraintStart; i < ConstraintEnd; i++)
		{
			SortedCCDConstraints[i]->ProcessedCount = 0;
		}
	}

	void FCCDManager::AdvanceParticleXToTOI(FCCDParticle *CCDParticle, const FReal TOI, const FReal Dt) const
	{
		if (TOI > CCDParticle->TOI)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
			const FReal RestDt = (TOI - CCDParticle->TOI) * Dt;
			Particle->X() = Particle->X() + Particle->V() * RestDt;
			CCDParticle->TOI = TOI;
		}
	}

	void FCCDManager::UpdateParticleP(FCCDParticle *CCDParticle, const FReal Dt) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		const FReal RestDt = (1.f - CCDParticle->TOI) * Dt;
		Particle->P() = Particle->X() + Particle->V() * RestDt;
	}

	void FCCDManager::ClipParticleP(FCCDParticle *CCDParticle) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		Particle->P() = Particle->X();
	}

	void FCCDManager::ClipParticleP(FCCDParticle *CCDParticle, const FVec3 Offset) const
	{
		TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle->Particle;
		Particle->X() += Offset;
		Particle->P() = Particle->X();
	}

	void FCCDManager::ApplyImpulse(FCCDConstraint *CCDConstraint)
	{
		FPBDCollisionConstraint *Constraint = CCDConstraint->SweptConstraint;
		TPBDRigidParticleHandle<FReal, 3> *Rigid0 = Constraint->GetParticle0()->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3> *Rigid1 = Constraint->GetParticle1()->CastToRigidParticle();
		check(Rigid0 != nullptr || Rigid1 != nullptr);
		const FReal Restitution = Constraint->GetRestitution();
		const FRigidTransform3& ShapeWorldTransform1 = Constraint->GetShapeWorldTransform1();
		for (int32 ManifoldPointIndex = 0; ManifoldPointIndex < Constraint->NumManifoldPoints(); ++ManifoldPointIndex)
		{
			const FManifoldPoint& ManifoldPoint = Constraint->GetManifoldPoint(ManifoldPointIndex);
			if (ManifoldPoint.Flags.bDisabled)
			{
				continue;
			}
			
			const FVec3 Normal = ShapeWorldTransform1.TransformVectorNoScale(ManifoldPoint.ContactPoint.ShapeContactNormal);
			const FVec3 V0 = Rigid0 != nullptr ? Rigid0->V() : FVec3(0.f);
			const FVec3 V1 = Rigid1 != nullptr ? Rigid1->V() : FVec3(0.f);
			const FReal NormalV = FVec3::DotProduct(V0 - V1, Normal);
			if (NormalV < 0.f)
			{
				const FReal TargetNormalV = -Restitution * NormalV;
				// If a particle is marked done, we treat it as static by setting InvM to 0. 
				const bool bInfMass0 = Rigid0 == nullptr || (CVars::bChaosCollisionCCDAllowClipping && CCDConstraint->Particle[0] && CCDConstraint->Particle[0]->Done);
				const bool bInfMass1 = Rigid1 == nullptr || (CVars::bChaosCollisionCCDAllowClipping && CCDConstraint->Particle[1] && CCDConstraint->Particle[1]->Done);
				const FReal InvM0 = bInfMass0 ? 0.f : Rigid0->InvM();
				const FReal InvM1 = bInfMass1 ? 0.f : Rigid1->InvM();
				const FVec3 Impulse = (TargetNormalV - NormalV) * Normal / (InvM0 + InvM1);
				if (InvM0 > 0.f)
				{
					Rigid0->V() += Impulse * InvM0;
				}
				if (InvM1 > 0.f)
				{
					Rigid1->V() -= Impulse * InvM1;
				}

				CCDConstraint->NetImpulse += Impulse;

#if CHAOS_DEBUG_DRAW
				if (CVars::ChaosSolverDrawCCDInteractions)
				{
					DebugDraw::DrawCCDCollisionImpulse(FRigidTransform3::Identity, *CCDConstraint, ManifoldPointIndex, Impulse, &CVars::ChaosSolverDebugDebugDrawSettings);
				}
#endif
			}
		}
	}

	void FCCDManager::UpdateSweptConstraints(const FReal Dt, FCollisionConstraintAllocator *CollisionAllocator)
	{
		// We need to update the world-space contact points at the final locations
		// @todo(chaos): parallelize this code
		// @todo(chaos): These SweptConstraints might contain non-CCD particles and those non-CCD particles might collide with other non-CCD particles, which are modeled in normal collision constraints. Those normal collision constraints might need to be updated as well.
		for (FPBDCollisionConstraint* SweptConstraint : SweptConstraints)
		{
			if (!SweptConstraint->IsEnabled())
			{
				continue;
			}

			const FConstGenericParticleHandle P0 = FConstGenericParticleHandle(SweptConstraint->GetParticle0());
			const FConstGenericParticleHandle P1 = FConstGenericParticleHandle(SweptConstraint->GetParticle1());
			SweptConstraint->ResetManifold();
			Collisions::UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(*SweptConstraint, FRigidTransform3(P0->P(), P0->Q()), FRigidTransform3(P1->P(), P1->Q()), Dt);

			// @todo(zhenglin): Removing constraints that has Phi larger than CullDistance could reduce the island sizes in the normal solve. But I could not get this to work...
			// if (SweptConstraint->GetPhi() > SweptConstraint->GetCullDistance())
			// {
			//     CollisionAllocator->RemoveConstraintSwap(SweptConstraint);
			// }
		}
	}

	void FCCDManager::OverwriteXUsingV(const FReal Dt)
	{
		// Overwriting X = P - V * Dt so that the implicit velocity step will give our velocity back.
		for (FCCDParticle& CCDParticle : CCDParticles)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle = CCDParticle.Particle;
			Particle->X() = Particle->P() - Particle->V() * Dt;
		}
	}

	bool CCDHelpers::DeltaExceedsThreshold(const FVec3& AxisThreshold, const FVec3& DeltaX, const FQuat& R)
	{
		FVec3 AbsLocalDelta, AxisThresholdScaled, AxisThresholdDiff;
		return DeltaExceedsThreshold(AxisThreshold, DeltaX, R, AbsLocalDelta, AxisThresholdScaled, AxisThresholdDiff);
	}

	bool CCDHelpers::DeltaExceedsThreshold(const FVec3& AxisThreshold, const FVec3& DeltaX, const FQuat& R, FVec3& OutAbsLocalDelta, FVec3& OutAxisThresholdScaled, FVec3& OutAxisThresholdDiff)
	{
		if (CVars::CCDEnableThresholdBoundsScale < 0.f) { return false; }
		if (CVars::CCDEnableThresholdBoundsScale == 0.f) { return true; }

		// Get per-component absolute value of position delta in local space.
		// This is how much we've moved on each principal axis (but not which
		// direction on that axis that we've moved in).
		OutAbsLocalDelta = R.UnrotateVector(DeltaX).GetAbs();

		// Scale the ccd extents in local space and subtract them from the 
		// local space position deltas. This will give us a vector representing
		// how much further we've moved on each axis than should be allowed by
		// the CCD bounds.
		OutAxisThresholdScaled = AxisThreshold * CVars::CCDEnableThresholdBoundsScale;
		OutAxisThresholdDiff = OutAbsLocalDelta - OutAxisThresholdScaled;

		// That is, if any element of ExtentsDiff is greater than zero, then that
		// means DeltaX has exceeded the scaled extents
		return OutAxisThresholdDiff.GetMax() > 0.f;
	}

	bool CCDHelpers::DeltaExceedsThreshold(
		const FVec3& AxisThreshold0, const FVec3& DeltaX0, const FQuat& R0,
		const FVec3& AxisThreshold1, const FVec3& DeltaX1, const FQuat& R1)
	{
		return CCDHelpers::DeltaExceedsThreshold(

			// To combine axis thresholds:
			// * transform particle1's threshold into particle0's local space
			// * take the per-component minimum of each axis threshold
			//
			// To think about why we use component mininma to combine thresholds,
			// imagine what happens when a large object and a small object move
			// towards each other at the same speed. Say particle0 is the large
			// object, and then think about particle1's motion from particle0's
			// inertial frame of reference. In this case, clearly you should
			// choose particle1's threshold since it is the one that is moving.
			//
			// Since there's no preferred inertial frame, the correct choice
			// will always be to take the smaller object's threshold.
			AxisThreshold0.ComponentMin((R0 * R1.UnrotateVector(AxisThreshold1)).GetAbs()),

			// Taking the difference of the deltas gives the total delta - how
			// much the objects have moved towards each other. We choose to use
			// particle0 as the reference.
			DeltaX1 - DeltaX0,

			// Since we're doing this in particle0's space, we choose its rotation.
			R0);
	}

	bool CCDHelpers::DeltaExceedsThreshold(const FGeometryParticleHandle& Particle0, const FGeometryParticleHandle& Particle1)
	{
		// For rigids, compute DeltaX from the X - P diff and use Q for the rotation.
		// For non-rigids, DeltaX is zero and use R for rotation.
		const auto Rigid0 = Particle0.CastToRigidParticle();
		const auto Rigid1 = Particle1.CastToRigidParticle();
		const FVec3 DeltaX0 = Rigid0 ? Rigid0->P() - Rigid0->X() : FVec3::ZeroVector;
		const FVec3 DeltaX1 = Rigid1 ? Rigid1->P() - Rigid1->X() : FVec3::ZeroVector;
		const FQuat& R0 = Rigid0 ? Rigid0->Q() : Particle0.R();
		const FQuat& R1 = Rigid1 ? Rigid1->Q() : Particle1.R();
		return DeltaExceedsThreshold(
			Particle0.CCDAxisThreshold(), DeltaX0, R0,
			Particle1.CCDAxisThreshold(), DeltaX1, R1);
	}

	bool CCDHelpers::DeltaExceedsThreshold(const FGeometryParticleHandle& Particle0, const FGeometryParticleHandle& Particle1, const FReal Dt)
	{
		// For rigids, compute DeltaX from the V * Dt and use Q for the rotation.
		// For non-rigids, DeltaX is zero and use R for rotation.
		const auto Rigid0 = Particle0.CastToRigidParticle();
		const auto Rigid1 = Particle1.CastToRigidParticle();
		const FVec3 DeltaX0 = Rigid0 ? Rigid0->V() * Dt : FVec3::ZeroVector;
		const FVec3 DeltaX1 = Rigid1 ? Rigid1->V() * Dt : FVec3::ZeroVector;
		const FQuat& R0 = Rigid0 ? Rigid0->Q() : Particle0.R();
		const FQuat& R1 = Rigid1 ? Rigid1->Q() : Particle1.R();
		return DeltaExceedsThreshold(
			Particle0.CCDAxisThreshold(), DeltaX0, R0,
			Particle1.CCDAxisThreshold(), DeltaX1, R1);
	}
}