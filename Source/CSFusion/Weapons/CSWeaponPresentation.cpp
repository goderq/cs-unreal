// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Weapons/CSWeaponPresentation.h"

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
	const FCSWeaponModel* Model = Get()->Models.Find(WeaponDefinition->GetFName());
	return (Model && !Model->Mesh.IsNull()) ? Model : nullptr;
}
