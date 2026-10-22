#include "TankCommandSubsystem.h"

#include "TankGameplayMath.h"
#include "TankPawn.h"
#include "TankPlayerController.h"

bool UTankCommandSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTankCommandSubsystem::RecordLocalCommand(ATankPlayerController& Controller, ATankPawn& Tank,
	const FTankControlIntent& Intent, float DeltaTime)
{
	FLocalPlayerCommands& Local = LocalPlayers.FindOrAdd(&Controller);
	FTankMovementCommand Command;
	Command.DeltaTime = DeltaTime;
	Command.SequenceNumber = Local.NextSequenceNumber;
	Command.Intent = Intent;
	// Sanitized on the owning side too, so the command we keep is exactly what the server will run.
	Command = NetworkTankArena::LocalTank::SanitizeMovementCommand(Command);
	if (Command.DeltaTime <= 0.f)
	{
		return;
	}
	++Local.NextSequenceNumber;

	if (Controller.HasAuthority())
	{
		// A listen host owns the simulation, so its command needs no network hop.
		ReceiveCommands(Controller, &Tank, MakeArrayView(&Command, 1));
		return;
	}
	Local.Unacknowledged.Push(Command);
	Local.SecondsUntilSend -= Command.DeltaTime;
	if (Local.SecondsUntilSend <= UE_SMALL_NUMBER)
	{
		SendUnsentCommands(Controller, Tank, Local);
		// No catch-up burst after a slow frame.
		Local.SecondsUntilSend = SendIntervalSeconds;
	}
}

void UTankCommandSubsystem::SendUnsentCommands(ATankPlayerController& Controller, ATankPawn& Tank,
	FLocalPlayerCommands& Local)
{
	TArray<FTankMovementCommand> Batch;
	for (int32 Index = 0; Index < Local.Unacknowledged.Num(); ++Index)
	{
		if (Local.Unacknowledged[Index].SequenceNumber >= Local.NextUnsentSequence)
		{
			Batch.Add(Local.Unacknowledged[Index]);
		}
	}
	if (Batch.IsEmpty())
	{
		return;
	}
	Local.NextUnsentSequence = Batch.Last().SequenceNumber + 1;
	Controller.ServerSubmitMovementCommands(&Tank, Batch);
}

void UTankCommandSubsystem::ResetLocalCommands(const ATankPlayerController& Controller)
{
	if (FLocalPlayerCommands* Local = LocalPlayers.Find(&Controller))
	{
		Local->Unacknowledged.Reset();
		Local->NextUnsentSequence = Local->NextSequenceNumber;
		Local->SecondsUntilSend = 0.f;
	}
}

void UTankCommandSubsystem::ReceiveCommands(ATankPlayerController& Controller, ATankPawn* TargetTank,
	TConstArrayView<FTankMovementCommand> Commands)
{
	// The RPC is routed through the owned controller. Never let its caller name another
	// player's tank, or let an in-flight batch for an old pawn move a new possession.
	if (!Controller.HasAuthority() || !IsValid(TargetTank) || !TargetTank->HasAuthority()
		|| Controller.GetPawn() != TargetTank || TargetTank->GetController() != &Controller)
	{
		return;
	}
	// An honest client can never send more than its buffer holds.
	if (Commands.Num() > FTankCommandRingBuffer::Capacity)
	{
		return;
	}
	FServerPlayerCommands& Server = ServerPlayers.FindOrAdd(&Controller);
	for (const FTankMovementCommand& Incoming : Commands)
	{
		const FTankMovementCommand Command = NetworkTankArena::LocalTank::SanitizeMovementCommand(Incoming);
		if (Command.SequenceNumber <= Server.LastProcessedSequence || Command.DeltaTime <= 0.f)
		{
			continue;
		}
		TargetTank->ApplyMovementCommand(Command);
		Server.History.Push(Command);
		Server.LastProcessedSequence = Command.SequenceNumber;
	}
}

void UTankCommandSubsystem::RemovePlayer(const ATankPlayerController& Controller)
{
	LocalPlayers.Remove(&Controller);
	ServerPlayers.Remove(&Controller);
}

const FTankCommandRingBuffer* UTankCommandSubsystem::FindUnacknowledgedCommands(const ATankPlayerController& Controller) const
{
	const FLocalPlayerCommands* Local = LocalPlayers.Find(&Controller);
	return Local ? &Local->Unacknowledged : nullptr;
}

const FTankCommandRingBuffer* UTankCommandSubsystem::FindCommandHistory(const ATankPlayerController& Controller) const
{
	const FServerPlayerCommands* Server = ServerPlayers.Find(&Controller);
	return Server ? &Server->History : nullptr;
}

uint32 UTankCommandSubsystem::GetLastProcessedSequence(const ATankPlayerController& Controller) const
{
	const FServerPlayerCommands* Server = ServerPlayers.Find(&Controller);
	return Server ? Server->LastProcessedSequence : 0;
}
