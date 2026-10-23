#include "TankCommandSubsystem.h"

#include "TankGameplayMath.h"
#include "TankMovementState.h"
#include "TankPawn.h"
#include "TankPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogTankPrediction, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogTankTimeBudget, Log, All);

namespace
{
	// Above the 0.005 cm rounding of the acknowledged location, so only real disagreements are reported.
	constexpr double CorrectionToleranceCm = 0.1;
	constexpr double CorrectionToleranceDegrees = 0.1;
}

void UTankCommandSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!NetworkTankArena::LocalTank::IsValidDeltaTime(DeltaTime))
	{
		return;
	}
	for (TPair<TObjectKey<ATankPlayerController>, FServerPlayerCommands>& Player : ServerPlayers)
	{
		Player.Value.TimeBudgetSeconds = FMath::Min(Player.Value.TimeBudgetSeconds + DeltaTime, MaxTimeBudgetSeconds);
	}
}

TStatId UTankCommandSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTankCommandSubsystem, STATGROUP_Tickables);
}

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
	// Predict, so the owning client sees its input now instead of a round trip later.
	Tank.ApplyMovementCommand(Command);
	Local.SecondsUntilSend -= Command.DeltaTime;
	if (Local.SecondsUntilSend <= UE_SMALL_NUMBER)
	{
		SendUnacknowledgedCommands(Controller, Tank, Local);
		// No catch-up burst after a slow frame.
		Local.SecondsUntilSend = SendIntervalSeconds;
	}
}

void UTankCommandSubsystem::CollectBatch(const FTankCommandRingBuffer& Unacknowledged,
	TArray<FTankMovementCommand>& OutBatch)
{
	OutBatch.Reset();
	const int32 Count = FMath::Min(Unacknowledged.Num(), MaxCommandsPerBatch);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutBatch.Add(Unacknowledged[Index]);
	}
}

void UTankCommandSubsystem::SendUnacknowledgedCommands(ATankPlayerController& Controller, ATankPawn& Tank,
	const FLocalPlayerCommands& Local)
{
	TArray<FTankMovementCommand> Batch;
	CollectBatch(Local.Unacknowledged, Batch);
	if (!Batch.IsEmpty())
	{
		Controller.ServerSubmitMovementCommands(&Tank, Batch);
	}
}

void UTankCommandSubsystem::ResetLocalCommands(const ATankPlayerController& Controller)
{
	if (FLocalPlayerCommands* Local = LocalPlayers.Find(&Controller))
	{
		Local->Unacknowledged.Reset();
		Local->SecondsUntilSend = 0.f;
	}
}

void UTankCommandSubsystem::AcknowledgeCommands(const ATankPlayerController& Controller, ATankPawn* Tank,
	uint32 LastProcessedSequence, const FTankMovementState& State)
{
	FLocalPlayerCommands* Local = LocalPlayers.Find(&Controller);
	// A reordered acknowledgement carries an older state than one already applied.
	if (!Local || LastProcessedSequence < Local->LastAcknowledgedSequence)
	{
		return;
	}
	Local->LastAcknowledgedSequence = LastProcessedSequence;
	Local->Unacknowledged.DiscardThrough(LastProcessedSequence);
	// A late acknowledgement for a previous pawn must not move the new one.
	if (!IsValid(Tank) || Controller.GetPawn() != Tank)
	{
		return;
	}
	const FVector PredictedLocation = Tank->GetActorLocation();
	const double PredictedYaw = Tank->GetActorRotation().Yaw;
	// Replaying on top of a rejected state would run the commands twice.
	if (!Tank->ApplyServerState(State))
	{
		return;
	}
	for (int32 Index = 0; Index < Local->Unacknowledged.Num(); ++Index)
	{
		Tank->ApplyMovementCommand(Local->Unacknowledged[Index]);
	}
	const double CorrectionCm = FVector::Dist(PredictedLocation, Tank->GetActorLocation());
	const double CorrectionDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(PredictedYaw, Tank->GetActorRotation().Yaw));
	if (CorrectionCm > CorrectionToleranceCm || CorrectionDegrees > CorrectionToleranceDegrees)
	{
		UE_LOG(LogTankPrediction, Log, TEXT("Corrected %.2f cm, %.2f deg at sequence %u, replayed %d command(s)."),
			CorrectionCm, CorrectionDegrees, LastProcessedSequence, Local->Unacknowledged.Num());
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
	// An honest client never sends more than one batch can hold.
	if (Commands.Num() > MaxCommandsPerBatch)
	{
		return;
	}
	FServerPlayerCommands& Server = ServerPlayers.FindOrAdd(&Controller);
	// A listen host is the server, so there is nobody to protect its time from.
	const bool bBudgeted = !Controller.IsLocalController();
	double ClampedSeconds = 0.0;
	for (const FTankMovementCommand& Incoming : Commands)
	{
		FTankMovementCommand Command = NetworkTankArena::LocalTank::SanitizeMovementCommand(Incoming);
		if (Command.SequenceNumber <= Server.LastProcessedSequence || Command.DeltaTime <= 0.f)
		{
			continue;
		}
		if (bBudgeted)
		{
			// An over-budget command still counts as processed with the time it was given, so it is
			// acknowledged and the client is corrected instead of resending it forever.
			const float Allowed = static_cast<float>(FMath::Clamp<double>(Server.TimeBudgetSeconds, 0.0, Command.DeltaTime));
			ClampedSeconds += Command.DeltaTime - Allowed;
			Command.DeltaTime = Allowed;
			Server.TimeBudgetSeconds -= Allowed;
		}
		TargetTank->ApplyMovementCommand(Command);
		Server.History.Push(Command);
		Server.LastProcessedSequence = Command.SequenceNumber;
	}
	if (ClampedSeconds > 0.0)
	{
		UE_LOG(LogTankTimeBudget, Log, TEXT("Clamped %.1f ms of command time over budget for %s."),
			ClampedSeconds * 1000.0, *Controller.GetName());
	}
	// Also sent when nothing new was processed, which repairs a lost acknowledgement.
	// A listen host's own commands never travel, so there is nothing to acknowledge.
	if (!Controller.IsLocalController())
	{
		Controller.ClientAcknowledgeCommands(TargetTank, Server.LastProcessedSequence, TargetTank->GetMovementState());
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

double UTankCommandSubsystem::GetTimeBudget(const ATankPlayerController& Controller) const
{
	const FServerPlayerCommands* Server = ServerPlayers.Find(&Controller);
	return Server ? Server->TimeBudgetSeconds : MaxTimeBudgetSeconds;
}
