#include "TankGameMode.h"

#include "TankPawn.h"
#include "TankPlayerController.h"
#include "UObject/ConstructorHelpers.h"

ATankGameMode::ATankGameMode()
{
	// Reuse the established Blueprint visual composition and input asset assignments.
	static ConstructorHelpers::FClassFinder<ATankPawn> TankClass(TEXT("/Game/Blueprints/BP_Tank"));
	static ConstructorHelpers::FClassFinder<ATankPlayerController> ControllerClass(TEXT("/Game/Blueprints/BP_TankPlayerController"));
	DefaultPawnClass = TankClass.Succeeded() ? TankClass.Class.Get() : ATankPawn::StaticClass();
	PlayerControllerClass = ControllerClass.Succeeded() ? ControllerClass.Class.Get() : ATankPlayerController::StaticClass();
}
