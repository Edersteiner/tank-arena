#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "TankMovementComponent.generated.h"

struct FTankControlIntent;

UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class NETWORKTANKARENA_API UTankMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UTankMovementComponent();
	void InitializeGameplayPlane();
	void SimulateMovement(const FTankControlIntent& Intent, float DeltaTime);
	double GetGameplayPlaneZ() const { return GetPlaneConstraintOrigin().Z; }
	virtual float GetMaxSpeed() const override;
	float GetForwardSpeed() const;
	float GetReverseSpeed() const;
	float GetTurnRateDegreesPerSecond() const { return TurnRateDegreesPerSecond; }
	// Development cheat. Scales speed on this machine's copy only, so a client's prediction disagrees with the server.
	void SetCheatSpeedMultiplier(float Multiplier);
	float GetCheatSpeedMultiplier() const { return CheatSpeedMultiplier; }

private:
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float ForwardSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float ReverseSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Movement", meta = (ClampMin = "0", Units = "deg/s"))
	float TurnRateDegreesPerSecond = 120.f;

	bool bGameplayPlaneInitialized = false;
	float CheatSpeedMultiplier = 1.f;
};
