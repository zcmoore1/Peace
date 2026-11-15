// Original basic M16 + Masterkey materials.
peace/m16_metal
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
}
peace/m16_polymer
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
}
peace/m16_fabric
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
}
peace/m16_rubber
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
}
peace/m16_steel
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
 { map $whiteimage
 blendFunc GL_ONE GL_ONE
 rgbGen const ( .045 .05 .055 )
 }
}
peace/m16_shell
{
 { map models/weapons2/m16/materials.tga
 rgbGen lightingDiffuse
 }
 { map $whiteimage
 blendFunc GL_DST_COLOR GL_ZERO
 rgbGen const ( .75 .15 .10 )
 }
}
peace/m16_flash
{
 cull none
 { map models/weapons2/m16/flash.tga
 blendFunc add
 rgbGen identity
 }
}
icons/peace_m16
{
 nopicmip
 { map icons/peace_m16.tga
 blendFunc blend
 }
}
