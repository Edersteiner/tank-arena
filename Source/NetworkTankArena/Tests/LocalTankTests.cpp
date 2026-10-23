#include "../TankCommandRingBuffer.h"
#include "../TankGameplayMath.h"
#include "../TankInterpolationBuffer.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

using namespace NetworkTankArena::LocalTank;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankIntentTest, "NetworkTankArena.LocalTank.Intent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankIntentTest::RunTest(const FString& Parameters)
{
	const FTankControlIntent Default;
	TestEqual(TEXT("Default throttle is neutral"), Default.Throttle, 0.f);
	TestEqual(TEXT("Default turn is neutral"), Default.Turn, 0.f);
	TestEqual(TEXT("Default aim yaw is zero"), Default.AimYaw, 0.f);
	TestFalse(TEXT("Default intent carries no aim"), Default.bHasAimYaw);

	FTankControlIntent Input;
	Input.Throttle = 2.f;
	Input.Turn = -3.f;
	Input.AimYaw = 135.f;
	Input.bHasAimYaw = true;
	FTankControlIntent Result = SanitizeIntent(Input);
	TestEqual(TEXT("Throttle clamps independently"), Result.Throttle, 1.f);
	TestEqual(TEXT("Turn clamps independently"), Result.Turn, -1.f);
	TestTrue(TEXT("Valid aim yaw is preserved"), Result.bHasAimYaw && FMath::IsNearlyEqual(Result.AimYaw, 135.f, 0.001f));
	Input.bHasAimYaw = false;
	TestFalse(TEXT("Finite yaw does not override missing aim"), SanitizeIntent(Input).bHasAimYaw);
	Input.bHasAimYaw = true;
	Input.AimYaw = 270.f;
	TestTrue(TEXT("Out-of-range aim yaw is normalized"), FMath::IsNearlyEqual(SanitizeIntent(Input).AimYaw, -90.f, 0.001f));
	Input.Throttle = 0.8f;
	Input.Turn = 0.9f;
	Result = SanitizeIntent(Input);
	TestTrue(TEXT("Axes are not normalized together"), FMath::IsNearlyEqual(Result.Throttle, 0.8f)
		&& FMath::IsNearlyEqual(Result.Turn, 0.9f));

	Input.Throttle = std::numeric_limits<float>::quiet_NaN();
	Input.Turn = std::numeric_limits<float>::infinity();
	Input.AimYaw = std::numeric_limits<float>::quiet_NaN();
	Result = SanitizeIntent(Input);
	TestEqual(TEXT("NaN throttle becomes zero"), Result.Throttle, 0.f);
	TestEqual(TEXT("Infinite turn becomes zero"), Result.Turn, 0.f);
	// What a non-finite yaw means for the turret is checked on the pawn; here it must just never leak through.
	TestTrue(TEXT("NaN aim yaw never leaves sanitizing"), FMath::IsFinite(Result.AimYaw));
	TestFalse(TEXT("NaN aim yaw is rejected"), Result.bHasAimYaw);
	Input.AimYaw = std::numeric_limits<float>::infinity();
	TestTrue(TEXT("Infinite aim yaw never leaves sanitizing"), FMath::IsFinite(SanitizeIntent(Input).AimYaw));
	TestFalse(TEXT("Infinite aim yaw is rejected"), SanitizeIntent(Input).bHasAimYaw);
	TestEqual(TEXT("Negative infinity axis becomes zero"), SanitizeAxis(-std::numeric_limits<float>::infinity()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMovementMathTest, "NetworkTankArena.LocalTank.Movement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMovementMathTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Forward follows +X"), CalculateMovementDelta(1.f, 500.f, 300.f, 0.f, 0.5f).Equals(FVector(250.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("Reverse follows -X at reverse speed"), CalculateMovementDelta(-1.f, 500.f, 300.f, 0.f, 0.5f).Equals(FVector(-150.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("Analog throttle scales translation"), CalculateMovementDelta(0.5f, 500.f, 300.f, 0.f, 1.f).Equals(FVector(250.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("Zero throttle requests no translation"), CalculateMovementDelta(0.f, 500.f, 300.f, 75.f, 1.f).IsZero());
	TestTrue(TEXT("Stationary turn still changes yaw"), FMath::IsNearlyEqual(CalculateChassisYaw(0.f, 1.f, 120.f, 0.5f), 60.f, 0.001f));
	TestTrue(TEXT("Negative turn changes yaw in the other direction"), FMath::IsNearlyEqual(CalculateChassisYaw(0.f, -1.f, 120.f, 0.5f), -60.f, 0.001f));
	TestTrue(TEXT("Zero turn preserves yaw"), FMath::IsNearlyEqual(CalculateChassisYaw(75.f, 0.f, 120.f, 1.f), 75.f));
	const float TurnedYaw = CalculateChassisYaw(0.f, 1.f, 90.f, 1.f);
	TestTrue(TEXT("Translation uses this frame's turned yaw"), CalculateMovementDelta(1.f, 500.f, 300.f, TurnedYaw, 1.f).Equals(FVector(0.0, 500.0, 0.0), 0.001));
	TestTrue(TEXT("Reverse uses the same turned yaw"), CalculateMovementDelta(-1.f, 500.f, 300.f, TurnedYaw, 1.f).Equals(FVector(0.0, -300.0, 0.0), 0.001));
	TestTrue(TEXT("Chassis yaw wraps"), FMath::IsNearlyEqual(CalculateChassisYaw(170.f, 1.f, 30.f, 1.f), -160.f, 0.001f));
	TestTrue(TEXT("Movement stays horizontal"), FMath::IsNearlyZero(CalculateMovementDelta(1.f, 500.f, 300.f, 42.f, 1.f).Z));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankInvalidTimeAndTuningTest, "NetworkTankArena.LocalTank.InvalidTimeAndTuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankInvalidTimeAndTuningTest::RunTest(const FString& Parameters)
{
	for (const float DeltaTime : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		TestFalse(TEXT("Invalid delta time is rejected"), IsValidDeltaTime(DeltaTime));
		TestTrue(TEXT("Invalid delta time requests no movement"), CalculateMovementDelta(1.f, 500.f, 300.f, 0.f, DeltaTime).IsZero());
		TestTrue(TEXT("Invalid delta time preserves yaw"), FMath::IsNearlyEqual(CalculateChassisYaw(30.f, 1.f, 120.f, DeltaTime), 30.f));
	}
	TestTrue(TEXT("Positive finite delta time is valid"), IsValidDeltaTime(1.f / 60.f));
	TestEqual(TEXT("Negative tuning becomes zero"), NonNegativeFinite(-10.f), 0.f);
	TestEqual(TEXT("Infinite tuning becomes zero"), NonNegativeFinite(std::numeric_limits<float>::infinity()), 0.f);
	TestEqual(TEXT("NaN tuning becomes zero"), NonNegativeFinite(std::numeric_limits<float>::quiet_NaN()), 0.f);
	TestTrue(TEXT("Invalid speed cannot generate translation"), CalculateMovementDelta(1.f, std::numeric_limits<float>::infinity(), 300.f, 0.f, 1.f).IsZero());
	TestTrue(TEXT("Invalid turn rate cannot change yaw"), FMath::IsNearlyEqual(CalculateChassisYaw(30.f, 1.f, std::numeric_limits<float>::quiet_NaN(), 1.f), 30.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankAimPlaneTest, "NetworkTankArena.LocalTank.AimPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankAimPlaneTest::RunTest(const FString& Parameters)
{
	FVector Point;
	TestTrue(TEXT("Downward ray intersects"), TryIntersectAimPlane(FVector(10.0, 20.0, 100.0), FVector(0.0, 0.0, -1.0), 5.0, Point));
	TestTrue(TEXT("Intersection is on the gameplay plane"), Point.Equals(FVector(10.0, 20.0, 5.0), 0.001));
	TestTrue(TEXT("Scaled oblique ray intersects"), TryIntersectAimPlane(FVector(0.0, 0.0, 100.0), FVector(2.0, 0.0, -2.0), 0.0, Point));
	TestTrue(TEXT("Oblique intersection is correct"), Point.Equals(FVector(100.0, 0.0, 0.0), 0.001));
	const FVector PreviousPoint = Point;
	TestFalse(TEXT("Parallel ray is rejected"), TryIntersectAimPlane(FVector(0.0, 0.0, 100.0), FVector::ForwardVector, 0.0, Point));
	TestFalse(TEXT("Nearly parallel ray is rejected"), TryIntersectAimPlane(FVector(0.0, 0.0, 100.0), FVector(1.0, 0.0, -1.e-6), 0.0, Point));
	TestFalse(TEXT("Behind-ray plane is rejected"), TryIntersectAimPlane(FVector(0.0, 0.0, 100.0), FVector::UpVector, 0.0, Point));
	TestFalse(TEXT("Degenerate direction is rejected"), TryIntersectAimPlane(FVector::ZeroVector, FVector::ZeroVector, 0.0, Point));
	TestFalse(TEXT("Infinite origin is rejected"), TryIntersectAimPlane(FVector(std::numeric_limits<double>::infinity(), 0.0, 0.0), -FVector::UpVector, 0.0, Point));
	TestFalse(TEXT("NaN direction is rejected"), TryIntersectAimPlane(FVector::ZeroVector, FVector(0.0, 0.0, std::numeric_limits<double>::quiet_NaN()), 0.0, Point));
	TestFalse(TEXT("Infinite plane is rejected"), TryIntersectAimPlane(FVector::ZeroVector, -FVector::UpVector, std::numeric_limits<double>::infinity(), Point));
	TestTrue(TEXT("Failed samples do not overwrite output"), Point.Equals(PreviousPoint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankTurretYawTest, "NetworkTankArena.LocalTank.TurretYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankTurretYawTest::RunTest(const FString& Parameters)
{
	const FVector Position(100.0, 200.0, 30.0);
	for (const float ExpectedYaw : {0.f, 90.f, 180.f, -90.f, 45.f, 179.f, -179.f, 181.f, -181.f})
	{
		const FVector Target = Position + FRotator(0.0, ExpectedYaw, 0.0).Vector() * 100.0 + FVector(0.0, 0.0, 500.0);
		float Yaw = 0.f;
		TestTrue(TEXT("Finite planar direction yields yaw"), TryCalculateAimYaw(Position, Target, Yaw));
		TestTrue(TEXT("World yaw matches direction and ignores height"), FMath::Abs(FMath::FindDeltaAngleDegrees(ExpectedYaw, Yaw)) < 0.001f);
	}
	TestTrue(TEXT("Relative yaw crosses positive wrap"), FMath::IsNearlyEqual(CalculateRelativeTurretYaw(170.f, -170.f), 20.f, 0.001f));
	TestTrue(TEXT("Relative yaw crosses negative wrap"), FMath::IsNearlyEqual(CalculateRelativeTurretYaw(-170.f, 170.f), -20.f, 0.001f));
	TestTrue(TEXT("Equivalent headings have zero relative yaw"), FMath::IsNearlyZero(CalculateRelativeTurretYaw(350.f, -10.f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankTurretStateTest, "NetworkTankArena.LocalTank.TurretState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankTurretStateTest::RunTest(const FString& Parameters)
{
	const FVector Position(100.0, 200.0, 30.0);
	float Yaw = ResolveTurretWorldYaw(275.f, Position, Position);
	TestTrue(TEXT("Aim on the tank itself keeps normalized previous yaw"), FMath::IsNearlyEqual(Yaw, -85.f, 0.001f));
	Yaw = ResolveTurretWorldYaw(Yaw, Position, Position + FVector(0.0, 100.0, 500.0));
	TestTrue(TEXT("Valid aim point updates yaw"), FMath::IsNearlyEqual(Yaw, 90.f, 0.001f));
	TestTrue(TEXT("Vertical-only aim retains yaw"), FMath::IsNearlyEqual(ResolveTurretWorldYaw(Yaw, Position, Position + FVector(0.0, 0.0, 100.0)), Yaw));
	FVector NonFinite = Position;
	NonFinite.X = std::numeric_limits<double>::quiet_NaN();
	TestTrue(TEXT("Non-finite aim point retains yaw"), FMath::IsNearlyEqual(ResolveTurretWorldYaw(Yaw, Position, NonFinite), Yaw));
	for (const float ChassisYaw : {0.f, 45.f, 170.f, -170.f})
	{
		const float VisualYaw = CalculateRelativeTurretYaw(ChassisYaw, Yaw);
		TestTrue(TEXT("Chassis turns leave retained turret world yaw unchanged"), FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, ChassisYaw + VisualYaw)) < 0.001f);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankCommandTest, "NetworkTankArena.LocalTank.Command", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTankCommandTest::RunTest(const FString& Parameters)
{
	// A valid command comes out unchanged (delta time, sequence number, intent)
	{
		FTankMovementCommand ingoingCommand;
		ingoingCommand.DeltaTime = 0.0167f; // 60 FPS
		ingoingCommand.SequenceNumber = 12; // Does not matter for this test
		ingoingCommand.Intent = FTankControlIntent(1.0f, 1.0f, 45.f, true); 
		
		FTankMovementCommand outgoingCommand = SanitizeMovementCommand(ingoingCommand);
		
		TestTrue(TEXT("DeltaTime passes through"), FMath::IsNearlyEqual(outgoingCommand.DeltaTime,ingoingCommand.DeltaTime));
		TestEqual(TEXT("SequenceNumber passes through"), outgoingCommand.SequenceNumber, ingoingCommand.SequenceNumber);
		TestTrue(TEXT("AimYaw passes through"), FMath::IsNearlyEqual(outgoingCommand.Intent.AimYaw, ingoingCommand.Intent.AimYaw));
		TestTrue(TEXT("Throttle passes through"), FMath::IsNearlyEqual(outgoingCommand.Intent.Throttle, ingoingCommand.Intent.Throttle));
		TestTrue(TEXT("Turn passes through"), FMath::IsNearlyEqual(outgoingCommand.Intent.Turn, ingoingCommand.Intent.Turn));
		TestEqual(TEXT("bHasAimYaw passes through"), outgoingCommand.Intent.bHasAimYaw, ingoingCommand.Intent.bHasAimYaw);
	}

	// A default command is neutral and carries an invalid delta time
	{
		const FTankMovementCommand outgoingCommand = SanitizeMovementCommand(FTankMovementCommand{});

		TestEqual(TEXT("Default DeltaTime stays zero"), outgoingCommand.DeltaTime, 0.f);
		TestEqual(TEXT("Default SequenceNumber stays zero"), outgoingCommand.SequenceNumber, 0u);
		TestEqual(TEXT("Default throttle stays neutral"), outgoingCommand.Intent.Throttle, 0.f);
		TestEqual(TEXT("Default turn stays neutral"), outgoingCommand.Intent.Turn, 0.f);
		TestFalse(TEXT("Default command carries no aim"), outgoingCommand.Intent.bHasAimYaw);
	}

	// Invalid delta times become zero so the server can reject the command
	for (const float DeltaTime : {0.f, -0.0167f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		FTankMovementCommand ingoingCommand;
		ingoingCommand.DeltaTime = DeltaTime;

		TestEqual(TEXT("Invalid DeltaTime becomes zero"), SanitizeMovementCommand(ingoingCommand).DeltaTime, 0.f);
	}

	// Delta time is capped at 0.1 seconds
	{
		FTankMovementCommand ingoingCommand;

		ingoingCommand.DeltaTime = 0.1f;
		TestEqual(TEXT("DeltaTime at the cap passes through"), SanitizeMovementCommand(ingoingCommand).DeltaTime, 0.1f);
		ingoingCommand.DeltaTime = 0.25f;
		TestEqual(TEXT("DeltaTime above the cap is clamped"), SanitizeMovementCommand(ingoingCommand).DeltaTime, 0.1f);
		ingoingCommand.DeltaTime = std::numeric_limits<float>::max();
		TestEqual(TEXT("Huge DeltaTime is clamped"), SanitizeMovementCommand(ingoingCommand).DeltaTime, 0.1f);
		ingoingCommand.DeltaTime = 1.e-6f;
		// Exact compare, since TestEqual's float tolerance would also accept zero here
		TestTrue(TEXT("Tiny positive DeltaTime passes through"), SanitizeMovementCommand(ingoingCommand).DeltaTime == 1.e-6f);
	}

	// Sequence numbers pass through untouched, including the extremes and alongside invalid fields
	for (const uint32 SequenceNumber : {0u, 1u, MAX_uint32})
	{
		FTankMovementCommand ingoingCommand;
		ingoingCommand.DeltaTime = std::numeric_limits<float>::quiet_NaN();
		ingoingCommand.SequenceNumber = SequenceNumber;
		ingoingCommand.Intent = FTankControlIntent(std::numeric_limits<float>::quiet_NaN(), 5.f, 0.f, true);

		TestEqual(TEXT("SequenceNumber passes through"), SanitizeMovementCommand(ingoingCommand).SequenceNumber, SequenceNumber);
	}

	// The intent inside a command is sanitized like a standalone intent
	{
		FTankMovementCommand ingoingCommand;
		ingoingCommand.DeltaTime = 0.0167f;
		ingoingCommand.Intent = FTankControlIntent(5.f, -std::numeric_limits<float>::infinity(), 270.f, true);

		FTankMovementCommand outgoingCommand = SanitizeMovementCommand(ingoingCommand);

		TestEqual(TEXT("Throttle above range is clamped"), outgoingCommand.Intent.Throttle, 1.f);
		TestEqual(TEXT("Infinite turn becomes zero"), outgoingCommand.Intent.Turn, 0.f);
		TestTrue(TEXT("Out-of-range aim yaw is normalized"), FMath::IsNearlyEqual(outgoingCommand.Intent.AimYaw, -90.f, 0.001f));
		TestTrue(TEXT("Valid aim is kept"), outgoingCommand.Intent.bHasAimYaw);

		ingoingCommand.Intent = FTankControlIntent(-5.f, 0.5f, std::numeric_limits<float>::quiet_NaN(), true);
		outgoingCommand = SanitizeMovementCommand(ingoingCommand);

		TestEqual(TEXT("Throttle below range is clamped"), outgoingCommand.Intent.Throttle, -1.f);
		TestTrue(TEXT("Analog turn passes through"), FMath::IsNearlyEqual(outgoingCommand.Intent.Turn, 0.5f));
		TestTrue(TEXT("NaN aim yaw never leaves sanitizing"), FMath::IsFinite(outgoingCommand.Intent.AimYaw));
		TestFalse(TEXT("NaN aim yaw is rejected"), outgoingCommand.Intent.bHasAimYaw);
		TestTrue(TEXT("Valid DeltaTime is unaffected by a dirty intent"), FMath::IsNearlyEqual(outgoingCommand.DeltaTime, 0.0167f));
	}

	// An invalid delta time does not skip intent sanitization
	{
		FTankMovementCommand ingoingCommand;
		ingoingCommand.DeltaTime = -1.f;
		ingoingCommand.Intent = FTankControlIntent(std::numeric_limits<float>::quiet_NaN(), 3.f, 45.f, true);

		const FTankMovementCommand outgoingCommand = SanitizeMovementCommand(ingoingCommand);

		TestEqual(TEXT("NaN throttle becomes zero"), outgoingCommand.Intent.Throttle, 0.f);
		TestEqual(TEXT("Turn above range is clamped"), outgoingCommand.Intent.Turn, 1.f);
		TestTrue(TEXT("Valid aim yaw is preserved"), outgoingCommand.Intent.bHasAimYaw
			&& FMath::IsNearlyEqual(outgoingCommand.Intent.AimYaw, 45.f, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankCommandRingBufferTest, "NetworkTankArena.LocalTank.CommandRingBuffer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTankCommandRingBufferTest::RunTest(const FString& Parameters)
{
	const auto MakeCommand = [](uint32 SequenceNumber)
	{
		FTankMovementCommand Command;
		Command.SequenceNumber = SequenceNumber;
		Command.DeltaTime = 0.0167f;
		return Command;
	};

	// Commands come back oldest first
	{
		FTankCommandRingBuffer Buffer;
		TestTrue(TEXT("New buffer is empty"), Buffer.IsEmpty());
		for (uint32 SequenceNumber = 1; SequenceNumber <= 3; ++SequenceNumber)
		{
			TestTrue(TEXT("Push with room drops nothing"), Buffer.Push(MakeCommand(SequenceNumber)));
		}
		TestEqual(TEXT("Every push is stored"), Buffer.Num(), 3);
		TestTrue(TEXT("Oldest command is first"), Buffer[0].SequenceNumber == 1 && Buffer[2].SequenceNumber == 3);
	}

	// Acknowledging discards from the front up to and including the sequence number
	{
		FTankCommandRingBuffer Buffer;
		for (uint32 SequenceNumber = 5; SequenceNumber <= 9; ++SequenceNumber)
		{
			Buffer.Push(MakeCommand(SequenceNumber));
		}
		TestEqual(TEXT("Sequence older than the buffer discards nothing"), Buffer.DiscardThrough(4), 0);
		TestEqual(TEXT("Discard is inclusive"), Buffer.DiscardThrough(6), 2);
		TestTrue(TEXT("Next unacknowledged command is now first"), Buffer.Num() == 3 && Buffer[0].SequenceNumber == 7);
		TestEqual(TEXT("Repeated acknowledgement discards nothing"), Buffer.DiscardThrough(6), 0);
		TestEqual(TEXT("Sequence beyond the newest discards everything"), Buffer.DiscardThrough(100), 3);
		TestTrue(TEXT("Fully acknowledged buffer is empty"), Buffer.IsEmpty());
	}

	// A full buffer drops the oldest command and keeps order across the wrap
	{
		FTankCommandRingBuffer Buffer;
		uint32 SequenceNumber = 1;
		for (; SequenceNumber <= static_cast<uint32>(FTankCommandRingBuffer::Capacity); ++SequenceNumber)
		{
			Buffer.Push(MakeCommand(SequenceNumber));
		}
		TestTrue(TEXT("Buffer fills to capacity"), Buffer.IsFull());
		TestFalse(TEXT("Push into a full buffer reports a drop"), Buffer.Push(MakeCommand(SequenceNumber++)));
		TestEqual(TEXT("Full buffer stays at capacity"), Buffer.Num(), FTankCommandRingBuffer::Capacity);
		TestEqual(TEXT("Oldest command was dropped"), Buffer[0].SequenceNumber, 2u);

		for (int32 Push = 0; Push < FTankCommandRingBuffer::Capacity * 2 + 7; ++Push)
		{
			Buffer.Push(MakeCommand(SequenceNumber++));
		}
		bool bInOrder = true;
		for (int32 Index = 1; Index < Buffer.Num(); ++Index)
		{
			bInOrder &= Buffer[Index].SequenceNumber == Buffer[Index - 1].SequenceNumber + 1;
		}
		TestTrue(TEXT("Order survives many wraps"), bInOrder);
		TestEqual(TEXT("Newest command is last"), Buffer[Buffer.Num() - 1].SequenceNumber, SequenceNumber - 1);

		TestEqual(TEXT("Discard works across the wrap"), Buffer.DiscardThrough(Buffer[9].SequenceNumber), 10);
		Buffer.Reset();
		TestTrue(TEXT("Reset empties the buffer"), Buffer.IsEmpty());
		Buffer.Push(MakeCommand(SequenceNumber));
		TestTrue(TEXT("Buffer is usable after reset"), Buffer.Num() == 1 && Buffer[0].SequenceNumber == SequenceNumber);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankInterpolationBufferTest, "NetworkTankArena.LocalTank.Interpolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTankInterpolationBufferTest::RunTest(const FString& Parameters)
{
	const auto MakeSnapshot = [](double Time, double X, float ChassisYaw, float TurretYaw)
	{
		FTankPresentationSnapshot Snapshot;
		Snapshot.Time = Time;
		Snapshot.Location = FVector(X, 0.0, 100.0);
		Snapshot.ChassisYaw = ChassisYaw;
		Snapshot.TurretYaw = TurretYaw;
		return Snapshot;
	};
	constexpr double Interval = 0.05;
	FTankPresentationSnapshot Sampled;

	// Samples between two updates are interpolated, and outside them the nearest update is held
	{
		FTankInterpolationBuffer Buffer;
		TestFalse(TEXT("Empty buffer has nothing to sample"), Buffer.Sample(0.0, Sampled));
		Buffer.Add(MakeSnapshot(1.0, 0.0, 0.f, 0.f), Interval);
		Buffer.Add(MakeSnapshot(1.05, 50.0, 10.f, 40.f), Interval);
		TestTrue(TEXT("Sample between updates succeeds"), Buffer.Sample(1.025, Sampled));
		TestTrue(TEXT("Location is interpolated"), Sampled.Location.Equals(FVector(25.0, 0.0, 100.0), 0.001));
		TestTrue(TEXT("Chassis yaw is interpolated"), FMath::IsNearlyEqual(Sampled.ChassisYaw, 5.f, 0.001f));
		TestTrue(TEXT("Turret yaw is interpolated"), FMath::IsNearlyEqual(Sampled.TurretYaw, 20.f, 0.001f));
		Buffer.Sample(0.5, Sampled);
		TestTrue(TEXT("Before the oldest update the oldest is held"), Sampled.Location.Equals(FVector(0.0, 0.0, 100.0)));
		Buffer.Sample(2.0, Sampled);
		TestTrue(TEXT("After the newest update the newest is held"), Sampled.Location.Equals(FVector(50.0, 0.0, 100.0)));
		Buffer.Add(MakeSnapshot(1.02, 999.0, 0.f, 0.f), Interval);
		TestEqual(TEXT("Snapshot older than the newest is ignored"), Buffer.Num(), 2);
	}

	// Yaw takes the short way across the wrap
	{
		FTankInterpolationBuffer Buffer;
		Buffer.Add(MakeSnapshot(0.0, 0.0, 170.f, -170.f), Interval);
		Buffer.Add(MakeSnapshot(Interval, 0.0, -170.f, 170.f), Interval);
		Buffer.Sample(Interval * 0.5, Sampled);
		TestTrue(TEXT("Chassis yaw crosses the wrap"), FMath::Abs(FMath::FindDeltaAngleDegrees(Sampled.ChassisYaw, 180.f)) < 0.001f);
		TestTrue(TEXT("Turret yaw crosses the wrap"), FMath::Abs(FMath::FindDeltaAngleDegrees(Sampled.TurretYaw, 180.f)) < 0.001f);
	}

	// A tank that stood still waits and then moves, instead of creeping across the whole gap
	{
		FTankInterpolationBuffer Buffer;
		Buffer.Add(MakeSnapshot(0.0, 0.0, 0.f, 0.f), Interval);
		Buffer.Add(MakeSnapshot(1.0, 50.0, 0.f, 0.f), Interval);
		TestEqual(TEXT("Still gap adds a hold snapshot"), Buffer.Num(), 3);
		Buffer.Sample(0.5, Sampled);
		TestTrue(TEXT("Tank stays put during the still gap"), Sampled.Location.Equals(FVector(0.0, 0.0, 100.0)));
		Buffer.Sample(1.0 - Interval * 0.5, Sampled);
		TestTrue(TEXT("Tank moves over the last interval only"), Sampled.Location.Equals(FVector(25.0, 0.0, 100.0), 0.001));
	}

	// A late update from jitter is still interpolated across
	{
		FTankInterpolationBuffer Buffer;
		Buffer.Add(MakeSnapshot(0.0, 0.0, 0.f, 0.f), Interval);
		Buffer.Add(MakeSnapshot(0.12, 120.0, 0.f, 0.f), Interval);
		TestEqual(TEXT("Jitter gap adds no hold snapshot"), Buffer.Num(), 2);
		Buffer.Sample(0.06, Sampled);
		TestTrue(TEXT("Jitter gap is interpolated"), Sampled.Location.Equals(FVector(60.0, 0.0, 100.0), 0.001));
	}

	// The buffer is bounded and keeps the newest snapshots
	{
		FTankInterpolationBuffer Buffer;
		for (int32 Update = 0; Update < FTankInterpolationBuffer::Capacity * 3; ++Update)
		{
			Buffer.Add(MakeSnapshot(Update * Interval, Update, 0.f, 0.f), Interval);
		}
		TestEqual(TEXT("Buffer stays at capacity"), Buffer.Num(), FTankInterpolationBuffer::Capacity);
		TestTrue(TEXT("Newest snapshot is kept"), Buffer.Newest() && FMath::IsNearlyEqual(Buffer.Newest()->Location.X, FTankInterpolationBuffer::Capacity * 3.0 - 1.0));
	}
	return true;
}

#endif
