#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TankControlIntent.h"
#include "TankMovementCommand.h"
#include "TankMovementState.h"
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

	// Development cheats for testing reconciliation. They only work on a remote client and only change its own side.
	// CheatSpeed <Multiplier>: predicts with a scaled speed while sending honest commands, so the server corrects it.
	UFUNCTION(Exec)
	void CheatSpeed(float Multiplier);
	// CheatTeleport <Distance>: moves the tank forward locally. The next acknowledgement pulls it back.
	UFUNCTION(Exec)
	void CheatTeleport(float Distance);
	// CheatDeltaTime <Multiplier>: sends and predicts inflated command times. The server accepts them up to its
	// 0.1 s cap, so this speed hack is never corrected.
	UFUNCTION(Exec)
	void CheatDeltaTime(float Multiplier);
	UFUNCTION(Exec)
	void CheatReset();

protected:
	virtual void BeginPlay() override;
	virtual void ReceivedPlayer() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FTankMultiplayerAuthorityTest;
	friend class FTankMultiplayerLifecycleTest;
	friend class FTankMultiplayerContentTest;
	friend class UTankCommandSubsystem;

	UFUNCTION(Server, Unreliable)
	void ServerSubmitMovementCommands(ATankPawn* TargetTank, const TArray<FTankMovementCommand>& Commands);
	UFUNCTION(Client, Unreliable)
	void ClientAcknowledgeCommands(ATankPawn* Tank, uint32 LastProcessedSequence, const FTankMovementState& State);

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

	ATankPawn* FindCheatTank(const TCHAR* Command) const;
	void ReportCheat(const FString& Line) const;
	float CheatDeltaTimeMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> TurnAction;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputMappingContext> TankMappingContext;

	float CachedThrottle = 0.f;
	float CachedTurn = 0.f;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;
};
