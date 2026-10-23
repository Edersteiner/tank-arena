#include "TankInterpolationBuffer.h"

#include "TankGameplayMath.h"

namespace
{
	float LerpYaw(float From, float To, float Alpha)
	{
		return NetworkTankArena::LocalTank::NormalizeYaw(From + FMath::FindDeltaAngleDegrees(From, To) * Alpha);
	}
}

void FTankInterpolationBuffer::Add(const FTankPresentationSnapshot& Snapshot, double UpdateIntervalSeconds)
{
	if (const FTankPresentationSnapshot* Previous = Newest())
	{
		if (Snapshot.Time < Previous->Time)
		{
			return;
		}
		if (Snapshot.Time - Previous->Time > StillGapIntervals * UpdateIntervalSeconds)
		{
			FTankPresentationSnapshot Hold = *Previous;
			Hold.Time = Snapshot.Time - UpdateIntervalSeconds;
			Push(Hold);
		}
	}
	Push(Snapshot);
}

void FTankInterpolationBuffer::Push(const FTankPresentationSnapshot& Snapshot)
{
	if (Snapshots.Num() == Capacity)
	{
		Snapshots.RemoveAt(0, EAllowShrinking::No);
	}
	Snapshots.Add(Snapshot);
}

bool FTankInterpolationBuffer::Sample(double RenderTime, FTankPresentationSnapshot& OutSnapshot) const
{
	if (Snapshots.IsEmpty())
	{
		return false;
	}
	if (RenderTime <= Snapshots[0].Time)
	{
		OutSnapshot = Snapshots[0];
		return true;
	}
	for (int32 Index = 1; Index < Snapshots.Num(); ++Index)
	{
		const FTankPresentationSnapshot& To = Snapshots[Index];
		if (RenderTime < To.Time)
		{
			// The previous iteration guarantees From.Time <= RenderTime < To.Time.
			const FTankPresentationSnapshot& From = Snapshots[Index - 1];
			const float Alpha = static_cast<float>((RenderTime - From.Time) / (To.Time - From.Time));
			OutSnapshot.Time = RenderTime;
			OutSnapshot.Location = FMath::Lerp(From.Location, To.Location, static_cast<double>(Alpha));
			OutSnapshot.ChassisYaw = LerpYaw(From.ChassisYaw, To.ChassisYaw, Alpha);
			OutSnapshot.TurretYaw = LerpYaw(From.TurretYaw, To.TurretYaw, Alpha);
			return true;
		}
	}
	OutSnapshot = Snapshots.Last();
	return true;
}
