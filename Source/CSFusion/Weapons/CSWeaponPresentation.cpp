// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Weapons/CSWeaponPresentation.h"

#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

FTransform FCSWeaponModel::GetMeshInHand() const
{
	// Model frame (barrel +X) -> hand frame (barrel +Y): yaw 90. GripRotation
	// is applied in the model frame first. The grip point lands on the origin.
	const FQuat Rotation = FQuat(FRotator(0.f, 90.f, 0.f)) * FQuat(GripRotation);
	const FVector Offset = -Rotation.RotateVector(Grip * Scale);
	return FTransform(Rotation, Offset, FVector(Scale));
}

const FCSWeaponModel* UCSWeaponPresentationSettings::Find(const UObject* WeaponDefinition)
{
	if (!WeaponDefinition)
	{
		return nullptr;
	}
	const UCSWeaponPresentationSettings* Settings = Get();
	const FName Key = WeaponDefinition->GetFName();
	static const bool bPreview = FParse::Param(FCommandLine::Get(), TEXT("replacementmodels"));
	if (Settings->bUseReplacementModels || bPreview)
	{
		const FCSWeaponModel* Replacement = Settings->ReplacementModels.Find(Key);
		if (Replacement && HasMesh(*Replacement))
		{
			return Replacement;
		}
	}
	const FCSWeaponModel* Model = Settings->Models.Find(Key);
	return (Model && !Model->Mesh.IsNull()) ? Model : nullptr;
}

bool UCSWeaponPresentationSettings::HasMesh(const FCSWeaponModel& Model)
{
	if (Model.Mesh.IsNull())
	{
		return false;
	}
	// Whether the (git-ignored) package is present in this copy; asked once per mesh.
	static TMap<FSoftObjectPath, bool> Known;
	const FSoftObjectPath Path = Model.Mesh.ToSoftObjectPath();
	if (const bool* Cached = Known.Find(Path))
	{
		return *Cached;
	}
	const bool bExists = FPackageName::DoesPackageExist(Path.GetLongPackageName());
	Known.Add(Path, bExists);
	return bExists;
}
