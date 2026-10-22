#include "TankPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TankCommandSubsystem.h"
#include "TankGameplayMath.h"
#include "TankMovementComponent.h"
#include "TankPawn.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTankMeasure, Log, All);

namespace
{
	constexpr float MeasureStableSeconds = 0.5f;
	constexpr double MeasureStableToleranceCm = 0.5;
	constexpr float MeasureMinCoastSeconds = 1.f;
	constexpr float MeasurePhaseTimeoutSeconds = 15.f;
}

void ATankPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyTankInputContext();
}

void ATankPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	ApplyTankInputContext();
}

void ATankPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!IsLocalController())
	{
		return;
	}
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		return;
	}
	if (ThrottleAction)
	{
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ATankPlayerController::UpdateThrottle);
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ATankPlayerController::ClearThrottle);
		EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Canceled, this, &ATankPlayerController::ClearThrottle);
	}
	if (TurnAction)
	{
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Triggered, this, &ATankPlayerController::UpdateTurn);
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Completed, this, &ATankPlayerController::ClearTurn);
		EnhancedInput->BindAction(TurnAction, ETriggerEvent::Canceled, this, &ATankPlayerController::ClearTurn);
	}
}

void ATankPlayerController::UpdateThrottle(const FInputActionValue& Value)
{
	CachedThrottle = IsLocalController() && Value.GetValueType() == EInputActionValueType::Axis1D
		? NetworkTankArena::LocalTank::SanitizeAxis(Value.Get<float>()) : 0.f;
}

void ATankPlayerController::UpdateTurn(const FInputActionValue& Value)
{
	CachedTurn = IsLocalController() && Value.GetValueType() == EInputActionValueType::Axis1D
		? NetworkTankArena::LocalTank::SanitizeAxis(Value.Get<float>()) : 0.f;
}

void ATankPlayerController::ClearThrottle()
{
	CachedThrottle = 0.f;
}

void ATankPlayerController::ClearTurn()
{
	CachedTurn = 0.f;
}

void ATankPlayerController::ClearCachedInput()
{
	ClearThrottle();
	ClearTurn();
}

void ATankPlayerController::FlushPressedKeys()
{
	Super::FlushPressedKeys();
	ClearCachedInput();
}

void ATankPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime); // Processes this frame's Enhanced Input before assembling intent.
	ATankPawn* Tank = Cast<ATankPawn>(GetPawn());
	if (!IsLocalController() || !IsValid(Tank) || Tank->GetController() != this)
	{
		return;
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FViewport* Viewport = LocalPlayer && LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
	FTankControlIntent Intent;
	// Set new intent's yaw to old one, to avoid resetting back to 0 if mouse leaves viewport
	Intent.AimYaw = Tank->GetCanonicalTurretWorldYaw();
	if (Viewport && Viewport->HasFocus())
	{
		Intent.Throttle = CachedThrottle;
		Intent.Turn = CachedTurn;
		FVector RayOrigin;
		FVector RayDirection;
		if (DeprojectMousePositionToWorld(RayOrigin, RayDirection))
		{
			FVector AimPoint;
			if (NetworkTankArena::LocalTank::TryIntersectAimPlane(RayOrigin, RayDirection, Tank->GetGameplayPlaneZ(), AimPoint))
			{
				Intent.AimYaw = NetworkTankArena::LocalTank::ResolveTurretWorldYaw(Intent.AimYaw, Tank->GetActorLocation(), AimPoint);
				Intent.bHasAimYaw = true;
			}
		}
	}
	else
	{
		ClearThrottle();
		ClearTurn();
	}
	if (DriveMeasurement.Phase != EDriveMeasurePhase::Idle)
	{
		ApplyDriveMeasurement(Tank, DeltaTime, Intent);
	}
	if (UTankCommandSubsystem* Commands = UWorld::GetSubsystem<UTankCommandSubsystem>(GetWorld()))
	{
		Commands->RecordLocalCommand(*this, *Tank, Intent, DeltaTime);
	}
}

void ATankPlayerController::ServerSubmitMovementCommands_Implementation(ATankPawn* TargetTank,
	const TArray<FTankMovementCommand>& Commands)
{
	if (UTankCommandSubsystem* CommandSubsystem = UWorld::GetSubsystem<UTankCommandSubsystem>(GetWorld()))
	{
		CommandSubsystem->ReceiveCommands(*this, TargetTank, Commands);
	}
}

void ATankPlayerController::AcknowledgePossession(APawn* P)
{
	ClearCachedInput();
	Super::AcknowledgePossession(P);
	ApplyTankInputContext();
}

void ATankPlayerController::Reset()
{
	ClearCachedInput();
	RemoveTankInputContext();
	Super::Reset();
}

void ATankPlayerController::HostLocalhost()
{
	if (IsLocalController() && GetWorld() && GetNetMode() == NM_Standalone)
	{
		ClearCachedInput();
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/TestMap")), true, TEXT("listen"));
	}
}

void ATankPlayerController::JoinLocalhost()
{
	if (IsLocalController() && GetWorld() && GetNetMode() == NM_Standalone)
	{
		ClearCachedInput();
		ClientTravel(TEXT("127.0.0.1:7777"), TRAVEL_Absolute);
	}
}

void ATankPlayerController::MeasureDrive(float Seconds, int32 Runs)
{
#if !UE_BUILD_SHIPPING
	ATankPawn* Tank = Cast<ATankPawn>(GetPawn());
	if (!IsLocalController() || !IsValid(Tank))
	{
		ReportDriveMeasurement(TEXT("MeasureDrive: no locally controlled tank."));
		return;
	}
	StopDriveMeasurement(TEXT("restarted"));
	DriveMeasurement.Tank = Tank;
	DriveMeasurement.DriveSeconds = FMath::Clamp(Seconds > 0.f ? Seconds : 2.f, 0.1f, 30.f);
	DriveMeasurement.RunCount = FMath::Clamp(Runs > 0 ? Runs : 1, 1, 50);
	DriveMeasurement.LastLocation = Tank->GetActorLocation();
	DriveMeasurement.Phase = EDriveMeasurePhase::Settle;
	ReportDriveMeasurement(FString::Printf(TEXT("MeasureDrive: %d run(s) of %.2f s, alternating forward/reverse."),
		DriveMeasurement.RunCount, DriveMeasurement.DriveSeconds));
#endif
}

void ATankPlayerController::ApplyDriveMeasurement(ATankPawn* Tank, float DeltaTime, FTankControlIntent& Intent)
{
	FDriveMeasurement& Measure = DriveMeasurement;
	if (Tank != Measure.Tank.Get())
	{
		StopDriveMeasurement(TEXT("possessed tank changed"));
		return;
	}
	// Scripted input replaces the keyboard but still leaves through the normal submission path.
	Intent.Throttle = 0.f;
	Intent.Turn = 0.f;
	if (!NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		return;
	}

	// On a client this is the replicated position, so it only settles once the server's tank has stopped.
	const FVector Location = Tank->GetActorLocation();
	Measure.StableSeconds = FVector::Dist2D(Location, Measure.LastLocation) <= MeasureStableToleranceCm
		? Measure.StableSeconds + DeltaTime : 0.f;
	Measure.LastLocation = Location;
	Measure.PhaseSeconds += DeltaTime;

	if (Measure.Phase == EDriveMeasurePhase::Settle && Measure.StableSeconds >= MeasureStableSeconds)
	{
		// Alternate direction so repeated runs stay in the same part of the map.
		Measure.Throttle = Measure.RunIndex % 2 == 0 ? 1.f : -1.f;
		Measure.StartLocation = Location;
		Measure.StartForward = FRotator(0.0, Tank->GetActorRotation().Yaw, 0.0).Vector();
		Measure.HeldSeconds = 0.f;
		Measure.PhaseSeconds = 0.f;
		Measure.Phase = EDriveMeasurePhase::Drive;
	}
	if (Measure.Phase == EDriveMeasurePhase::Drive && Measure.HeldSeconds >= Measure.DriveSeconds)
	{
		Measure.StableSeconds = 0.f;
		Measure.PhaseSeconds = 0.f;
		Measure.Phase = EDriveMeasurePhase::Coast;
	}
	if (Measure.Phase == EDriveMeasurePhase::Coast)
	{
		// Leave room for the final commands to arrive and the last movement update to come back.
		const float PingSeconds = PlayerState ? PlayerState->GetPingInMilliseconds() / 1000.f : 0.f;
		if (Measure.PhaseSeconds >= MeasureMinCoastSeconds + 2.f * PingSeconds && Measure.StableSeconds >= MeasureStableSeconds)
		{
			FinishDriveMeasurementRun(Tank);
			return;
		}
	}

	if (Measure.Phase == EDriveMeasurePhase::Drive)
	{
		// The time counted here is exactly the time this player asked the tank to drive.
		Intent.Throttle = Measure.Throttle;
		Measure.HeldSeconds += DeltaTime;
	}
	else if (Measure.PhaseSeconds > MeasurePhaseTimeoutSeconds)
	{
		StopDriveMeasurement(TEXT("tank never came to rest"));
	}
}

void ATankPlayerController::FinishDriveMeasurementRun(ATankPawn* Tank)
{
	FDriveMeasurement& Measure = DriveMeasurement;
	const UTankMovementComponent* Movement = Cast<UTankMovementComponent>(Tank->GetMovementComponent());
	const double Speed = Movement ? (Measure.Throttle > 0.f ? Movement->GetForwardSpeed() : Movement->GetReverseSpeed()) : 0.0;
	FVector Delta = Measure.LastLocation - Measure.StartLocation;
	Delta.Z = 0.0;
	const double Forward = FVector::DotProduct(Delta, Measure.StartForward);
	const double Travelled = Forward * Measure.Throttle;
	const double Lateral = (Delta - Measure.StartForward * Forward).Size();
	const double Expected = Speed * Measure.HeldSeconds;
	const double ErrorCm = Travelled - Expected;
	const double ErrorPercent = Expected > UE_KINDA_SMALL_NUMBER ? 100.0 * ErrorCm / Expected : 0.0;
	Measure.ErrorPercents.Add(ErrorPercent);

	FString NetInfo = TEXT("local, no connection");
	if (const UNetConnection* Connection = GetNetConnection())
	{
		NetInfo = FString::Printf(TEXT("ping %.0f ms, loss in %.1f%% out %.1f%%"),
			PlayerState ? PlayerState->GetPingInMilliseconds() : 0.f,
			100.f * Connection->GetInLossPercentage().GetAvgLossPercentage(),
			100.f * Connection->GetOutLossPercentage().GetAvgLossPercentage());
	}
	ReportDriveMeasurement(FString::Printf(
		TEXT("Run %d/%d %s: held %.3f s, expected %.1f cm, travelled %.1f cm, error %+.1f cm (%+.2f%%), lateral %.1f cm | %s"),
		Measure.RunIndex + 1, Measure.RunCount, Measure.Throttle > 0.f ? TEXT("forward") : TEXT("reverse"),
		Measure.HeldSeconds, Expected, Travelled, ErrorCm, ErrorPercent, Lateral, *NetInfo));

	++Measure.RunIndex;
	if (Measure.RunIndex < Measure.RunCount)
	{
		Measure.PhaseSeconds = 0.f;
		Measure.Phase = EDriveMeasurePhase::Settle;
		return;
	}
	double Min = Measure.ErrorPercents[0];
	double Max = Min;
	double Sum = 0.0;
	double AbsSum = 0.0;
	for (const double Error : Measure.ErrorPercents)
	{
		Min = FMath::Min(Min, Error);
		Max = FMath::Max(Max, Error);
		Sum += Error;
		AbsSum += FMath::Abs(Error);
	}
	const int32 Count = Measure.ErrorPercents.Num();
	ReportDriveMeasurement(FString::Printf(TEXT("Summary over %d run(s): error avg %+.2f%%, avg abs %.2f%%, min %+.2f%%, max %+.2f%%"),
		Count, Sum / Count, AbsSum / Count, Min, Max));
	DriveMeasurement = FDriveMeasurement{};
}

void ATankPlayerController::StopDriveMeasurement(const TCHAR* Reason)
{
	if (DriveMeasurement.Phase != EDriveMeasurePhase::Idle)
	{
		ReportDriveMeasurement(FString::Printf(TEXT("MeasureDrive aborted: %s."), Reason));
	}
	DriveMeasurement = FDriveMeasurement{};
}

void ATankPlayerController::ReportDriveMeasurement(const FString& Line) const
{
	const TCHAR* Machine = GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Host");
	UE_LOG(LogTankMeasure, Display, TEXT("[%s] %s"), Machine, *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Cyan, FString::Printf(TEXT("[%s] %s"), Machine, *Line));
	}
}

void ATankPlayerController::SetPawn(APawn* InPawn)
{
	ClearCachedInput();
	if (UTankCommandSubsystem* Commands = UWorld::GetSubsystem<UTankCommandSubsystem>(GetWorld()))
	{
		Commands->ResetLocalCommands(*this);
	}
	RemoveTankInputContext();
	Super::SetPawn(InPawn);
	ApplyTankInputContext();
}

void ATankPlayerController::ApplyTankInputContext()
{
	if (!IsLocalController() || !Cast<ATankPawn>(GetPawn()))
	{
		return;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return;
	}
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem || !TankMappingContext)
	{
		return; // Asset references are intentionally supplied by a later Blueprint child.
	}
	if (MappingSubsystem.Get() == Subsystem && AppliedMappingContext == TankMappingContext)
	{
		return;
	}
	RemoveTankInputContext();
	Subsystem->AddMappingContext(TankMappingContext, 0);
	MappingSubsystem = Subsystem;
	AppliedMappingContext = TankMappingContext;
}

void ATankPlayerController::RemoveTankInputContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = MappingSubsystem.Get())
	{
		if (AppliedMappingContext)
		{
			Subsystem->RemoveMappingContext(AppliedMappingContext);
		}
	}
	MappingSubsystem.Reset();
	AppliedMappingContext = nullptr;
}

void ATankPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCachedInput();
	if (UTankCommandSubsystem* Commands = UWorld::GetSubsystem<UTankCommandSubsystem>(GetWorld()))
	{
		Commands->RemovePlayer(*this);
	}
	RemoveTankInputContext();
	Super::EndPlay(EndPlayReason);
}
