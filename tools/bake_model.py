"""
Bake a 3D model (FBX/OBJ/GLTF) into a PS1-ready C mesh for PSn00bSDK.

Run with Blender (no GUI):
    blender --background --python tools/bake_model.py -- <input> <out.c> <out.h> <name> [target_tris] [target_height]

Example:
    blender --background --python tools/bake_model.py -- assets/hero.fbx src/model_player.c src/model_player.h model_player 250 300

Coordinate mapping: the game world uses -Y as "up", +Z as "forward", and the
model is placed with its feet at y=0. Blender is Z-up, so we map
    world_x =  blender_x
    world_y = -(blender_z - z_min)   (so feet sit on the ground)
    world_z =  blender_y
and scale the whole model so its height becomes `target_height` units.
"""

import bpy
import sys
import os

# ---- parse args after "--" ----
argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
if len(argv) < 4:
    print("usage: ... -- <input> <out.c> <out.h> <name> [target_tris] [target_height]")
    sys.exit(1)

in_path   = argv[0]
out_c     = argv[1]
out_h     = argv[2]
name      = argv[3]
TARGET_TRIS   = int(argv[4]) if len(argv) > 4 else 250
TARGET_HEIGHT = float(argv[5]) if len(argv) > 5 else 300.0

# ---- clean scene ----
bpy.ops.wm.read_factory_settings(use_empty=True)

ext = os.path.splitext(in_path)[1].lower()
if ext == ".fbx":
    bpy.ops.import_scene.fbx(filepath=in_path)
elif ext == ".obj":
    bpy.ops.wm.obj_import(filepath=in_path)
elif ext in (".gltf", ".glb"):
    bpy.ops.import_scene.gltf(filepath=in_path)
else:
    print("unsupported format:", ext)
    sys.exit(1)

# ---- collect mesh objects, apply transforms, join ----
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    print("no mesh found in", in_path)
    sys.exit(1)

bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active

# apply object transform so vertex coords are final
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# ---- triangulate + recalc normals + decimate ----
mesh = obj.data
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode="OBJECT")

tri_count = len(mesh.polygons)
if tri_count > TARGET_TRIS:
    mod = obj.modifiers.new("dec", "DECIMATE")
    mod.ratio = TARGET_TRIS / tri_count
    bpy.ops.object.modifier_apply(modifier=mod.name)
    # re-triangulate after decimation
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh = obj.data

# ---- compute bounds / scale ----
xs = [v.co.x for v in mesh.vertices]
ys = [v.co.y for v in mesh.vertices]
zs = [v.co.z for v in mesh.vertices]
zmin, zmax = min(zs), max(zs)
xmid = (min(xs) + max(xs)) / 2.0
ymid = (min(ys) + max(ys)) / 2.0
height = max(zmax - zmin, 1e-6)
scale = TARGET_HEIGHT / height


def w(v):
    """Blender vert -> world int coords (centered XZ, feet at y=0)."""
    wx = int(round((v.co.x - xmid) * scale))
    wy = int(round(-(v.co.z - zmin) * scale))
    wz = int(round((v.co.y - ymid) * scale))
    return wx, wy, wz


def mat_color(poly):
    """Pick an RGB color for a face from its material, else shade by normal."""
    mats = obj.data.materials
    if mats and poly.material_index < len(mats) and mats[poly.material_index]:
        m = mats[poly.material_index]
        col = None
        if m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == "BSDF_PRINCIPLED":
                    col = n.inputs["Base Color"].default_value
                    break
        if col is None:
            col = m.diffuse_color
        return (int(col[0] * 255), int(col[1] * 255), int(col[2] * 255))
    # fallback: gray shaded by how "up" the face points
    up = max(0.0, min(1.0, (poly.normal.z + 1.0) * 0.5))
    g = int(90 + up * 130)
    return (g, g, g)


verts = [w(v) for v in mesh.vertices]
faces = []
colors = []
for poly in mesh.polygons:
    if len(poly.vertices) != 3:
        continue
    faces.append(tuple(poly.vertices))
    colors.append(mat_color(poly))

# ---- emit C ----
guard = name.upper() + "_H"
with open(out_h, "w") as f:
    f.write("#ifndef %s\n#define %s\n\n" % (guard, guard))
    f.write('#include "engine3d.h"\n\n')
    f.write("extern const Mesh %s;\n\n#endif\n" % name)

with open(out_c, "w") as f:
    f.write('#include "%s"\n\n' % os.path.basename(out_h))
    f.write("static const SVECTOR _verts[] = {\n")
    for (x, y, z) in verts:
        f.write("    { %d, %d, %d, 0 },\n" % (x, y, z))
    f.write("};\n\n")
    f.write("static const unsigned short _faces[] = {\n")
    for (a, b, c) in faces:
        f.write("    %d, %d, %d,\n" % (a, b, c))
    f.write("};\n\n")
    f.write("static const unsigned char _colors[] = {\n")
    for (r, g, b) in colors:
        f.write("    %d, %d, %d,\n" % (r, g, b))
    f.write("};\n\n")
    f.write("const Mesh %s = {\n" % name)
    f.write("    _verts, %d,\n" % len(verts))
    f.write("    _faces, %d,\n" % len(faces))
    f.write("    _colors\n};\n")

print("BAKED %s: %d verts, %d tris (scale %.4f, height %.1f)"
      % (name, len(verts), len(faces), scale, TARGET_HEIGHT))
