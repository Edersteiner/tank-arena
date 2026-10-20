#pragma once

#include "CoreMinimal.h"
#include "TankControlIntent.generated.h"

// Gameplay-facing controls: no device, viewport, or controller dependencies.
USTRUCT()
struct NETWORKTANKARENA_API FTankControlIntent
{
	GENERATED_BODY()

	UPROPERTY()
	float Throttle = 0.f;
	UPROPERTY()
	float Turn = 0.f;
	UPROPERTY()
	FVector AimWorldPoint = FVector::ZeroVector;
	UPROPERTY()
	bool bHasAimWorldPoint = false;
};
