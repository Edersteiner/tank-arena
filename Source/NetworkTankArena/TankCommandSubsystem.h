#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TankCommandRingBuffer.h"
#include "UObject/ObjectKey.h"
#include "TankCommandSubsystem.generated.h"

class ATankPawn;
class ATankPlayerController;
struct FTankControlIntent;

// Owns the numbered movement command flow for every player in this world. The owning side
// records and sends commands, the server validates, simulates and keeps a history of them.
UCLASS()
class NETWORKTANKARENA_API UTankCommandSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Commands are batched and sent at this rate, matching the pawn replication rate.
	static constexpr float SendIntervalSeconds = 1.f / 20.f;

	// Owning side. Turns this frame's intent into the next numbered command. A listen host
	// simulates it immediately; a remote client buffers it until the next send.
	void RecordLocalCommand(ATankPlayerController& Controller, ATankPawn& Tank,
		const FTankControlIntent& Intent, float DeltaTime);
	// Owning side. Drops buffered commands meant for a previous pawn. Sequence numbers keep counting.
	void ResetLocalCommands(const ATankPlayerController& Controller);

	// Server side. Simulates new commands in sequence order and skips duplicate, stale or invalid ones.
	void ReceiveCommands(ATankPlayerController& Controller, ATankPawn* TargetTank,
		TConstArrayView<FTankMovementCommand> Commands);

	void RemovePlayer(const ATankPlayerController& Controller);

	const FTankCommandRingBuffer* FindUnacknowledgedCommands(const ATankPlayerController& Controller) const;
	const FTankCommandRingBuffer* FindCommandHistory(const ATankPlayerController& Controller) const;
	// Zero until the first command is processed, since sequence numbers start at 1.
	uint32 GetLastProcessedSequence(const ATankPlayerController& Controller) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FLocalPlayerCommands
	{
		uint32 NextSequenceNumber = 1;
		uint32 NextUnsentSequence = 1;
		float SecondsUntilSend = 0.f;
		FTankCommandRingBuffer Unacknowledged;
	};

	struct FServerPlayerCommands
	{
		uint32 LastProcessedSequence = 0;
		FTankCommandRingBuffer History;
	};

	void SendUnsentCommands(ATankPlayerController& Controller, ATankPawn& Tank, FLocalPlayerCommands& Local);

	TMap<TObjectKey<ATankPlayerController>, FLocalPlayerCommands> LocalPlayers;
	TMap<TObjectKey<ATankPlayerController>, FServerPlayerCommands> ServerPlayers;
};
