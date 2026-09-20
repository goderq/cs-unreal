// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Characters/CSCharacterMovementComponent.h"

#include "GameFramework/Character.h"

UCSCharacterMovementComponent::UCSCharacterMovementComponent()
{
	NavAgentProps.bCanCrouch = true;

	MaxWalkSpeed = WalkSpeed;
	MaxWalkSpeedCrouched = CrouchSpeed;
	MaxAcceleration = 2400.f;
	BrakingDecelerationWalking = 2400.f;
	GroundFriction = 8.f;
	JumpZVelocity = 480.f;
	AirControl = 0.25f;
	bUseControllerDesiredRotation = false;
	bOrientRotationToMovement = false;
	RotationRate = FRotator(0.f, 720.f, 0.f);

	// Crouch half-height tuned to the default 88-unit capsule.
	SetCrouchedHalfHeight(60.f);
}

bool UCSCharacterMovementComponent::IsSprinting() const
{
	if (!bWantsToSprint || IsCrouching() || !IsMovingOnGround())
	{
		return false;
	}

	const FVector Velocity2D = FVector(Velocity.X, Velocity.Y, 0.f);
	if (Velocity2D.IsNearlyZero())
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FVector Forward = FRotator(0.f, Owner->GetActorRotation().Yaw, 0.f).Vector();
	return FVector::DotProduct(Forward, Velocity2D.GetSafeNormal()) >= SprintForwardDotThreshold;
}

float UCSCharacterMovementComponent::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		if (IsCrouching())
		{
			return CrouchSpeed;
		}
		if (IsSprinting())
		{
			return SprintSpeed;
		}
		return WalkSpeed;
	}

	return Super::GetMaxSpeed();
}

void UCSCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// FLAG_Custom_0 is the first slot the engine leaves free for games.
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UCSCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UCSCharacterMovementComponent* Mutable = const_cast<UCSCharacterMovementComponent*>(this);
		Mutable->ClientPredictionData = new FNetworkPredictionData_Client_CSCharacter(*this);
		Mutable->ClientPredictionData->MaxSmoothNetUpdateDist = 92.f;
		Mutable->ClientPredictionData->NoSmoothNetUpdateDist = 140.f;
	}

	return ClientPredictionData;
}

// ---------------------------------------------------------------------------
// FSavedMove_CSCharacter
// ---------------------------------------------------------------------------

void FSavedMove_CSCharacter::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = 0;
}

uint8 FSavedMove_CSCharacter::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}
	return Result;
}

bool FSavedMove_CSCharacter::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	// Moves with different sprint intent must not be merged, otherwise the
	// server replays a different speed than the client predicted.
	const FSavedMove_CSCharacter* Other = static_cast<const FSavedMove_CSCharacter*>(NewMove.Get());
	if (Other && Other->bSavedWantsToSprint != bSavedWantsToSprint)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FSavedMove_CSCharacter::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	if (const UCSCharacterMovementComponent* Movement =
			Cast<UCSCharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		bSavedWantsToSprint = Movement->bWantsToSprint ? 1 : 0;
	}
}

void FSavedMove_CSCharacter::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	if (UCSCharacterMovementComponent* Movement =
			Cast<UCSCharacterMovementComponent>(Character->GetCharacterMovement()))
	{
		Movement->bWantsToSprint = bSavedWantsToSprint != 0;
	}
}

// ---------------------------------------------------------------------------
// FNetworkPredictionData_Client_CSCharacter
// ---------------------------------------------------------------------------

FNetworkPredictionData_Client_CSCharacter::FNetworkPredictionData_Client_CSCharacter(
	const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_CSCharacter::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_CSCharacter());
}
