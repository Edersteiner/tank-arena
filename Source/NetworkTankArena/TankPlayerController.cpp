#include "TankPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TankGameplayMath.h"
#include "TankPawn.h"
#include "UnrealClient.h"

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
	SecondsUntilInputSend = 0.f;
	if (ATankPawn* Tank = Cast<ATankPawn>(GetPawn()))
	{
		if (HasAuthority())
		{
			Tank->ClearControlIntent();
		}
		else if (IsLocalController())
		{
			// A lost neutral packet is repaired by the following periodic heartbeat.
			SubmitLocalControlIntent(Tank, FTankControlIntent{});
		}
	}
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
	if (Viewport && Viewport->HasFocus())
	{
		Intent.Throttle = CachedThrottle;
		Intent.Turn = CachedTurn;
		FVector RayOrigin;
		FVector RayDirection;
		if (DeprojectMousePositionToWorld(RayOrigin, RayDirection))
		{
			Intent.bHasAimWorldPoint = NetworkTankArena::LocalTank::TryIntersectAimPlane(
				RayOrigin, RayDirection, Tank->GetGameplayPlaneZ(), Intent.AimWorldPoint);
		}
	}
	else
	{
		ClearThrottle();
		ClearTurn();
	}
	if (HasAuthority())
	{
		AcceptControlIntent(Tank, Intent); // Listen host/standalone uses the same acceptance path.
	}
	else if (NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		SecondsUntilInputSend -= DeltaTime;
		if (SecondsUntilInputSend <= UE_SMALL_NUMBER)
		{
			SubmitLocalControlIntent(Tank, Intent);
			// No catch-up burst after a slow frame. Send full current state even when neutral.
			SecondsUntilInputSend = InputSendIntervalSeconds;
		}
	}
}

void ATankPlayerController::SubmitLocalControlIntent(ATankPawn* Tank, const FTankControlIntent& Intent)
{
	if (IsLocalController() && IsValid(Tank) && GetPawn() == Tank && Tank->GetController() == this)
	{
		ServerSubmitControlIntent(Tank, NetworkTankArena::LocalTank::SanitizeIntent(Intent));
	}
}

void ATankPlayerController::ServerSubmitControlIntent_Implementation(ATankPawn* TargetTank, FTankControlIntent Intent)
{
	AcceptControlIntent(TargetTank, Intent);
}

void ATankPlayerController::AcceptControlIntent(ATankPawn* TargetTank, const FTankControlIntent& Intent)
{
	// The RPC is routed through the owned controller. Never let its caller name another
	// player's tank, or let an in-flight update for an old pawn affect a new possession.
	if (HasAuthority() && IsValid(TargetTank) && TargetTank->HasAuthority()
		&& GetPawn() == TargetTank && TargetTank->GetController() == this)
	{
		TargetTank->SubmitControlIntent(Intent); // Pawn sanitizes again at the authority boundary.
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

void ATankPlayerController::SetPawn(APawn* InPawn)
{
	ClearCachedInput();
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
	RemoveTankInputContext();
	Super::EndPlay(EndPlayReason);
}
