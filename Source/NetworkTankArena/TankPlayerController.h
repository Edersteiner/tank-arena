#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TankControlIntent.h"
#include "TankPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;
class ATankPawn;

UCLASS()
class NETWORKTANKARENA_API ATankPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void FlushPressedKeys() override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void Reset() override;

	// Small localhost lab flow, also callable from a Blueprint widget or the console.
	UFUNCTION(Exec, BlueprintCallable, Category = "Tank|Localhost")
	void HostLocalhost();
	UFUNCTION(Exec, BlueprintCallable, Category = "Tank|Localhost")
	void JoinLocalhost();

	// Development measurement: MeasureDrive <Seconds> <Runs>. Drives the local tank by script through the
	// normal input path and reports how far it actually moved versus the time the throttle was held.
	UFUNCTION(Exec)
	void MeasureDrive(float Seconds, int32 Runs);

protected:
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FTankMultiplayerAuthorityTest;
	friend class FTankMultiplayerLifecycleTest;
	friend class FTankMultiplayerContentTest;

	UFUNCTION(Server, Unreliable)
	void ServerSubmitControlIntent(ATankPawn* TargetTank, FTankControlIntent Intent);
	void SubmitLocalControlIntent(ATankPawn* Tank, const FTankControlIntent& Intent);
	void AcceptControlIntent(ATankPawn* TargetTank, const FTankControlIntent& Intent);

	void UpdateThrottle(const FInputActionValue& Value);
	void UpdateTurn(const FInputActionValue& Value);
	void ClearThrottle();
	void ClearTurn();
	void ClearCachedInput();
	void ApplyTankInputContext();
	void RemoveTankInputContext();

	enum class EDriveMeasurePhase : uint8 { Idle, Settle, Drive, Coast };
	struct FDriveMeasurement
	{
		EDriveMeasurePhase Phase = EDriveMeasurePhase::Idle;
		TWeakObjectPtr<ATankPawn> Tank;
		float DriveSeconds = 0.f;
		int32 RunCount = 0;
		int32 RunIndex = 0;
		float Throttle = 0.f;
		float PhaseSeconds = 0.f;
		float StableSeconds = 0.f;
		float HeldSeconds = 0.f;
		FVector LastLocation = FVector::ZeroVector;
		FVector StartLocation = FVector::ZeroVector;
		FVector StartForward = FVector::ForwardVector;
		TArray<double> ErrorPercents;
	};
	void ApplyDriveMeasurement(ATankPawn* Tank, float DeltaTime, FTankControlIntent& Intent);
	void FinishDriveMeasurementRun(ATankPawn* Tank);
	void StopDriveMeasurement(const TCHAR* Reason);
	void ReportDriveMeasurement(const FString& Line) const;
	FDriveMeasurement DriveMeasurement;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> TurnAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputMappingContext> TankMappingContext;

	float CachedThrottle = 0.f;
	float CachedTurn = 0.f;
	static constexpr float InputSendIntervalSeconds = 1.f / 20.f;
	float SecondsUntilInputSend = 0.f;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;
};
