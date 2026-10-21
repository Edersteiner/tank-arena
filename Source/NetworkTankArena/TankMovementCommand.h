#pragma once

#include "CoreMinimal.h"
#include "TankControlIntent.h"

#include "TankMovementCommand.generated.h"
// Wraps FTankControlIntent with required metadata
// for prediction and reconciliation
USTRUCT()
struct NETWORKTANKARENA_API FTankMovementCommand
{
	GENERATED_BODY()
	
	UPROPERTY()
	float DeltaTime = 0.f;
	UPROPERTY()
	uint32 SequenceNumber = 0;
	UPROPERTY()
	FTankControlIntent Intent;
};
