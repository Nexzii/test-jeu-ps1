#ifndef ENGINE3D_H
#define ENGINE3D_H

#include <stdint.h>
#include <psxgte.h>

#define SCREEN_W  320
#define SCREEN_H  240
#define CENTER_X  (SCREEN_W >> 1)
#define CENTER_Y  (SCREEN_H >> 1)

#define OT_LEN    2048

/* Initialize the GTE (call once after ResetGraph). */
void eng3d_init(void);

/* Build the view matrix for this frame from a camera eye/target.
 * Must be called before any eng3d_draw_* call in the frame. */
void eng3d_set_camera(VECTOR *eye, VECTOR *target);

/* Draw an axis-aligned box centered at world position (cx,cy,cz) with the
 * given half-extents. Each face is flat shaded; the top face is brightest,
 * sides darker, to give the shape readable volume without GTE lighting.
 * Primitives are sorted into `ot` and allocated from `*pri`. */
void eng3d_draw_box(int32_t cx, int32_t cy, int32_t cz,
                    int32_t hx, int32_t hy, int32_t hz,
                    uint8_t r, uint8_t g, uint8_t b,
                    uint32_t *ot, char **pri);

/* Draw a flat horizontal floor quad centered at (cx, 0, cz). */
void eng3d_draw_floor(int32_t cx, int32_t cz, int32_t half,
                      uint8_t r, uint8_t g, uint8_t b,
                      uint32_t *ot, char **pri);

/* ---- Textures ---- */

/* Upload a 16-bit (BGR555) texture to VRAM at (vx,vy) and return its tpage. */
uint16_t eng3d_load_texture(const uint16_t *data, int w, int h, int vx, int vy);

/* `fog` 0..255 dims distant geometry toward black (atmospheric depth). */
void eng3d_set_fog(int fog);

/* Textured floor quad (full 128x128 texture mapped). */
void eng3d_draw_floor_tex(int32_t cx, int32_t cz, int32_t half,
                          uint16_t tpage, uint8_t shade,
                          uint32_t *ot, char **pri);

/* Textured floor quad with independent corner heights (for rolling terrain).
 * Corner order: NW(-,-), NE(+,-), SW(-,+), SE(+,+). */
void eng3d_draw_floor_tex_h(int32_t cx, int32_t cz, int32_t half,
                            int16_t ynw, int16_t yne, int16_t ysw, int16_t yse,
                            uint16_t tpage, uint8_t shade,
                            uint32_t *ot, char **pri);

/* Textured axis-aligned box (each face mapped with the full texture). */
void eng3d_draw_box_tex(int32_t cx, int32_t cy, int32_t cz,
                        int32_t hx, int32_t hy, int32_t hz,
                        uint16_t tpage, uint32_t *ot, char **pri);

/* ---- Arbitrary triangle mesh (baked from a 3D model) ---- */

typedef struct {
    const SVECTOR  *verts;       /* model-space vertices                       */
    int             vert_count;
    const uint16_t *faces;       /* 3 vertex indices per triangle              */
    int             tri_count;
    const uint8_t  *colors;      /* 3 bytes (r,g,b) per triangle, flat shaded  */
} Mesh;

/* Draw a baked mesh at world (cx,cy,cz), rotated `yaw` around the vertical
 * axis (RotMatrix units: 4096 = 360 deg). Backface culled and depth sorted. */
void eng3d_draw_mesh(const Mesh *m, int32_t cx, int32_t cy, int32_t cz,
                     int yaw, uint32_t *ot, char **pri);

/* Draw a static world mesh (no model transform — verts are already in world
 * space). Uses the current view matrix set by eng3d_set_camera. */
void eng3d_draw_world(const Mesh *m, uint32_t *ot, char **pri);

/* ---- Textured triangle mesh (UV-mapped) ---- */

typedef struct {
    const SVECTOR  *verts;       /* model-space vertices                  */
    int             vert_count;
    const uint16_t *faces;       /* 3 vertex indices per triangle          */
    int             tri_count;
    const uint8_t  *uvs;         /* 2 bytes (u,v) per vertex (0-255)       */
} TexMesh;

/* Draw a textured mesh at world (cx,cy,cz), yaw rotation (4096 = 360 deg),
 * using texture page `tpage`. Backface culled and depth sorted. */
void eng3d_draw_texmesh(const TexMesh *m, int32_t cx, int32_t cy, int32_t cz,
                        int yaw, uint16_t tpage, uint32_t *ot, char **pri);

#endif
