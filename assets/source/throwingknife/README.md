# Peace throwing-knife artwork

The sources are `dev/tools/build_throwingknife.py` and `animation.json`. They
author the blade, grip, open lanyard eye, gloved hand, sleeve, animated grip tag,
shader materials and silhouette icon. No third-party mesh, texture or animation was imported. The generator
and its generated assets use the repository's GPL-2.0-or-later terms.

```sh
python3 dev/tools/build_throwingknife.py
python3 dev/tools/build_throwingknife.py --check
python3 dev/tests/throwingknife_asset_test.py
```

The world model points along +X with the blade tip at x=6. These are stylized
game coordinates. Its origin and orientation fit the existing projectile and
sticking code; adding the artwork does not change collision or damage.
`knife_hand.md3` has a `tag_weapon` on every frame that carries this same mesh
through the first-person motion. The fingers stay wrapped around the grip and
the sleeve follows the wrist. The single-frame blade is also regular melee's
world/held model and icon; it is not duplicated or reoriented on disk.

`animation.json` contains original wrist position/rotation keys, sample counts
and clip mappings. The generator bakes 58 MD3 hand frames and writes `throw.cfg`
and `melee.cfg` in the existing animation-table format. Pullout rises from below
the view, rolls into the grip, and settles exactly into idle. Melee sweeps right
to left with a horizontal blade at contact, follows through, then withdraws
below the view for the gun's return. Its two clips sample one continuous curve;
the last pre-hit and first post-hit poses are identical, including the tag.

`CG_WeapAnim_ActionFrame` reads these tables and fits the picture to existing
predicted timers: 150 ms pullout, melee contact at 79 ms and completion at
800 ms. Frame numbers are data, not C constants. No animation emits a gameplay
event, changes ammunition, or adds a lock. A thrown blade disappears from the
hand at release; a melee blade stays attached through the follow-through.
Semtex and throwing follow-through retain their existing fallback gestures.

Runtime files are under `assets/baseq3/models/weapons2/throwingknife`,
`scripts/peace_throwingknife.shader` and `icons/peace_throwingknife.tga`.
They require no retail textures or hand model. The normal `peace_assets`
build target copies them with the rest of the runtime content.
