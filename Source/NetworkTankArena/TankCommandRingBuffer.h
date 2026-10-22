#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "TankMovementCommand.h"

// Fixed-capacity queue of movement commands, oldest first. Commands must be pushed in
// increasing sequence order. A push into a full buffer drops the oldest command.
class NETWORKTANKARENA_API FTankCommandRingBuffer
{
public:
	static constexpr int32 Capacity = 256;

	// Returns false when the buffer was full and the oldest command was dropped to make room.
	bool Push(const FTankMovementCommand& Command);
	// Drops commands from the front up to and including this sequence number. Returns how many were dropped.
	int32 DiscardThrough(uint32 SequenceNumber);
	void Reset();

	int32 Num() const { return Count; }
	bool IsEmpty() const { return Count == 0; }
	bool IsFull() const { return Count == Capacity; }
	// Index 0 is the oldest stored command.
	const FTankMovementCommand& operator[](int32 Index) const;

private:
	TStaticArray<FTankMovementCommand, Capacity> Commands;
	// Storage slot of the oldest command.
	int32 Head = 0;
	int32 Count = 0;
};
