#pragma once

#include "CoreMinimal.h"
#include "TankControlIntent.h"

namespace NetworkTankArena::LocalTank
{
	bool IsFiniteVector(const FVector& Value);
	bool IsValidDeltaTime(float DeltaTime);
	float SanitizeAxis(float Value);
	float NonNegativeFinite(float Value);
	float NormalizeYaw(double Yaw);
	FTankControlIntent SanitizeIntent(const FTankControlIntent& Intent);

	float CalculateChassisYaw(float CurrentYaw, float Turn, float TurnRate, float DeltaTime);
	FVector CalculateMovementDelta(float Throttle, float ForwardSpeed, float ReverseSpeed,
		float ChassisYaw, float DeltaTime);
	bool TryIntersectAimPlane(const FVector& RayOrigin, const FVector& RayDirection,
		double PlaneZ, FVector& OutAimPoint);
	bool TryCalculateAimYaw(const FVector& GameplayPosition, const FVector& AimPoint, float& OutYaw);
	float ResolveTurretWorldYaw(float PreviousYaw, const FVector& GameplayPosition,
		const FVector& AimPoint);
	float CalculateRelativeTurretYaw(float ChassisYaw, float TurretWorldYaw);
}
