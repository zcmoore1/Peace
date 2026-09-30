// Original housing; lighting gives the frame readable depth.
peace/optic_housing
{
	cull none
	{
		map gfx/peace/optic_housing.tga
		rgbGen lightingDiffuse
	}
}
peace/optic_dot
{
	cull none
	{
		map gfx/peace/optic_dot.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
