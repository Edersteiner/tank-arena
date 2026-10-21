#include "../TankGameplayMath.h"

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
	return true;
}

#endif
