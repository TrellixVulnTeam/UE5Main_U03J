// Copyright Epic Games, Inc. All Rights Reserved.

using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Visual;
using System.Collections.Generic;

namespace DatasmithRevitExporter
{
	public class FDecalMaterial
	{
		public string MaterialName { get; private set; }
		public string DiffuseTexturePath { get; private set; }
		public string BumpTexturePath { get; private set; }

		private void SetMaterialProperties(Asset InMaterialAsset)
		{
			const string DiffuseMapPropName = "decApp_diffuse";
			const string BumpMapPropName = "decApp_bump_map";
			const string TexturePathName = "unifiedbitmap_Bitmap";

			string GetConnectedImagePath(AssetProperty InProp)
			{
				IList<AssetProperty> ConnectedProps = InProp.GetAllConnectedProperties();

				foreach (AssetProperty InnerProp in ConnectedProps)
				{
					if (InnerProp.Type == AssetPropertyType.Asset)
					{
						Asset InnerPropAsset = InnerProp as Asset;

						for (int Index = 0; Index < InnerPropAsset.Size; ++Index)
						{
#if REVIT_API_2018
							AssetProperty AssetProp = InnerPropAsset[Index];
#else
							AssetProperty AssetProp = InnerPropAsset.Get(Index);
#endif

							if (AssetProp.Name == TexturePathName)
							{
								return (AssetProp as AssetPropertyString).Value;
							}
						}
					}
				}

				return null;
			}

			bool bDiffusePropertyProcessed = false;
			bool bBumpPropertyProcessed = false;

			for (int PropIndex = 0; PropIndex < InMaterialAsset.Size; ++PropIndex)
			{
#if REVIT_API_2018
				AssetProperty Prop = InMaterialAsset[PropIndex];
#else
				AssetProperty Prop = InMaterialAsset.Get(PropIndex);
#endif
				if (Prop.Name == DiffuseMapPropName)
				{
					DiffuseTexturePath = GetConnectedImagePath(Prop);
					bDiffusePropertyProcessed = true;
				}
				else if (Prop.Name == BumpMapPropName)
				{
					BumpTexturePath = GetConnectedImagePath(Prop);
					bBumpPropertyProcessed = true;
				}

				if (bDiffusePropertyProcessed && bBumpPropertyProcessed)
				{
					break;
				}
			}
		}

		public static ElementId GetDecalElementId(MaterialNode InMaterialNode)
		{
			const string PropertyName_DecalElemId = "decalelementId";

			if (!InMaterialNode.HasOverriddenAppearance)
			{
				return ElementId.InvalidElementId;
			}

			Asset Decal = InMaterialNode.GetAppearanceOverride();

			if (Decal == null || Decal.Name != "Decal")
			{
				return ElementId.InvalidElementId;
			}

			for (int PropIndex = 0; PropIndex < Decal.Size; ++PropIndex)
			{
#if REVIT_API_2018
				AssetProperty Prop = Decal[PropIndex];
#else
				AssetProperty Prop = Decal.Get(PropIndex);
#endif
				if (Prop.Name == PropertyName_DecalElemId && Prop.Type == AssetPropertyType.Integer)
				{
					int Id = (Prop as AssetPropertyInteger).Value;
					return new ElementId(Id);
				}
			}
			return ElementId.InvalidElementId;
		}

		public static FDecalMaterial Create(MaterialNode InMaterialNode, Material InMaterial)
		{
			if (!InMaterialNode.HasOverriddenAppearance)
			{
				return null;
			}

			Asset Decal = InMaterialNode.GetAppearanceOverride();

			if (Decal == null || Decal.Name != "Decal")
			{
				return null;
			}

			const string AssetName_Material = "DecalAppearance";

			FDecalMaterial DecalMaterial = new FDecalMaterial();

			DecalMaterial.MaterialName = FDatasmithFacadeElement.GetStringHash($"Decal_{FMaterialData.GetMaterialName(InMaterialNode, InMaterial)}");

			// Look for DecalAppearance asset, which defines the material parameters

			bool bMaterialInitialized = false;

			for (int PropIndex = 0; PropIndex < Decal.Size; ++PropIndex)
			{
#if REVIT_API_2018
				AssetProperty Prop = Decal[PropIndex];
#else
				AssetProperty Prop = Decal.Get(PropIndex);
#endif
				if (Prop.Type == AssetPropertyType.Reference)
				{
					AssetPropertyReference RefProp = (Prop as AssetPropertyReference);
					foreach (AssetProperty InnerProp in RefProp.GetAllConnectedProperties())
					{
						if (InnerProp.Type == AssetPropertyType.Asset)
						{
							Asset InnerPropAsset = InnerProp as Asset;

							if (InnerPropAsset.Name == AssetName_Material)
							{
								DecalMaterial.SetMaterialProperties(InnerPropAsset);
								bMaterialInitialized = true;
								break;
							}
						}
					}
				}

				if (bMaterialInitialized)
				{
					break;
				}
			}

			return DecalMaterial;
		}
	}
}
