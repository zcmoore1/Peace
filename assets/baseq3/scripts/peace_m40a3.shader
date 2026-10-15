// Original generated texture and authored game geometry.
peace/m40_stock
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
}
peace/m40_metal
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
}
peace/m40_fabric
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
}
peace/m40_rubber
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
}
peace/m40_steel
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
 { map $whiteimage
 blendFunc GL_ONE GL_ONE
 rgbGen const ( .14 .16 .17 )
 }
}
peace/m40_brass
{
 { map models/weapons2/m40a3/materials.jpg
 rgbGen lightingDiffuse
 }
 { map $whiteimage
 blendFunc GL_ONE GL_ONE
 rgbGen const ( .45 .27 .08 )
 }
}
peace/m40_glass
{
 { map $whiteimage
 rgbGen const ( .025 .055 .065 )
 }
 { map models/weapons2/m40a3/materials.jpg
 tcGen environment
 blendFunc GL_ONE GL_ONE
 rgbGen const ( .12 .2 .22 )
 }
}
peace/m40_flash
{
 cull none
 { map models/weapons2/m40a3/flash.tga
 blendFunc add
 rgbGen identity
 }
}
peace/m40_scope
{
 nopicmip
 nomipmaps
 { map models/weapons2/m40a3/scope.tga
 blendFunc blend
 rgbGen vertex
 alphaGen vertex
 }
}
icons/peace_m40a3
{
 nopicmip
 { map icons/peace_m40a3.tga
 blendFunc blend
 }
}
