// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "FX/CSTransientFX.h"

#include "Components/PointLightComponent.h"
#include "Core/CSLog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName GColorParam(TEXT("Color"));
	const FName GIntensityParam(TEXT("Intensity"));
	const FName GOpacityParam(TEXT("Opacity"));

	UStaticMeshComponent* MakeMesh(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Mesh = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->bReceivesDecals = false;
		return Mesh;
	}
}

ACSTransientFX::ACSTransientFX()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	MeshA = MakeMesh(this, TEXT("MeshA"), Root);
	MeshB = MakeMesh(this, TEXT("MeshB"), Root);
	MeshB->SetVisibility(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.f);
	Light->SetVisibility(false);

	// Referenced from the class default object, so the cooker packages them.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Additive(TEXT("/Game/FX/Materials/M_CS_FXAdditive.M_CS_FXAdditive"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Translucent(TEXT("/Game/FX/Materials/M_CS_FXTranslucent.M_CS_FXTranslucent"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flash(TEXT("/Game/FX/Materials/M_CS_FXFlash.M_CS_FXFlash"));
	SphereMesh = Sphere.Object;
	CylinderMesh = Cylinder.Object;
	PlaneMesh = Plane.Object;
	AdditiveMaterial = Additive.Object;
	TranslucentMaterial = Translucent.Object;
	FlashMaterial = Flash.Object;
}

ACSTransientFX* ACSTransientFX::Spawn(UWorld* World, const FTransform& Transform, const FParams& Params)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || World->bIsTearingDown)
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	ACSTransientFX* FX = World->SpawnActor<ACSTransientFX>(ACSTransientFX::StaticClass(), Transform, SpawnParams);
	if (FX)
	{
		FX->Start(Params);
	}
	return FX;
}

void ACSTransientFX::Start(const FParams& InParams)
{
	Params = InParams;
	Age = 0.f;

	UStaticMesh* Mesh = SphereMesh;
	if (Params.Shape == ECSFXShape::Cylinder)
	{
		Mesh = CylinderMesh;
	}
	else if (Params.Shape == ECSFXShape::Star)
	{
		Mesh = PlaneMesh;
	}

	UMaterialInterface* Base = Params.bAdditive
		? ((Params.Shape == ECSFXShape::Star && FlashMaterial) ? FlashMaterial.Get() : AdditiveMaterial.Get())
		: TranslucentMaterial.Get();
	Material = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Material)
	{
		Material->SetVectorParameterValue(GColorParam, Params.Color);
		Material->SetScalarParameterValue(GIntensityParam, Params.Intensity);
	}

	for (UStaticMeshComponent* Comp : { MeshA.Get(), MeshB.Get() })
	{
		Comp->SetStaticMesh(Mesh);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetOnlyOwnerSee(Params.bOnlyOwnerSee);
		Comp->SetOwnerNoSee(Params.bOwnerNoSee);
	}

	// The engine plane lies in XY facing +Z. Plane A is turned to face along +X
	// (a disc seen by the shooter, who looks down the barrel); plane B stands
	// along the barrel so observers to the side see the flame length.
	if (Params.Shape == ECSFXShape::Star)
	{
		MeshA->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
		MeshB->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
		MeshB->SetVisibility(true);
	}
	else if (Params.Shape == ECSFXShape::Cylinder)
	{
		// Cylinder is authored along Z and centred; stand it along X from 0.
		MeshA->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	}

	if (Params.VisibilityOwner)
	{
		SetOwner(const_cast<AActor*>(Params.VisibilityOwner));
	}

	if (Params.LightIntensity > 0.f)
	{
		Light->SetVisibility(true);
		Light->SetLightColor(Params.Color);
		Light->SetAttenuationRadius(Params.LightRadius);
	}

	ApplyAlpha(0.f);
}

void ACSTransientFX::ApplyAlpha(float Alpha)
{
	const FVector Scale = FMath::Lerp(Params.StartScale, Params.EndScale, Alpha);
	if (Params.Shape == ECSFXShape::Cylinder)
	{
		// Scale.X is the length (in units of the 100 cm mesh), Y/Z the thickness.
		MeshA->SetRelativeScale3D(FVector(Scale.Y, Scale.Z, Scale.X));
		MeshA->SetRelativeLocation(FVector(Scale.X * 50.f, 0.f, 0.f));
	}
	else
	{
		MeshA->SetRelativeScale3D(Scale);
		MeshB->SetRelativeScale3D(Scale);
	}

	const float Fade = 1.f - Alpha;
	if (Material)
	{
		Material->SetScalarParameterValue(GOpacityParam, Params.Opacity * Fade);
		if (Params.bAdditive)
		{
			Material->SetScalarParameterValue(GIntensityParam, Params.Intensity * Fade);
		}
	}
	if (Params.LightIntensity > 0.f)
	{
		Light->SetIntensity(Params.LightIntensity * Fade * Fade);
	}
}

void ACSTransientFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float Alpha = Params.Lifetime > 0.f ? Age / Params.Lifetime : 1.f;
	if (Alpha >= 1.f)
	{
		Destroy();
		return;
	}
	ApplyAlpha(Alpha);
}
