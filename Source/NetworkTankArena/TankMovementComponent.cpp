#include "TankMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "TankGameplayMath.h"

UTankMovementComponent::UTankMovementComponent()
{
	// Simulation is synchronous inside ATankPawn::Tick, never a second component tick.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoUpdateTickRegistration = false;
	bTickBeforeOwner = false;
	SetPlaneConstraintNormal(FVector::UpVector);
	SetPlaneConstraintEnabled(true);
}

void UTankMovementComponent::InitializeGameplayPlane()
{
	if (IsValid(UpdatedComponent) && NetworkTankArena::LocalTank::IsFiniteVector(UpdatedComponent->GetComponentLocation()))
	{
		SetPlaneConstraintNormal(FVector::UpVector);
		SetPlaneConstraintOrigin(UpdatedComponent->GetComponentLocation());
		SetPlaneConstraintEnabled(true);
		bGameplayPlaneInitialized = true;
	}
}

float UTankMovementComponent::GetMaxSpeed() const
{
	using namespace NetworkTankArena::LocalTank;
	return FMath::Max(NonNegativeFinite(ForwardSpeed), NonNegativeFinite(ReverseSpeed));
}

float UTankMovementComponent::GetForwardSpeed() const
{
	return NetworkTankArena::LocalTank::NonNegativeFinite(ForwardSpeed);
}

float UTankMovementComponent::GetReverseSpeed() const
{
	return NetworkTankArena::LocalTank::NonNegativeFinite(ReverseSpeed);
}

void UTankMovementComponent::SimulateMovement(const FTankControlIntent& Intent, float DeltaTime)
{
	using namespace NetworkTankArena::LocalTank;
	if (!IsValid(PawnOwner) || !PawnOwner->HasAuthority())
	{
		return; // Clients consume Actor replication; they never simulate final gameplay transforms.
	}
	Velocity = FVector::ZeroVector;
	if (!IsValid(UpdatedPrimitive) || !bGameplayPlaneInitialized
		|| !IsValidDeltaTime(DeltaTime) || UpdatedPrimitive->IsSimulatingPhysics())
	{
		UpdateComponentVelocity();
		return;
	}

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	if (!IsFiniteVector(OldLocation))
	{
		UpdateComponentVelocity();
		return;
	}
	UpdatedComponent->SetWorldScale3D(FVector::OneVector);
	const float Yaw = CalculateChassisYaw(UpdatedComponent->GetComponentRotation().Yaw,
		Intent.Turn, TurnRateDegreesPerSecond, DeltaTime);
	const FQuat Rotation = FRotator(0.0, Yaw, 0.0).Quaternion();

	// The upright, unit-scaled capsule is yaw-invariant. A box would need rotation collision handling.
	FHitResult Hit;
	SafeMoveUpdatedComponent(FVector::ZeroVector, Rotation, true, Hit);
	const FVector Delta = CalculateMovementDelta(Intent.Throttle, ForwardSpeed, ReverseSpeed,
		UpdatedComponent->GetComponentRotation().Yaw, DeltaTime);
	SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		HandleImpact(Hit, DeltaTime, Delta);
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
	const FVector ActualVelocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	Velocity = IsFiniteVector(ActualVelocity) ? ActualVelocity : FVector::ZeroVector;
	UpdateComponentVelocity();
}
