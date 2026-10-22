#include "TankPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "TankGameplayMath.h"
#include "TankMovementCommand.h"
#include "TankMovementComponent.h"

ATankPawn::ATankPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(20.f);
	SetMinNetUpdateFrequency(20.f);
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CollisionRoot = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionRoot"));
	SetRootComponent(CollisionRoot);
	CollisionRoot->InitCapsuleSize(90.f, 100.f);
	CollisionRoot->SetCollisionProfileName(TEXT("Pawn"));
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionRoot->SetSimulatePhysics(false);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionRoot);
	VisualRoot->SetRelativeLocation(FVector(0.0, 0.0, -65.0));
	ChassisVisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ChassisVisualPivot"));
	ChassisVisualPivot->SetupAttachment(VisualRoot);
	ChassisMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChassisMesh"));
	ChassisMesh->SetupAttachment(ChassisVisualPivot);
	TurretYawPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretYawPivot"));
	TurretYawPivot->SetupAttachment(VisualRoot);
	TurretYawPivot->SetRelativeLocation(FVector(0.0, 0.0, 40.0));
	TurretMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(TurretYawPivot);
	BarrelMesh->SetRelativeLocation(FVector(70.0, 0.0, 0.0));
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCanEverAffectNavigation(false);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionRoot);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-60.0, 45.0, 0.0));
	CameraBoom->TargetArmLength = 1500.f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	CameraBoom->AddTickPrerequisiteActor(this);
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->ProjectionMode = ECameraProjectionMode::Orthographic;
	FollowCamera->OrthoWidth = 2400.f;
	FollowCamera->bUsePawnControlRotation = false;

	TankMovement = CreateDefaultSubobject<UTankMovementComponent>(TEXT("TankMovement"));
	TankMovement->SetUpdatedComponent(CollisionRoot);
}

UPawnMovementComponent* ATankPawn::GetMovementComponent() const
{
	return TankMovement;
}

void ATankPawn::BeginPlay()
{
	Super::BeginPlay();
	using namespace NetworkTankArena::LocalTank;
	if (HasAuthority())
	{
		CollisionRoot->SetWorldScale3D(FVector::OneVector);
		CollisionRoot->SetWorldRotation(FRotator(0.0, NormalizeYaw(GetActorRotation().Yaw), 0.0));
	}
	CollisionRoot->SetSimulatePhysics(false);
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	for (UStaticMeshComponent* Mesh : {ChassisMesh.Get(), TurretMesh.Get(), BarrelMesh.Get()})
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// Import-axis corrections belong on meshes, not the shared yaw/bob pivots.
	VisualRoot->SetRelativeRotation(FRotator::ZeroRotator);
	TurretYawPivot->SetRelativeRotation(FRotator::ZeroRotator);
	BaseVisualTransform = VisualRoot->GetRelativeTransform();
	BaseChassisTransform = ChassisVisualPivot->GetRelativeTransform();
	TankMovement->SetUpdatedComponent(CollisionRoot);
	TankMovement->InitializeGameplayPlane();
	PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);
	UpdateControllerTickPrerequisite();
	ApplyPresentation(0.f);
	
	// Set initial turret yaw after initial chassis transformation
	CanonicalTurretWorldYaw = NormalizeYaw(GetActorRotation().Yaw);	
}

void ATankPawn::ApplyMovementCommand(const FTankMovementCommand& Command)
{
	using namespace NetworkTankArena::LocalTank;
	if (GetLocalRole() < ROLE_AutonomousProxy)
	{
		return;
	}
	// Sanitized again at the authority boundary.
	const FTankMovementCommand Sanitized = SanitizeMovementCommand(Command);
	if (!IsValidDeltaTime(Sanitized.DeltaTime))
	{
		return;
	}
	TankMovement->SimulateMovement(Sanitized.Intent, Sanitized.DeltaTime);
	if (Sanitized.Intent.bHasAimYaw)
	{
		CanonicalTurretWorldYaw = Sanitized.Intent.AimYaw;
	}
}

FTankMovementState ATankPawn::GetMovementState() const
{
	FTankMovementState State;
	State.Location = GetActorLocation();
	State.ChassisYaw = NetworkTankArena::LocalTank::NormalizeYaw(GetActorRotation().Yaw);
	return State;
}

bool ATankPawn::ApplyServerState(const FTankMovementState& State)
{
	using namespace NetworkTankArena::LocalTank;
	if (HasAuthority() || !IsFiniteVector(State.Location) || !FMath::IsFinite(State.ChassisYaw))
	{
		return false;
	}
	return SetActorLocationAndRotation(State.Location, FRotator(0.0, NormalizeYaw(State.ChassisYaw), 0.0),
		false, nullptr, ETeleportType::TeleportPhysics);
}

double ATankPawn::GetGameplayPlaneZ() const
{
	return TankMovement->GetGameplayPlaneZ();
}

void ATankPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Gameplay movement only happens per received command, never per server frame.
	ApplyPresentation(DeltaTime);
}

void ATankPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The owning client predicts its own aim.
	DOREPLIFETIME_CONDITION(ATankPawn, CanonicalTurretWorldYaw, COND_SkipOwner);
}

void ATankPawn::OnRep_CanonicalTurretWorldYaw()
{
	// Initial replication can notify before BeginPlay captures Blueprint visual offsets.
	if (HasActorBegunPlay())
	{
		ApplyPresentation(0.f);
	}
}

void ATankPawn::PossessedBy(AController* NewController)
{
	// APawn promotes a remote player's pawn to an autonomous proxy, which FRepMovement skips.
	// The owning client positions its tank from command acknowledgements instead.
	Super::PossessedBy(NewController);
	TankMovement->StopMovementImmediately();
}

void ATankPawn::Restart()
{
	Super::Restart();
	TankMovement->StopMovementImmediately();
	UpdateControllerTickPrerequisite();
}

void ATankPawn::ApplyPresentation(float DeltaTime)
{
	using namespace NetworkTankArena::LocalTank;
	if (IsValidDeltaTime(DeltaTime))
	{
		HoverPhase = FMath::Fmod(HoverPhase + 2.0 * UE_PI * NonNegativeFinite(HoverFrequencyHz) * DeltaTime, 2.0 * UE_PI);
		// Derive local banking from observed chassis motion on every machine. No intent
		// or cosmetic transforms need replication; stepped client motion is intentional.
		const float TurnRate = NonNegativeFinite(TankMovement->GetTurnRateDegreesPerSecond());
		const float ObservedTurn = TurnRate > UE_SMALL_NUMBER
			? SanitizeAxis(FMath::FindDeltaAngleDegrees(PreviousPresentationYaw, GetActorRotation().Yaw) / (DeltaTime * TurnRate)) : 0.f;
		const float TargetBank = -ObservedTurn * FMath::Min(NonNegativeFinite(MaxBankDegrees), 15.f);
		CurrentBankDegrees = FMath::FInterpTo(CurrentBankDegrees, TargetBank, DeltaTime, NonNegativeFinite(BankInterpSpeed));
		PreviousPresentationYaw = NormalizeYaw(GetActorRotation().Yaw);
	}
	const double Bob = FMath::Sin(HoverPhase) * NonNegativeFinite(HoverAmplitude);
	VisualRoot->SetRelativeLocation(BaseVisualTransform.GetLocation() + FVector(0.0, 0.0, Bob));
	ChassisVisualPivot->SetRelativeRotation(BaseChassisTransform.Rotator() + FRotator(0.0, 0.0, CurrentBankDegrees));
	TurretYawPivot->SetRelativeRotation(FRotator(0.0,
		CalculateRelativeTurretYaw(GetActorRotation().Yaw, CanonicalTurretWorldYaw), 0.0));
}

void ATankPawn::UpdateControllerTickPrerequisite()
{
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController = GetController();
	if (AController* Current = TickController.Get())
	{
		// Only controller -> pawn -> camera. Movement has no independent tick.
		AddTickPrerequisiteActor(Current);
	}
}

void ATankPawn::NotifyControllerChanged()
{
	TankMovement->StopMovementImmediately();
	UpdateControllerTickPrerequisite();
	Super::NotifyControllerChanged();
}

void ATankPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AController* Previous = TickController.Get())
	{
		RemoveTickPrerequisiteActor(Previous);
	}
	TickController.Reset();
	Super::EndPlay(EndPlayReason);
}
