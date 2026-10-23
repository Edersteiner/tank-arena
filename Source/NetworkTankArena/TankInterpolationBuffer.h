#pragma once

#include "CoreMinimal.h"

// How another player's tank looked at one moment, stamped with the local time the update arrived.
struct FTankPresentationSnapshot
{
	double Time = 0.0;
	FVector Location = FVector::ZeroVector;
	float ChassisYaw = 0.f;
	float TurretYaw = 0.f;
};

// Timed snapshots of a tank, oldest first, so it can be drawn a fixed delay in the past
// between two updates instead of jumping to each one as it arrives.
class NETWORKTANKARENA_API FTankInterpolationBuffer
{
public:
	static constexpr int32 Capacity = 32;
	static constexpr double StillGapIntervals = 5.0;

	void Add(const FTankPresentationSnapshot& Snapshot, double UpdateIntervalSeconds);
	bool Sample(double RenderTime, FTankPresentationSnapshot& OutSnapshot) const;
	const FTankPresentationSnapshot* Newest() const { return Snapshots.IsEmpty() ? nullptr : &Snapshots.Last(); }
	int32 Num() const { return Snapshots.Num(); }
	void Reset() { Snapshots.Reset(); }

private:
	void Push(const FTankPresentationSnapshot& Snapshot);

	TArray<FTankPresentationSnapshot> Snapshots;
};
