#if WITH_DEV_AUTOMATION_TESTS

#include "../TankCommandSubsystem.h"
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

	FTankMovementCommand MakeCommand(uint32 SequenceNumber, float DeltaTime, const FTankControlIntent& Intent)
	{
		FTankMovementCommand Command;
		Command.SequenceNumber = SequenceNumber;
		Command.DeltaTime = DeltaTime;
		Command.Intent = Intent;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerAuthorityTest, "NetworkTankArena.Multiplayer.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerAuthorityTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* Tank = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPawn* Other = Scope.SpawnTank(FVector(1000.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	UTankCommandSubsystem* Commands = Scope.World->GetSubsystem<UTankCommandSubsystem>();
	if (!TestNotNull(TEXT("Game worlds create the command subsystem"), Commands))
	{
		return false;
	}
	Tank->SetAutonomousProxy(true); // Possession must deliberately undo autonomous promotion.
	Controller->Possess(Tank);
	TestTrue(TEXT("Actor and movement replication enabled"), Tank->GetIsReplicated() && Tank->IsReplicatingMovement());
	TestEqual(TEXT("Client pawn role receives ordinary FRepMovement, including owner"), Tank->GetRemoteRole(), ROLE_SimulatedProxy);

	const FTankMovementCommand Command = MakeCommand(1, 0.05f,
		FTankControlIntent(4.f, std::numeric_limits<float>::infinity(), 120.f, true));
	const FVector OtherStart = Other->GetActorLocation();
	Commands->ReceiveCommands(*Controller, Other, TArray<FTankMovementCommand>{Command});
	TestTrue(TEXT("Cannot submit for another tank"), Other->GetActorLocation().Equals(OtherStart));
	TestEqual(TEXT("Rejected batch processes nothing"), Commands->GetLastProcessedSequence(*Controller), 0u);
	Commands->ReceiveCommands(*Controller, nullptr, TArray<FTankMovementCommand>{Command});
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{Command});
	TestTrue(TEXT("Authority clamps throttle and moves by the command's own delta time"),
		Tank->GetActorLocation().Equals(FVector(25.0, 0.0, 100.0), 0.01));
	TestTrue(TEXT("Authority neutralizes non-finite turn"), FMath::IsNearlyZero(Tank->GetActorRotation().Yaw, 0.001));
	TestTrue(TEXT("Server applies submitted turret yaw"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 120.f, 0.001f));
	TestEqual(TEXT("Processed command advances the sequence"), Commands->GetLastProcessedSequence(*Controller), 1u);

	const FVector AfterFirst = Tank->GetActorLocation();
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{Command});
	TestTrue(TEXT("Duplicate command is not simulated twice"), Tank->GetActorLocation().Equals(AfterFirst));
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{MakeCommand(0, 0.05f, Command.Intent)});
	TestTrue(TEXT("Sequence zero is never simulated"), Tank->GetActorLocation().Equals(AfterFirst));
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{
		MakeCommand(2, std::numeric_limits<float>::quiet_NaN(), Command.Intent)});
	TestTrue(TEXT("Command without valid time is not simulated"), Tank->GetActorLocation().Equals(AfterFirst));
	TestEqual(TEXT("Invalid command does not advance the sequence"), Commands->GetLastProcessedSequence(*Controller), 1u);
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{
		MakeCommand(3, 0.05f, FTankControlIntent(0.f, 0.f, std::numeric_limits<float>::quiet_NaN(), true))});
	TestTrue(TEXT("Non-finite aim yaw keeps previous turret yaw"), FMath::IsNearlyEqual(Tank->GetCanonicalTurretWorldYaw(), 120.f, 0.001f));
	TestEqual(TEXT("Gaps in the sequence are accepted"), Commands->GetLastProcessedSequence(*Controller), 3u);

	const FVector BeforeTick = Tank->GetActorLocation();
	Tank->Tick(0.5f);
	TestTrue(TEXT("Server does not move a tank without commands"), Tank->GetActorLocation().Equals(BeforeTick));

	const FTransform Before = Tank->GetActorTransform();
	const float YawBefore = Tank->GetCanonicalTurretWorldYaw();
	const FTankControlIntent Drive(1.f, 1.f, -30.f, true);
	Tank->SetRole(ROLE_SimulatedProxy);
	Tank->ApplyMovementCommand(MakeCommand(4, 0.1f, Drive));
	Tank->TankMovement->SimulateMovement(Drive, 0.1f);
	Tank->Tick(0.1f);
	TestTrue(TEXT("Client pawn/component cannot author gameplay transform"), Tank->GetActorTransform().Equals(Before));
	TestEqual(TEXT("Client cannot author canonical turret yaw"), Tank->GetCanonicalTurretWorldYaw(), YawBefore);
	Tank->CanonicalTurretWorldYaw = -75.f; // Exercise presentation notification, not a transport test.
	Tank->OnRep_CanonicalTurretWorldYaw();
	TestTrue(TEXT("Replicated yaw is applied in world space"), FMath::IsNearlyEqual(
		static_cast<float>(Tank->TurretYawPivot->GetComponentRotation().Yaw), -75.f, 0.001f));
	TestTrue(TEXT("Turret/hover presentation leaves gameplay transform unchanged"), Tank->GetActorTransform().Equals(Before));
	Tank->SetRole(ROLE_Authority);
	Controller->SetRole(ROLE_SimulatedProxy);
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{MakeCommand(5, 0.05f, Drive)});
	TestTrue(TEXT("Non-authority controller cannot run commands"), Tank->GetActorTransform().Equals(Before));
	Controller->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerCommandBatchTest, "NetworkTankArena.Multiplayer.CommandBatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerCommandBatchTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* Tank = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	UTankCommandSubsystem* Commands = Scope.World->GetSubsystem<UTankCommandSubsystem>();
	if (!TestNotNull(TEXT("Game worlds create the command subsystem"), Commands))
	{
		return false;
	}
	Controller->Possess(Tank);

	// 0.1 s at full forward throttle is 50 cm per command.
	const FTankControlIntent Forward(1.f, 0.f, 0.f, false);
	Commands->ReceiveCommands(*Controller, Tank, TArray<FTankMovementCommand>{
		MakeCommand(5, 0.1f, Forward), MakeCommand(4, 0.1f, Forward), MakeCommand(6, 0.1f, Forward), MakeCommand(6, 0.1f, Forward)});
	TestTrue(TEXT("Out-of-order and duplicate commands in a batch are skipped"),
		Tank->GetActorLocation().Equals(FVector(100.0, 0.0, 100.0), 0.01));
	TestEqual(TEXT("Last processed sequence is the newest command"), Commands->GetLastProcessedSequence(*Controller), 6u);
	const FTankCommandRingBuffer* History = Commands->FindCommandHistory(*Controller);
	if (TestNotNull(TEXT("Server keeps a command history per player"), History))
	{
		TestEqual(TEXT("History holds only simulated commands"), History->Num(), 2);
		TestTrue(TEXT("History is in sequence order"), History->Num() == 2
			&& (*History)[0].SequenceNumber == 5 && (*History)[1].SequenceNumber == 6);
	}

	TArray<FTankMovementCommand> Oversized;
	for (int32 Index = 0; Index <= FTankCommandRingBuffer::Capacity; ++Index)
	{
		Oversized.Add(MakeCommand(7 + Index, 0.1f, Forward));
	}
	const FVector BeforeOversized = Tank->GetActorLocation();
	Commands->ReceiveCommands(*Controller, Tank, Oversized);
	TestTrue(TEXT("Batch larger than the client buffer is rejected"), Tank->GetActorLocation().Equals(BeforeOversized));
	TestEqual(TEXT("Rejected batch does not advance the sequence"), Commands->GetLastProcessedSequence(*Controller), 6u);

	// Neutral commands so the history fills without driving the tank anywhere.
	uint32 SequenceNumber = 7;
	for (int32 Batch = 0; Batch < FTankCommandRingBuffer::Capacity / 10 + 2; ++Batch)
	{
		TArray<FTankMovementCommand> Neutral;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Neutral.Add(MakeCommand(SequenceNumber++, 0.01f, FTankControlIntent{}));
		}
		Commands->ReceiveCommands(*Controller, Tank, Neutral);
	}
	History = Commands->FindCommandHistory(*Controller);
	if (TestNotNull(TEXT("History survives many batches"), History))
	{
		TestEqual(TEXT("History is bounded by its capacity"), History->Num(), FTankCommandRingBuffer::Capacity);
		TestEqual(TEXT("History keeps the newest command"), (*History)[History->Num() - 1].SequenceNumber, SequenceNumber - 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMultiplayerLocalCommandTest, "NetworkTankArena.Multiplayer.LocalCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMultiplayerLocalCommandTest::RunTest(const FString& Parameters)
{
	FScopedTankTestWorld Scope;
	ATankPawn* Tank = Scope.SpawnTank(FVector(0.0, 0.0, 100.0));
	ATankPlayerController* Controller = Scope.World->SpawnActor<ATankPlayerController>();
	UTankCommandSubsystem* Commands = Scope.World->GetSubsystem<UTankCommandSubsystem>();
	if (!TestNotNull(TEXT("Game worlds create the command subsystem"), Commands))
	{
		return false;
	}
	Controller->Possess(Tank);
	const FTankControlIntent Forward(1.f, 0.f, 0.f, false);

	Commands->RecordLocalCommand(*Controller, *Tank, Forward, 0.1f);
	TestTrue(TEXT("Host command is simulated immediately"), Tank->GetActorLocation().Equals(FVector(50.0, 0.0, 100.0), 0.01));
	TestEqual(TEXT("First command uses sequence 1"), Commands->GetLastProcessedSequence(*Controller), 1u);
	const FTankCommandRingBuffer* Unacknowledged = Commands->FindUnacknowledgedCommands(*Controller);
	TestTrue(TEXT("Host keeps no unacknowledged commands"), Unacknowledged && Unacknowledged->IsEmpty());
	Commands->RecordLocalCommand(*Controller, *Tank, Forward, 0.f);
	TestEqual(TEXT("Frame without valid time produces no command"), Commands->GetLastProcessedSequence(*Controller), 1u);
	Commands->RecordLocalCommand(*Controller, *Tank, Forward, 0.1f);
	TestEqual(TEXT("Invalid frame does not consume a sequence number"), Commands->GetLastProcessedSequence(*Controller), 2u);

	// Without a net driver the server RPC is absorbed, so only the owning-side buffering runs.
	Controller->SetRole(ROLE_AutonomousProxy);
	const FVector ClientStart = Tank->GetActorLocation();
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		Commands->RecordLocalCommand(*Controller, *Tank, Forward, 0.02f);
	}
	TestTrue(TEXT("Remote client does not move the tank itself"), Tank->GetActorLocation().Equals(ClientStart));
	Unacknowledged = Commands->FindUnacknowledgedCommands(*Controller);
	if (TestNotNull(TEXT("Remote client keeps unacknowledged commands"), Unacknowledged))
	{
		TestEqual(TEXT("Every frame is buffered"), Unacknowledged->Num(), 3);
		TestTrue(TEXT("Buffered commands continue the sequence"), Unacknowledged->Num() == 3
			&& (*Unacknowledged)[0].SequenceNumber == 3 && (*Unacknowledged)[2].SequenceNumber == 5);
	}
	Commands->ResetLocalCommands(*Controller);
	TestTrue(TEXT("Reset drops buffered commands"), Unacknowledged && Unacknowledged->IsEmpty());
	Commands->RecordLocalCommand(*Controller, *Tank, Forward, 0.02f);
	TestTrue(TEXT("Sequence numbers keep counting after a reset"), Unacknowledged && Unacknowledged->Num() == 1
		&& (*Unacknowledged)[0].SequenceNumber == 6);
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
	UTankCommandSubsystem* Commands = Scope.World->GetSubsystem<UTankCommandSubsystem>();
	if (!TestNotNull(TEXT("Game worlds create the command subsystem"), Commands))
	{
		return false;
	}
	const FTankControlIntent Drive(1.f, 1.f, 45.f, true);
	Controller->Possess(First);
	Commands->ReceiveCommands(*Controller, First, TArray<FTankMovementCommand>{MakeCommand(1, 0.05f, Drive)});
	const FVector MovingPosition = First->GetActorLocation();
	TestFalse(TEXT("Possessed tank runs its commands"), MovingPosition.Equals(FVector(0.0, 0.0, 100.0)));
	First->Tick(0.5f);
	TestTrue(TEXT("Missing commands cannot keep moving the tank"), First->GetActorLocation().Equals(MovingPosition));
	TestTrue(TEXT("Missing commands keep last turret yaw"), FMath::IsNearlyEqual(First->GetCanonicalTurretWorldYaw(), 45.f, 0.001f));
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->AcknowledgePossession(First);
	TestEqual(TEXT("Possession acknowledgement clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Possession acknowledgement clears cached turn"), Controller->CachedTurn, 0.f);
	Controller->CachedThrottle = 1.f;
	Controller->CachedTurn = -1.f;
	Controller->FlushPressedKeys();
	TestEqual(TEXT("Flush clears cached throttle"), Controller->CachedThrottle, 0.f);
	TestEqual(TEXT("Flush clears cached turn"), Controller->CachedTurn, 0.f);

	Controller->Possess(Second);
	TestFalse(TEXT("Unpossessed tank drops tick controller"), First->TickController.IsValid());
	TestTrue(TEXT("New tank uses current controller as prerequisite"), Second->TickController.Get() == Controller);
	const FVector FirstParked = First->GetActorLocation();
	const FVector SecondStart = Second->GetActorLocation();
	Commands->ReceiveCommands(*Controller, First, TArray<FTankMovementCommand>{MakeCommand(2, 0.05f, Drive)});
	TestTrue(TEXT("Late batch for previous pawn is ignored"), First->GetActorLocation().Equals(FirstParked));
	TestTrue(TEXT("Late batch cannot move new pawn"), Second->GetActorLocation().Equals(SecondStart));
	Commands->ReceiveCommands(*Controller, Second, TArray<FTankMovementCommand>{MakeCommand(3, 0.05f, Drive)});
	TestFalse(TEXT("New pawn runs its commands"), Second->GetActorLocation().Equals(SecondStart));
	TestEqual(TEXT("Sequence numbers carry over to the new pawn"), Commands->GetLastProcessedSequence(*Controller), 3u);
	Controller->UnPossess();
	TestFalse(TEXT("Unpossession removes new tick prerequisite"), Second->TickController.IsValid());
	TestFalse(TEXT("Unpossession removes input context bookkeeping"), Controller->MappingSubsystem.IsValid());
	uint32 SequenceNumber = 4;
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		Controller->Possess(First);
		Controller->UnPossess();
		Commands->ReceiveCommands(*Controller, First, TArray<FTankMovementCommand>{MakeCommand(SequenceNumber++, 0.05f, Drive)});
		TestTrue(TEXT("Unpossessed tank ignores commands"), First->GetActorLocation().Equals(FirstParked));
		TestFalse(TEXT("Repeated possession drops old prerequisite"), First->TickController.IsValid());
	}
	Commands->RemovePlayer(*Controller);
	TestNull(TEXT("Removed player leaves no command history"), Commands->FindCommandHistory(*Controller));
	TestNull(TEXT("Removed player leaves no local commands"), Commands->FindUnacknowledgedCommands(*Controller));
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
