# Peace throwing-knife artwork

The source is `dev/tools/build_throwingknife.py`. It authors the blade, grip,
open lanyard eye, gloved hand, sleeve, grip tag, shader materials and silhouette
icon. No third-party mesh, texture or animation was imported. The generator
and its generated assets use the repository's GPL-2.0-or-later terms.

```sh
python3 dev/tools/build_throwingknife.py
python3 dev/tools/build_throwingknife.py --check
python3 dev/tests/throwingknife_asset_test.py
```

The world model points along +X with the blade tip at x=6. These are stylized
game coordinates. Its origin and orientation fit the existing projectile and
sticking code; adding the artwork does not change collision or damage.
`knife_hand.md3` has a `tag_weapon` that turns this same mesh upward for the
first-person grip. The sleeve and fingers are authored around that grip.

Runtime files are under `assets/baseq3/models/weapons2/throwingknife`,
`scripts/peace_throwingknife.shader` and `icons/peace_throwingknife.tga`.
They require no retail textures or hand model. The normal `peace_assets`
build target copies them with the rest of the runtime content.
