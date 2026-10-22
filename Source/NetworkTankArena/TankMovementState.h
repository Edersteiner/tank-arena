#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"

#include "TankMovementState.generated.h"

// The gameplay transform that movement commands change. The server sends it with each
// acknowledgement so the owning client knows where its tank is after that command.
USTRUCT()
struct NETWORKTANKARENA_API FTankMovementState
{
	GENERATED_BODY()

	// Two decimals of a centimetre, so the acknowledgement stays small.
	UPROPERTY()
	FVector_NetQuantize100 Location = FVector_NetQuantize100(ForceInit);
	UPROPERTY()
	float ChassisYaw = 0.f;
};
