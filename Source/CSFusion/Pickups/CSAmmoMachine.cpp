// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Pickups/CSAmmoMachine.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"

namespace
{
	// Built by Scripts/bootstrap_v20.py; a plain box stands in until then.
	const TCHAR* MachineMeshPath = TEXT("/Game/Environment/Props/SM_AmmoMachine.SM_AmmoMachine");
	const TCHAR* FallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	const FLinearColor ScreenColor(1.f, 0.62f, 0.18f);
}

ACSAmmoMachine::ACSAmmoMachine()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCanEverAffectNavigation(true);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetRelativeLocation(FVector(55.f, 0.f, 150.f));
	Glow->SetLightColor(ScreenColor);
	Glow->SetIntensity(1800.f);
	Glow->SetAttenuationRadius(420.f);
	Glow->SetCastShadows(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(46.f, 0.f, 212.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(16.f);
	Label->SetTextRenderColor(FColor(255, 170, 60));
	Label->SetText(FText::FromString(TEXT("AMMO")));
}

void ACSAmmoMachine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MachineMeshPath))
	{
		Body->SetStaticMesh(Mesh);
		Body->SetRelativeScale3D(FVector(1.f));
		Body->SetRelativeLocation(FVector::ZeroVector);
	}
	else if (UStaticMesh* Box = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath))
	{
		// 90 x 110 x 220 cm cabinet, standing on the floor.
		Body->SetStaticMesh(Box);
		Body->SetRelativeScale3D(FVector(0.9f, 1.1f, 2.2f));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	}
}

void ACSAmmoMachine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// A slow breathing glow so machines catch the eye from across the map,
	// and a bright blink right after a purchase.
	Phase += DeltaSeconds;
	VendFlash = FMath::Max(0.f, VendFlash - DeltaSeconds * 2.5f);
	const float Breath = 0.8f + 0.2f * FMath::Sin(Phase * 2.2f);
	Glow->SetIntensity(1800.f * Breath + 9000.f * VendFlash);
}

void ACSAmmoMachine::PlayVend()
{
	VendFlash = 1.f;
}

TArray<ACSAmmoMachine*> ACSAmmoMachine::GetAllSorted(const UObject* WorldContextObject)
{
	TArray<ACSAmmoMachine*> Machines;
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return Machines;
	}
	for (TActorIterator<ACSAmmoMachine> It(const_cast<UWorld*>(World)); It; ++It)
	{
		Machines.Add(*It);
	}
	Machines.Sort([](const ACSAmmoMachine& A, const ACSAmmoMachine& B) { return A.GetName() < B.GetName(); });
	return Machines;
}

ACSAmmoMachine* ACSAmmoMachine::FindByIndex(const UObject* WorldContextObject, int32 Index)
{
	const TArray<ACSAmmoMachine*> Machines = GetAllSorted(WorldContextObject);
	return Machines.IsValidIndex(Index) ? Machines[Index] : nullptr;
}

int32 ACSAmmoMachine::GetSortedIndex() const
{
	return GetAllSorted(this).IndexOfByKey(this);
}

FVector ACSAmmoMachine::GetUsePoint() const
{
	return GetActorLocation() + GetActorForwardVector() * 80.f + FVector(0.f, 0.f, 90.f);
}
