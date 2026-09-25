// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "FX/CSFXBuilderCommandlet.h"

#include "Core/CSLog.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#endif

UCSFXBuilderCommandlet::UCSFXBuilderCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

#if WITH_EDITOR
namespace CSFXBuilder
{
	const TCHAR* const TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight.DirectionalBurstLightweight");
	const TCHAR* const OutDir = TEXT("/Game/FX/Niagara");
	const TCHAR* const AdditiveMaterial = TEXT("/Game/FX/Materials/M_CS_ParticleAdditive.M_CS_ParticleAdditive");
	const TCHAR* const TranslucentMaterial = TEXT("/Game/FX/Materials/M_CS_ParticleTranslucent.M_CS_ParticleTranslucent");

	int32 Failures = 0;

	// --- Reflection --------------------------------------------------------

	// The lightweight emitter type lives in Niagara's Internal headers; reach
	// it as a plain UObject (UObject is its first base) and use reflection.
	UObject* StatelessOf(const FNiagaraEmitterHandle& Handle)
	{
		return reinterpret_cast<UObject*>(Handle.GetStatelessEmitter());
	}

	FString Export(const FProperty* Property, const void* Container, const UObject* Owner)
	{
		FString Text;
		Property->ExportText_InContainer(0, Text, Container, Container, const_cast<UObject*>(Owner), PPF_None);
		return Text;
	}

	/** Sets one property from UE text (as the details panel copies it) and runs the owner's edit hooks. */
	void Set(UObject* Object, const TCHAR* Name, const FString& Text)
	{
		FProperty* Property = Object ? FindFProperty<FProperty>(Object->GetClass(), Name) : nullptr;
		if (!Property)
		{
			UE_LOG(LogCS, Error, TEXT("FX build: %s has no property %s"), Object ? *Object->GetClass()->GetName() : TEXT("null"), Name);
			++Failures;
			return;
		}
		Object->PreEditChange(Property);
		if (!Property->ImportText_InContainer(*Text, Object, Object, PPF_None))
		{
			UE_LOG(LogCS, Error, TEXT("FX build: %s.%s rejected '%s'"), *Object->GetClass()->GetName(), Name, *Text);
			++Failures;
		}
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(Event);
	}

	TArray<UObject*> ObjectList(UObject* Owner, const TCHAR* ListName)
	{
		TArray<UObject*> Result;
		const FArrayProperty* List = FindFProperty<FArrayProperty>(Owner->GetClass(), ListName);
		const FObjectPropertyBase* Inner = List ? CastField<FObjectPropertyBase>(List->Inner) : nullptr;
		if (!Inner)
		{
			return Result;
		}
		FScriptArrayHelper Array(List, List->ContainerPtrToValuePtr<void>(Owner));
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			Result.Add(Inner->GetObjectPropertyValue(Array.GetRawPtr(i)));
		}
		return Result;
	}

	UObject* Module(UObject* Emitter, const TCHAR* Suffix)
	{
		const FString ClassName = FString(TEXT("NiagaraStatelessModule_")) + Suffix;
		for (UObject* Item : ObjectList(Emitter, TEXT("Modules")))
		{
			if (Item && Item->GetClass()->GetName() == ClassName)
			{
				return Item;
			}
		}
		UE_LOG(LogCS, Error, TEXT("FX build: no module %s"), *ClassName);
		++Failures;
		return nullptr;
	}

	// --- Distribution text ------------------------------------------------

	FString F(float V) { return FString::Printf(TEXT("%f"), V); }

	FString RangeF(float Min, float Max)
	{
		return Min == Max
			? FString::Printf(TEXT("(Mode=UniformConstant,Min=%s,Max=%s,ChannelConstantsAndRanges=(%s))"), *F(Min), *F(Min), *F(Min))
			: FString::Printf(TEXT("(Mode=UniformRange,Min=%s,Max=%s,ChannelConstantsAndRanges=(%s,%s))"), *F(Min), *F(Max), *F(Min), *F(Max));
	}

	FString RangeInt(int32 Min, int32 Max)
	{
		return FString::Printf(TEXT("(Mode=%s,Min=%d,Max=%d)"), Min == Max ? TEXT("UniformConstant") : TEXT("UniformRange"), Min, Max);
	}

	/** Square sprites of a random size between Min and Max. */
	FString SizeUniform(float Min, float Max)
	{
		return FString::Printf(TEXT("(Mode=UniformRange,Min=(X=%s,Y=%s),Max=(X=%s,Y=%s),ChannelConstantsAndRanges=(%s,%s))"),
			*F(Min), *F(Min), *F(Max), *F(Max), *F(Min), *F(Max));
	}

	/** Stretched sprites: width and length vary separately. */
	FString SizeNonUniform(FVector2f Min, FVector2f Max)
	{
		return FString::Printf(TEXT("(Mode=NonUniformRange,Min=(X=%s,Y=%s),Max=(X=%s,Y=%s),ChannelConstantsAndRanges=(%s,%s,%s,%s))"),
			*F(Min.X), *F(Min.Y), *F(Max.X), *F(Max.Y), *F(Min.X), *F(Min.Y), *F(Max.X), *F(Max.Y));
	}

	FString Vec3(const FVector3f& V)
	{
		return FString::Printf(TEXT("(Mode=NonUniformConstant,Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),ChannelConstantsAndRanges=(%s,%s,%s))"),
			*F(V.X), *F(V.Y), *F(V.Z), *F(V.X), *F(V.Y), *F(V.Z), *F(V.X), *F(V.Y), *F(V.Z));
	}

	FString ColorRange(const FLinearColor& A, const FLinearColor& B)
	{
		return FString::Printf(TEXT("(Mode=NonUniformRange,ChannelConstantsAndRanges=(%s,%s,%s,%s,%s,%s,%s,%s))"),
			*F(A.R), *F(A.G), *F(A.B), *F(A.A), *F(B.R), *F(B.G), *F(B.B), *F(B.A));
	}

	FString Curve(std::initializer_list<FVector2f> Keys)
	{
		FString Text = TEXT("(Keys=(");
		bool bFirst = true;
		for (const FVector2f& Key : Keys)
		{
			Text += FString::Printf(TEXT("%s(Time=%s,Value=%s)"), bFirst ? TEXT("") : TEXT(","), *F(Key.X), *F(Key.Y));
			bFirst = false;
		}
		return Text + TEXT("))");
	}

	/** Colour multiplier over life: rgb stays, alpha follows the keys. */
	FString AlphaOverLife(std::initializer_list<FVector2f> AlphaKeys)
	{
		const FString One = Curve({ FVector2f(0.f, 1.f), FVector2f(1.f, 1.f) });
		return FString::Printf(TEXT("(Mode=NonUniformCurve,ChannelCurves=(%s,%s,%s,%s))"), *One, *One, *One, *Curve(AlphaKeys));
	}

	/** Sprite size multiplier over life, same on both axes. */
	FString SizeOverLife(float Start, float End)
	{
		return FString::Printf(TEXT("(Mode=UniformCurve,ChannelCurves=(%s))"), *Curve({ FVector2f(0.f, Start), FVector2f(1.f, End) }));
	}

	// --- Effect description -------------------------------------------------

	enum class EFade : uint8
	{
		Quick,	// full at birth, gone at death (sparks, chips)
		Puff,	// quick fade in, long fade out (dust, smoke)
	};

	/**
	 * One emitter. Particles leave along the system's +X axis in a cone (the
	 * game spawns the system rotated so +X is the surface normal, the muzzle
	 * direction or up).
	 */
	struct FLayer
	{
		const TCHAR* Name = TEXT("Layer");
		bool bAdditive = false;
		FIntPoint Count = FIntPoint(4, 6);
		FVector2f Lifetime = FVector2f(0.3f, 0.5f);
		FLinearColor ColorA = FLinearColor::White;
		FLinearColor ColorB = FLinearColor::White;
		/** Square size range, cm; or a stretched size when bStreak. */
		FVector2f Size = FVector2f(2.f, 4.f);
		bool bStreak = false;
		FVector2f StreakMin = FVector2f(0.5f, 2.f);
		FVector2f StreakMax = FVector2f(1.f, 4.f);
		FVector2f Speed = FVector2f(100.f, 300.f);
		float ConeAngle = 40.f;
		float Gravity = -980.f;
		float Drag = 1.f;
		FVector2f Growth = FVector2f(1.f, 1.f);
		EFade Fade = EFade::Quick;
		float SpawnRadius = 0.f;
	};

	struct FEffect
	{
		const TCHAR* Name;
		float Bounds;
		TArray<FLayer> Layers;
	};

	void ConfigureLayer(UNiagaraSystem* System, FNiagaraEmitterHandle& Handle, const FLayer& L, float Bounds)
	{
		Handle.SetName(FName(L.Name), *System);
		UObject* Emitter = StatelessOf(Handle);
		const float LongestLife = L.Lifetime.Y;

		Set(Emitter, TEXT("SpawnInfos"), FString::Printf(TEXT("((Type=Burst,SpawnTime=0.000000,Amount=%s))"), *RangeInt(L.Count.X, L.Count.Y)));
		Set(Emitter, TEXT("EmitterState"), FString::Printf(
			TEXT("(InactiveResponse=Complete,LoopBehavior=Once,LoopCount=1,LoopDurationMode=Fixed,LoopDuration=%s)"), *RangeF(LongestLife, LongestLife)));
		Set(Emitter, TEXT("FixedBounds"), FString::Printf(TEXT("(Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),IsValid=True)"),
			*F(-Bounds), *F(-Bounds), *F(-Bounds), *F(Bounds), *F(Bounds), *F(Bounds)));

		UObject* Init = Module(Emitter, TEXT("InitializeParticle"));
		Set(Init, TEXT("LifetimeDistribution"), RangeF(L.Lifetime.X, L.Lifetime.Y));
		Set(Init, TEXT("ColorDistribution"), ColorRange(L.ColorA, L.ColorB));
		Set(Init, TEXT("SpriteSizeDistribution"), L.bStreak ? SizeNonUniform(L.StreakMin, L.StreakMax) : SizeUniform(L.Size.X, L.Size.Y));
		Set(Init, TEXT("SpriteRotationDistribution"), L.bStreak ? RangeF(0.f, 0.f) : RangeF(0.f, 360.f));

		UObject* Shape = Module(Emitter, TEXT("ShapeLocation"));
		Set(Shape, TEXT("bModuleEnabled"), L.SpawnRadius > 0.f ? TEXT("True") : TEXT("False"));
		Set(Shape, TEXT("ShapePrimitive"), TEXT("Sphere"));
		Set(Shape, TEXT("SphereRadius"), RangeF(0.f, FMath::Max(L.SpawnRadius, 1.f)));

		UObject* Velocity = Module(Emitter, TEXT("AddVelocity"));
		Set(Velocity, TEXT("ConeVelocityDistribution"), RangeF(L.Speed.X, L.Speed.Y));
		Set(Velocity, TEXT("ConeAngle"), F(L.ConeAngle));

		Set(Module(Emitter, TEXT("GravityForce")), TEXT("GravityDistribution"), Vec3(FVector3f(0.f, 0.f, L.Gravity)));
		UObject* Drag = Module(Emitter, TEXT("Drag"));
		Set(Drag, TEXT("bModuleEnabled"), L.Drag > 0.f ? TEXT("True") : TEXT("False"));
		Set(Drag, TEXT("DragDistribution"), RangeF(L.Drag, L.Drag));

		UObject* Color = Module(Emitter, TEXT("ScaleColor"));
		Set(Color, TEXT("ScaleDistribution"), L.Fade == EFade::Puff
			? AlphaOverLife({ FVector2f(0.f, 0.f), FVector2f(0.08f, 1.f), FVector2f(1.f, 0.f) })
			: AlphaOverLife({ FVector2f(0.f, 1.f), FVector2f(0.6f, 0.8f), FVector2f(1.f, 0.f) }));

		UObject* Grow = Module(Emitter, TEXT("ScaleSpriteSize"));
		const bool bGrows = L.Growth.X != 1.f || L.Growth.Y != 1.f;
		Set(Grow, TEXT("bModuleEnabled"), bGrows ? TEXT("True") : TEXT("False"));
		Set(Grow, TEXT("ScaleDistribution"), SizeOverLife(L.Growth.X, L.Growth.Y));

		// Streaks stretch with speed (the template's setting); round puffs do not.
		Set(Module(Emitter, TEXT("ScaleSpriteSizeBySpeed")), TEXT("bModuleEnabled"), L.bStreak ? TEXT("True") : TEXT("False"));
		Set(Module(Emitter, TEXT("SpriteRotationRate")), TEXT("bModuleEnabled"), L.bStreak ? TEXT("False") : TEXT("True"));
		Set(Module(Emitter, TEXT("SpriteRotationRate")), TEXT("RotationRateDistribution"), RangeF(-90.f, 90.f));

		for (UObject* Renderer : ObjectList(Emitter, TEXT("RendererProperties")))
		{
			Set(Renderer, TEXT("Material"), FString::Printf(TEXT("\"%s\""), L.bAdditive ? AdditiveMaterial : TranslucentMaterial));
			Set(Renderer, TEXT("Alignment"), L.bStreak ? TEXT("VelocityAligned") : TEXT("Unaligned"));
			Set(Renderer, TEXT("SortMode"), L.bAdditive ? TEXT("None") : TEXT("ViewDepth"));
			Set(Renderer, TEXT("bCastShadows"), TEXT("False"));
		}
		// Rebuilds the emitter's compiled particle layout after the module edits.
		Emitter->PostEditChange();
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.Error = GError;
		return UPackage::SavePackage(Package, Asset, *File, Args);
	}

	bool Build(const FEffect& Effect)
	{
		UNiagaraSystem* Template = LoadObject<UNiagaraSystem>(nullptr, TemplatePath);
		if (!Template)
		{
			UE_LOG(LogCS, Error, TEXT("FX build: template missing: %s"), TemplatePath);
			++Failures;
			return false;
		}
		const FString PackageName = FString(OutDir) / Effect.Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		if (UObject* Old = StaticFindObject(UObject::StaticClass(), Package, Effect.Name))
		{
			// Rebuilt from the template every run; the old one goes away.
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Old->ClearFlags(RF_Public | RF_Standalone);
		}
		UNiagaraSystem* System = DuplicateObject<UNiagaraSystem>(Template, Package, Effect.Name);
		System->SetFlags(RF_Public | RF_Standalone);
		System->bFixedBounds = true;
		System->SetFixedBounds(FBox(FVector(-Effect.Bounds), FVector(Effect.Bounds)));

		// One template emitter becomes the first layer; the rest are copies.
		const FNiagaraEmitterHandle First = System->GetEmitterHandles()[0];
		for (int32 i = 1; i < Effect.Layers.Num(); ++i)
		{
			System->DuplicateEmitterHandle(First, FName(Effect.Layers[i].Name));
		}
		for (int32 i = 0; i < Effect.Layers.Num(); ++i)
		{
			ConfigureLayer(System, System->GetEmitterHandles()[i], Effect.Layers[i], Effect.Bounds);
		}

		System->RequestCompile(false);
		System->WaitForCompilationComplete(false, false);
		FAssetRegistryModule::AssetCreated(System);
		const bool bSaved = SaveAsset(System);
		UE_LOG(LogCS, Display, TEXT("FX build: %s, %d layer(s) -> %s"), Effect.Name, Effect.Layers.Num(), bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
		Failures += bSaved ? 0 : 1;
		return bSaved;
	}

	// --- The project's effects ---------------------------------------------

	FLayer Dust(const TCHAR* Name, const FLinearColor& A, const FLinearColor& B, FVector2f Size, FIntPoint Count, FVector2f Life, FVector2f Growth)
	{
		FLayer L;
		L.Name = Name;
		L.Count = Count;
		L.Lifetime = Life;
		L.ColorA = A;
		L.ColorB = B;
		L.Size = Size;
		L.Speed = FVector2f(60.f, 170.f);
		L.ConeAngle = 40.f;
		L.Gravity = -40.f;
		L.Drag = 3.f;
		L.Growth = Growth;
		L.Fade = EFade::Puff;
		L.SpawnRadius = 2.f;
		return L;
	}

	FLayer Debris(const TCHAR* Name, const FLinearColor& A, const FLinearColor& B, FVector2f Size, FIntPoint Count, FVector2f Speed)
	{
		FLayer L;
		L.Name = Name;
		L.Count = Count;
		L.Lifetime = FVector2f(0.35f, 0.7f);
		L.ColorA = A;
		L.ColorB = B;
		L.Size = Size;
		L.Speed = Speed;
		L.ConeAngle = 45.f;
		L.Gravity = -980.f;
		L.Drag = 0.4f;
		return L;
	}

	FLayer Sparks(const TCHAR* Name, FIntPoint Count, FVector2f Life, FVector2f Speed, const FLinearColor& A, const FLinearColor& B)
	{
		FLayer L;
		L.Name = Name;
		L.bAdditive = true;
		L.Count = Count;
		L.Lifetime = Life;
		L.ColorA = A;
		L.ColorB = B;
		L.bStreak = true;
		L.StreakMin = FVector2f(1.2f, 5.f);
		L.StreakMax = FVector2f(2.f, 10.f);
		L.Speed = Speed;
		L.ConeAngle = 45.f;
		L.Gravity = -700.f;
		L.Drag = 1.2f;
		return L;
	}

	// Sizes are what reads at 5-15 m in a bright level; anything under ~1 cm
	// is sub-pixel at that range and simply vanishes.
	TArray<FEffect> Effects()
	{
		const FLinearColor Spark(12.f, 6.f, 1.8f, 1.f);
		const FLinearColor SparkHot(16.f, 9.f, 3.5f, 1.f);
		TArray<FEffect> List;

		List.Add({ TEXT("NS_Impact_Stone"), 300.f, {
			Dust(TEXT("Dust"), FLinearColor(0.36f, 0.35f, 0.33f, 0.85f), FLinearColor(0.46f, 0.44f, 0.41f, 0.95f), FVector2f(30.f, 45.f), FIntPoint(7, 9), FVector2f(0.6f, 1.f), FVector2f(1.f, 2.6f)),
			Debris(TEXT("Chips"), FLinearColor(0.06f, 0.055f, 0.05f, 1.f), FLinearColor(0.12f, 0.11f, 0.1f, 1.f), FVector2f(4.f, 6.5f), FIntPoint(10, 14), FVector2f(250.f, 600.f)),
			Sparks(TEXT("Sparks"), FIntPoint(2, 4), FVector2f(0.06f, 0.14f), FVector2f(500.f, 1100.f), Spark, SparkHot) } });

		FLayer MetalSparks = Sparks(TEXT("Sparks"), FIntPoint(14, 20), FVector2f(0.2f, 0.45f), FVector2f(400.f, 1300.f), Spark, SparkHot);
		FLayer MetalFlash;
		MetalFlash.Name = TEXT("Flash");
		MetalFlash.bAdditive = true;
		MetalFlash.Count = FIntPoint(1, 1);
		MetalFlash.Lifetime = FVector2f(0.05f, 0.05f);
		MetalFlash.ColorA = FLinearColor(10.f, 7.f, 3.f, 1.f);
		MetalFlash.ColorB = MetalFlash.ColorA;
		MetalFlash.Size = FVector2f(18.f, 24.f);
		MetalFlash.Speed = FVector2f(0.f, 0.f);
		MetalFlash.Gravity = 0.f;
		MetalFlash.Drag = 0.f;
		List.Add({ TEXT("NS_Impact_Metal"), 400.f, {
			MetalSparks,
			MetalFlash,
			Dust(TEXT("Smoke"), FLinearColor(0.55f, 0.55f, 0.55f, 0.45f), FLinearColor(0.65f, 0.65f, 0.65f, 0.55f), FVector2f(14.f, 22.f), FIntPoint(2, 3), FVector2f(0.5f, 0.8f), FVector2f(1.f, 2.4f)) } });

		FLayer Splinters = Debris(TEXT("Splinters"), FLinearColor(0.26f, 0.16f, 0.07f, 1.f), FLinearColor(0.42f, 0.28f, 0.13f, 1.f), FVector2f(1.f, 1.f), FIntPoint(8, 12), FVector2f(220.f, 560.f));
		Splinters.bStreak = true;
		Splinters.StreakMin = FVector2f(2.f, 6.f);
		Splinters.StreakMax = FVector2f(3.f, 10.f);
		List.Add({ TEXT("NS_Impact_Wood"), 300.f, {
			Splinters,
			Dust(TEXT("Dust"), FLinearColor(0.38f, 0.29f, 0.19f, 0.8f), FLinearColor(0.48f, 0.37f, 0.25f, 0.9f), FVector2f(26.f, 40.f), FIntPoint(5, 7), FVector2f(0.5f, 0.9f), FVector2f(1.f, 2.4f)) } });

		FLayer Clods = Debris(TEXT("Clods"), FLinearColor(0.08f, 0.06f, 0.035f, 1.f), FLinearColor(0.15f, 0.11f, 0.07f, 1.f), FVector2f(3.f, 6.f), FIntPoint(12, 18), FVector2f(200.f, 550.f));
		Clods.ConeAngle = 30.f;
		List.Add({ TEXT("NS_Impact_Dirt"), 300.f, {
			Clods,
			Dust(TEXT("Dust"), FLinearColor(0.36f, 0.29f, 0.2f, 0.8f), FLinearColor(0.45f, 0.37f, 0.26f, 0.9f), FVector2f(36.f, 60.f), FIntPoint(7, 9), FVector2f(0.9f, 1.4f), FVector2f(1.f, 2.8f)) } });

		FLayer Mist = Dust(TEXT("Mist"), FLinearColor(0.3f, 0.01f, 0.01f, 0.8f), FLinearColor(0.45f, 0.02f, 0.02f, 0.9f), FVector2f(25.f, 40.f), FIntPoint(5, 7), FVector2f(0.3f, 0.55f), FVector2f(1.f, 2.2f));
		Mist.Gravity = -100.f;
		FLayer Drops = Debris(TEXT("Drops"), FLinearColor(0.18f, 0.005f, 0.005f, 1.f), FLinearColor(0.32f, 0.01f, 0.01f, 1.f), FVector2f(2.f, 3.5f), FIntPoint(8, 12), FVector2f(150.f, 400.f));
		List.Add({ TEXT("NS_Impact_Blood"), 250.f, { Mist, Drops } });

		FLayer Muzzle = Dust(TEXT("Smoke"), FLinearColor(0.7f, 0.7f, 0.7f, 0.3f), FLinearColor(0.8f, 0.8f, 0.8f, 0.4f), FVector2f(10.f, 18.f), FIntPoint(2, 3), FVector2f(0.6f, 1.f), FVector2f(1.f, 3.5f));
		Muzzle.Speed = FVector2f(30.f, 90.f);
		Muzzle.ConeAngle = 15.f;
		Muzzle.Gravity = 30.f;
		Muzzle.SpawnRadius = 0.f;
		List.Add({ TEXT("NS_MuzzleSmoke"), 200.f, { Muzzle } });

		FLayer Shell;
		Shell.Name = TEXT("Shell");
		Shell.Count = FIntPoint(1, 1);
		Shell.Lifetime = FVector2f(0.7f, 0.7f);
		Shell.ColorA = FLinearColor(0.6f, 0.43f, 0.12f, 1.f);
		Shell.ColorB = FLinearColor(0.75f, 0.55f, 0.18f, 1.f);
		Shell.bStreak = true;
		Shell.StreakMin = FVector2f(1.f, 2.6f);
		Shell.StreakMax = FVector2f(1.1f, 2.9f);
		Shell.Speed = FVector2f(150.f, 220.f);
		Shell.ConeAngle = 20.f;
		Shell.Gravity = -980.f;
		Shell.Drag = 0.2f;
		List.Add({ TEXT("NS_ShellEject"), 200.f, { Shell } });

		// Explosion: +X points up.
		FLayer Fire;
		Fire.Name = TEXT("Fireball");
		Fire.bAdditive = true;
		Fire.Count = FIntPoint(10, 14);
		Fire.Lifetime = FVector2f(0.15f, 0.3f);
		Fire.ColorA = FLinearColor(10.f, 4.f, 1.f, 1.f);
		Fire.ColorB = FLinearColor(14.f, 6.5f, 2.f, 1.f);
		Fire.Size = FVector2f(120.f, 220.f);
		Fire.Speed = FVector2f(150.f, 500.f);
		Fire.ConeAngle = 180.f;
		Fire.Gravity = 0.f;
		Fire.Drag = 4.f;
		Fire.Growth = FVector2f(1.f, 2.2f);
		Fire.SpawnRadius = 40.f;
		FLayer Smoke = Dust(TEXT("Smoke"), FLinearColor(0.07f, 0.07f, 0.07f, 0.92f), FLinearColor(0.12f, 0.12f, 0.12f, 0.97f), FVector2f(150.f, 260.f), FIntPoint(12, 16), FVector2f(2.4f, 3.4f), FVector2f(1.f, 2.6f));
		Smoke.Speed = FVector2f(120.f, 380.f);
		Smoke.ConeAngle = 90.f;
		Smoke.Gravity = 50.f;
		Smoke.Drag = 1.5f;
		Smoke.SpawnRadius = 60.f;
		FLayer Chunks = Debris(TEXT("Debris"), FLinearColor(0.04f, 0.035f, 0.03f, 1.f), FLinearColor(0.1f, 0.085f, 0.07f, 1.f), FVector2f(5.f, 9.f), FIntPoint(18, 26), FVector2f(600.f, 1300.f));
		Chunks.ConeAngle = 70.f;
		Chunks.Lifetime = FVector2f(0.8f, 1.4f);
		FLayer Embers = Sparks(TEXT("Sparks"), FIntPoint(24, 34), FVector2f(0.3f, 0.7f), FVector2f(800.f, 1800.f), Spark, SparkHot);
		Embers.ConeAngle = 80.f;
		List.Add({ TEXT("NS_Explosion"), 1500.f, { Fire, Smoke, Chunks, Embers } });

		FLayer White = Sparks(TEXT("Sparks"), FIntPoint(20, 28), FVector2f(0.2f, 0.5f), FVector2f(600.f, 1400.f), FLinearColor(14.f, 14.f, 12.f, 1.f), FLinearColor(18.f, 18.f, 16.f, 1.f));
		White.ConeAngle = 90.f;
		FLayer Haze = Dust(TEXT("Smoke"), FLinearColor(0.82f, 0.82f, 0.82f, 0.6f), FLinearColor(0.9f, 0.9f, 0.9f, 0.7f), FVector2f(70.f, 110.f), FIntPoint(6, 8), FVector2f(1.3f, 2.f), FVector2f(1.f, 2.4f));
		Haze.ConeAngle = 90.f;
		Haze.Gravity = 20.f;
		List.Add({ TEXT("NS_FlashbangBurst"), 1000.f, { White, Haze } });
		return List;
	}


	// --- Dump (for finding property names) ---------------------------------

	void DumpObject(const UObject* Object, const TCHAR* Indent)
	{
		UE_LOG(LogCS, Display, TEXT("%s%s (%s)"), Indent, *Object->GetName(), *Object->GetClass()->GetName());
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (It->GetOwnerClass() == UObject::StaticClass())
			{
				continue;
			}
			UE_LOG(LogCS, Display, TEXT("%s  %s = %s"), Indent, *It->GetName(), *Export(*It, Object, Object).Left(600));
		}
	}

	void Dump(const TCHAR* Path)
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, Path);
		if (!System)
		{
			UE_LOG(LogCS, Error, TEXT("FX dump: cannot load %s"), Path);
			return;
		}
		UE_LOG(LogCS, Display, TEXT("FX dump: %s, %d emitter(s)"), Path, System->GetEmitterHandles().Num());
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			UObject* Stateless = StatelessOf(Handle);
			UE_LOG(LogCS, Display, TEXT(" emitter %s, stateless %s"), *Handle.GetName().ToString(), Stateless ? TEXT("yes") : TEXT("no"));
			if (!Stateless)
			{
				continue;
			}
			DumpObject(Stateless, TEXT("  "));
			for (const TCHAR* ListName : { TEXT("Modules"), TEXT("RendererProperties") })
			{
				for (const UObject* Item : ObjectList(Stateless, ListName))
				{
					if (Item)
					{
						DumpObject(Item, TEXT("    "));
					}
				}
			}
		}
	}
}
#endif

int32 UCSFXBuilderCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	FString Path;
	if (FParse::Param(*Params, TEXT("dump")))
	{
		CSFXBuilder::Dump(FParse::Value(*Params, TEXT("path="), Path) ? *Path : CSFXBuilder::TemplatePath);
		return 0;
	}
	for (const CSFXBuilder::FEffect& Effect : CSFXBuilder::Effects())
	{
		CSFXBuilder::Build(Effect);
	}
	UE_LOG(LogCS, Display, TEXT("FX build: %s (%d problem(s))"), CSFXBuilder::Failures == 0 ? TEXT("done") : TEXT("FAILED"), CSFXBuilder::Failures);
	return CSFXBuilder::Failures == 0 ? 0 : 1;
#else
	return 1;
#endif
}
