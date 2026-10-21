#if WITH_DEV_AUTOMATION_TESTS

#include "../TankGameMode.h"
#include "../TankMovementComponent.h"
#include "../TankPawn.h"
#include "../TankPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include <limits>

namespace
{
	// Real Unreal actors/components in a transient world. No simulated transport layer.
	struct FScopedTankTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		~FScopedTankTestWorld() { World->DestroyWorld(false); }

		ATankPawn* SpawnTank(const FVector& Location)
		{
			ATankPawn* Tank = World->SpawnActor<ATankPawn>(Location, FRotator::ZeroRotator);
			Tank->DispatchBeginPlay();
			return Tank;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerAuthorityTest, "NetworkTankArena.Multiplayer.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerAuthorityTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* Tank = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPawn* Other = Scope.SpawnTank(FVector(1000.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	Tank->SetAutonomousProxy(true); // Possession must deliberately undo autonomous promotion.
	Controller->Possess(Tank);
	TestTrue(TEXT("Actor and movement replication enabled"), Tank->GetIsReplicated() && Tank->IsReplicatingMovement());
	TestEqual(TEXT("Client pawn role receives ordinary FRepMovement, including owner"), Tank->GetRemoteRole(), ROLE_SimulatedProxy);

	FTankControlIntent Intent;
	Intent.Throttle = 4.f;
	Intent.Turn = std::numeric_limits<float>::infinity();
	Intent.AimYaw = 120.f;
	Intent.bHasAimYaw = true;
	Controller->AcceptControlIntent(Other, Intent);
	TestEqual(TEXT("Cannot submit for another tank"), Other->CurrentIntent.Throttle, 0.f);
	Controller->AcceptControlIntent(nullptr, Intent);
	Controller->AcceptControlIntent(Tank, Intent);
	TestEqual(TEXT("Authority clamps incoming throttle"), Tank->CurrentIntent.Throttle, 1.f);
	TestEqual(TEXT("Authority neutralizes non-finite turn"), Tank->CurrentIntent.Turn, 0.f);
	Tank->Tick(0.05f);
	TestTrue(TEXT("Server moves using existing movement component"), Tank->GetActorLocation().X > 0.0);
	TestTrue(TEXT("Server applies submitted turret yaw"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 120.f, 0.001f));
	Intent.AimYaw = std::numeric_limits<float>::quiet_NaN();
	Controller->AcceptControlIntent(Tank, Intent);
	Tank->Tick(0.05f);
	TestTrue(TEXT("Non-finite aim yaw keeps previous turret yaw"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 120.f, 0.001f));

	Tank->ClearControlIntent();
	Tank->Tick(0.05f);
	TestTrue(TEXT("Cleared intent leaves turret yaw unchanged"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 120.f, 0.001f));
	const FTransform Before = Tank->GetActorTransform();
	const float YawBefore = Tank->GetCanonicalTurretWorldYaw();
	Tank->SetRole(ROLE_SimulatedProxy);
	Tank->SubmitControlIntent(Intent);
	TestEqual(TEXT("Client cannot install gameplay intent"), Tank->CurrentIntent.Throttle, 0.f);
	Tank->TankMovement->SimulateMovement(Intent, 0.1f);
	Tank->Tick(0.1f);
	TestTrue(TEXT("Client tick/component cannot author gameplay transform"), Tank->GetActorTransform().Equals(Before));
	TestEqual(TEXT("Client cannot author canonical turret yaw"), Tank->GetCanonicalTurretWorldYaw(), YawBefore);
	Tank->CanonicalTurretWorldYaw = -75.f; // Exercise presentation notification, not a transport test.
	Tank->OnRep_CanonicalTurretWorldYaw();
	TestTrue(TEXT("Replicated yaw is applied in world space"), FMath::IsNearlyEqual(
		static_cast<float>(Tank->TurretYawPivot->GetComponentRotation().Yaw), -75.f, 0.001f));
	TestTrue(TEXT("Turret/hover presentation leaves gameplay transform unchanged"), Tank->GetActorTransform().Equals(Before));
	Tank->SetRole(ROLE_Authority);
	Controller->SetRole(ROLE_SimulatedProxy);
	Controller->AcceptControlIntent(Tank, Intent);
	TestEqual(TEXT("Non-authority controller cannot accept intent"), Tank->CurrentIntent.Throttle, 0.f);
	Controller->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerTurretSpawnTest, "NetworkTankArena.Multiplayer.TurretSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerTurretSpawnTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	// Away from the world origin, so aiming at the origin by accident gives a clearly different yaw.
	ATankPawn* Tank = Scope.World->SpawnActor<ATankPawn>(FVector(1000.0, 500.0, 100.0), FRotator(0.0, 30.0, 0.0));
	Tank->DispatchBeginPlay();
	TestTrue(TEXT("Turret starts aligned with spawn chassis yaw"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 30.f, 0.001f));
	Tank->Tick(0.05f);
	TestTrue(TEXT("Turret keeps spawn yaw until aim arrives"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 30.f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerLifecycleTest, "NetworkTankArena.Multiplayer.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerLifecycleTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* First = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPawn* Second = Scope.SpawnTank(FVector(1000.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	FTankControlIntent Intent;
	Intent.Throttle = 1.f;
	Intent.Turn = 1.f;
	Intent.AimYaw = 45.f;
	Intent.bHasAimYaw = true;
	Controller->Possess(First);
	Controller->AcceptControlIntent(First, Intent);
	First->Tick(0.05f);
	TestTrue(TEXT("Most recent intent is held between updates"), First->CurrentIntent.Throttle == 1.f);
	const FVector MovingPosition = First->GetActorLocation();
	First->Tick(0.5f);
	TestEqual(TEXT("Missing heartbeat clears throttle"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Missing heartbeat clears turn"), First->CurrentIntent.Turn, 0.f);
	TestTrue(TEXT("Expired intent cannot keep moving the tank"), First->GetActorLocation().Equals(MovingPosition));
	TestTrue(TEXT("Expired intent keeps last turret yaw"), FMath::IsNearlyEqual(First->GetCanonicalTurretWorldYaw(), 45.f, 0.001f));
	Controller->AcceptControlIntent(First, Intent);
	First->Restart();
	TestEqual(TEXT("Restart clears authoritative intent"), First->CurrentIntent.Throttle, 0.f);
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->AcknowledgePossession(First);
	TestEqual(TEXT("Possession acknowledgement clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Possession acknowledgement clears cached turn"), Controller->CachedTurn, 0.f);
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->SecondsUntilInputSend = 0.04f;
	Controller->AcceptControlIntent(First, Intent);
	Controller->FlushPressedKeys();
	TestEqual(TEXT("Flush clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Flush clears cached turn"), Controller->CachedTurn, 0.f);
	TestEqual(TEXT("Flush resets send cadence"), Controller->SecondsUntilInputSend, 0.f);
	TestEqual(TEXT("Flush clears server intent"), First->CurrentIntent.Throttle, 0.f);
	Controller->AcceptControlIntent(First, Intent);
	Controller->Possess(Second);
	TestEqual(TEXT("Unpossessed tank has neutral intent"), First->CurrentIntent.Throttle, 0.f);
	TestFalse(TEXT("Unpossessed tank drops tick controller"), First->TickController.IsValid());
	TestTrue(TEXT("New tank uses current controller as prerequisite"), Second->TickController.Get() == Controller);
	Controller->AcceptControlIntent(First, Intent);
	TestEqual(TEXT("Late submission for previous pawn is ignored"), First->CurrentIntent.Throttle, 0.f);
	TestEqual(TEXT("Late submission cannot move new pawn"), Second->CurrentIntent.Throttle, 0.f);
	Controller->UnPossess();
	TestFalse(TEXT("Unpossession removes new tick prerequisite"), Second->TickController.IsValid());
	TestFalse(TEXT("Unpossession removes input context bookkeeping"), Controller->MappingSubsystem.IsValid());
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		Controller->Possess(First);
		Controller->AcceptControlIntent(First, Intent);
		Controller->UnPossess();
		TestEqual(TEXT("Repeated possession clears intent"), First->CurrentIntent.Throttle, 0.f);
		TestFalse(TEXT("Repeated possession drops old prerequisite"), First->TickController.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerContentTest, "NetworkTankArena.Multiplayer.ContentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerContentTest::RunTest(const FString& Parameters)
{
	const ATankGameMode* Mode = GetDefault<ATankGameMode>();
	TestEqual(TEXT("GameMode reuses visual Blueprint"), Mode->DefaultPawnClass->GetPathName(), FString(TEXT("/Game/Blueprints/BP_Tank.BP_Tank_C")));
	TestEqual(TEXT("GameMode reuses input Blueprint"), Mode->PlayerControllerClass->GetPathName(), FString(TEXT("/Game/Blueprints/BP_TankPlayerController.BP_TankPlayerController_C")));
	const ATankPawn* Tank = Cast<ATankPawn>(Mode->DefaultPawnClass->GetDefaultObject());
	TestTrue(TEXT("Blueprint inherits replication defaults"), Tank && Tank->GetIsReplicated() && Tank->IsReplicatingMovement());
	TestTrue(TEXT("Blueprint defers spawning/possession to GameMode"), Tank && Tank->AutoPossessPlayer == EAutoReceiveInput::Disabled && Tank->AutoPossessAI == EAutoPossessAI::Disabled);
	TestTrue(TEXT("Visual hierarchy remains unreplicated"), Tank && !Tank->VisualRoot->GetIsReplicated() && !Tank->ChassisVisualPivot->GetIsReplicated() && !Tank->TurretYawPivot->GetIsReplicated());
	TestTrue(TEXT("Camera remains orthographic beneath the gameplay root"), Tank && Tank->FollowCamera->ProjectionMode == ECameraProjectionMode::Orthographic && Tank->CameraBoom->GetAttachParent() == Tank->GetRootComponent() && Tank->CameraBoom->IsUsingAbsoluteRotation());
	const ATankPlayerController* Controller = Cast<ATankPlayerController>(Mode->PlayerControllerClass->GetDefaultObject());
	if (TestNotNull(TEXT("Controller Blueprint derives from native tank controller"), Controller))
	{
		TestTrue(TEXT("Throttle action is assigned as Axis1D"), Controller->ThrottleAction && Controller->ThrottleAction->ValueType == EInputActionValueType::Axis1D);
		TestTrue(TEXT("Turn action is assigned as Axis1D"), Controller->TurnAction && Controller->TurnAction->ValueType == EInputActionValueType::Axis1D);
		TestNotNull(TEXT("Tank mapping context is assigned"), Controller->TankMappingContext.Get());
		TestTrue(TEXT("Local possession manages the view target"), Controller->bAutoManageActiveCameraTarget);
	}
	FString DefaultMode;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), DefaultMode, GEngineIni);
	TestEqual(TEXT("Project selects native GameMode"), DefaultMode, FString(TEXT("/Script/NetworkTankArena.TankGameMode")));
	UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/TestMap.TestMap"));
	if (!TestNotNull(TEXT("TestMap loads"), Map))
	{
		return false;
	}
	int32 Starts = 0;
	int32 Tanks = 0;
	for (AActor* Actor : Map->PersistentLevel->Actors)
	{
		Starts += Actor && Actor->IsA<APlayerStart>() ? 1 : 0;
		Tanks += Actor && Actor->IsA<ATankPawn>() ? 1 : 0;
	}
	TestEqual(TEXT("TestMap contains no preplaced duplicate tanks"), Tanks, 0);
	const TSubclassOf<AGameModeBase> MapMode = Map->GetWorldSettings()->DefaultGameMode;
	TestTrue(TEXT("TestMap allows native project GameMode"), !MapMode || MapMode->IsChildOf(ATankGameMode::StaticClass()));
	AddInfo(FString::Printf(TEXT("TestMap inventory: %d PlayerStarts. Two separated starts are required for manual acceptance."), Starts));
	return true;
}

#endif
