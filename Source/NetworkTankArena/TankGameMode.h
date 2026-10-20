#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TankGameMode.generated.h"

// Standard GameMode login/restart creates and possesses one tank per connection.
UCLASS()
class NETWORKTANKARENA_API ATankGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATankGameMode();
};
