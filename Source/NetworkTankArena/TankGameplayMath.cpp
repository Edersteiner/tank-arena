#include "TankGameplayMath.h"

namespace NetworkTankArena::LocalTank
{
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsValidDeltaTime(float DeltaTime)
	{
		return FMath::IsFinite(DeltaTime) && DeltaTime > 0.f;
	}

	float SanitizeAxis(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.f, 1.f) : 0.f;
	}

	float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(Value, 0.f) : 0.f;
	}

	float NormalizeYaw(double Yaw)
	{
		return FMath::IsFinite(Yaw) ? static_cast<float>(FRotator::NormalizeAxis(Yaw)) : 0.f;
	}

	FTankControlIntent SanitizeIntent(const FTankControlIntent& Intent)
	{
		FTankControlIntent Result;
		Result.Throttle = SanitizeAxis(Intent.Throttle);
		Result.Turn = SanitizeAxis(Intent.Turn);
		Result.AimYaw = NormalizeYaw(Intent.AimYaw);
		Result.bHasAimYaw = Intent.bHasAimYaw && FMath::IsFinite(Intent.AimYaw);
		return Result;
	}

	float CalculateChassisYaw(float CurrentYaw, float Turn, float TurnRate, float DeltaTime)
	{
		const float InitialYaw = NormalizeYaw(CurrentYaw);
		if (!IsValidDeltaTime(DeltaTime))
		{
			return InitialYaw;
		}
		return NormalizeYaw(InitialYaw + static_cast<double>(SanitizeAxis(Turn))
			* NonNegativeFinite(TurnRate) * DeltaTime);
	}

	FVector CalculateMovementDelta(float Throttle, float ForwardSpeed, float ReverseSpeed,
		float ChassisYaw, float DeltaTime)
	{
		if (!IsValidDeltaTime(DeltaTime))
		{
			return FVector::ZeroVector;
		}
		const float Axis = SanitizeAxis(Throttle);
		const double Speed = NonNegativeFinite(Axis >= 0.f ? ForwardSpeed : ReverseSpeed);
		return FRotator(0.0, NormalizeYaw(ChassisYaw), 0.0).Vector() * (Axis * Speed * DeltaTime);
	}

	bool TryIntersectAimPlane(const FVector& RayOrigin, const FVector& RayDirection,
		double PlaneZ, FVector& OutAimPoint)
	{
		if (!IsFiniteVector(RayOrigin) || !IsFiniteVector(RayDirection) || !FMath::IsFinite(PlaneZ))
		{
			return false;
		}
		const FVector Direction = RayDirection.GetSafeNormal();
		if (!IsFiniteVector(Direction) || Direction.IsNearlyZero() || FMath::Abs(Direction.Z) < 1.e-4)
		{
			return false;
		}
		const double Distance = (PlaneZ - RayOrigin.Z) / Direction.Z;
		if (!FMath::IsFinite(Distance) || Distance < 0.0)
		{
			return false;
		}
		const FVector Point = RayOrigin + Direction * Distance;
		if (!IsFiniteVector(Point))
		{
			return false;
		}
		OutAimPoint = Point;
		return true;
	}

	bool TryCalculateAimYaw(const FVector& GameplayPosition, const FVector& AimPoint, float& OutYaw)
	{
		if (!IsFiniteVector(GameplayPosition) || !IsFiniteVector(AimPoint))
		{
			return false;
		}
		FVector Direction = AimPoint - GameplayPosition;
		Direction.Z = 0.0;
		if (!IsFiniteVector(Direction) || Direction.IsNearlyZero())
		{
			return false;
		}
		OutYaw = NormalizeYaw(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
		return true;
	}

	float ResolveTurretWorldYaw(float PreviousYaw, const FVector& GameplayPosition, const FVector& AimPoint)
	{
		float Result = NormalizeYaw(PreviousYaw);
		TryCalculateAimYaw(GameplayPosition, AimPoint, Result);
		return Result;
	}

	float CalculateRelativeTurretYaw(float ChassisYaw, float TurretWorldYaw)
	{
		return FMath::FindDeltaAngleDegrees(NormalizeYaw(ChassisYaw), NormalizeYaw(TurretWorldYaw));
	}
}
