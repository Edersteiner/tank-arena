#include "TankCommandRingBuffer.h"

bool FTankCommandRingBuffer::Push(const FTankMovementCommand& Command)
{
	checkSlow(IsEmpty() || Command.SequenceNumber > (*this)[Count - 1].SequenceNumber);
	const bool bHadRoom = !IsFull();
	if (!bHadRoom)
	{
		Head = (Head + 1) % Capacity;
		--Count;
	}
	Commands[(Head + Count) % Capacity] = Command;
	++Count;
	return bHadRoom;
}

int32 FTankCommandRingBuffer::DiscardThrough(uint32 SequenceNumber)
{
	int32 Discarded = 0;
	while (!IsEmpty() && Commands[Head].SequenceNumber <= SequenceNumber)
	{
		Head = (Head + 1) % Capacity;
		--Count;
		++Discarded;
	}
	return Discarded;
}

void FTankCommandRingBuffer::Reset()
{
	Head = 0;
	Count = 0;
}

const FTankMovementCommand& FTankCommandRingBuffer::operator[](int32 Index) const
{
	check(Index >= 0 && Index < Count);
	return Commands[(Head + Index) % Capacity];
}
