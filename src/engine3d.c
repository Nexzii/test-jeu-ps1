#include "engine3d.h"
#include <psxgpu.h>
#include <psxgte.h>
#include <inline_c.h>

/* ---- Vertex index table for a box (same winding as the SDK gte demo) ---- */

typedef struct { short v0, v1, v2, v3; } INDEX;

static const INDEX box_faces[6] = {
    { 0, 1, 2, 3 },   /* -Z */
    { 4, 5, 6, 7 },   /* +Z */
    { 5, 4, 0, 1 },   /* top    (-Y, brightest) */
    { 6, 7, 3, 2 },   /* bottom (+Y, darkest)   */
    { 0, 2, 5, 7 },   /* -X */
    { 3, 1, 6, 4 }    /* +X */
};

/* Per-face brightness (0..256 fixed): top brightest, bottom darkest. */
static const int face_shade[6] = { 180, 180, 256, 110, 150, 210 };

/* Current frame view matrix (world -> camera). */
static MATRIX view;

/* Distance fog: darkens geometry toward black past FOG_NEAR. */
static int s_fog = 0;
#define FOG_NEAR 130
#define FOG_FAR  520
void eng3d_set_fog(int fog) { s_fog = fog; }
static int fog_mul(int p) {              /* returns 0..256 brightness scale */
    if (s_fog <= 0 || p <= FOG_NEAR) return 256;
    int t = (p - FOG_NEAR) * 256 / (FOG_FAR - FOG_NEAR);
    if (t > 256) t = 256;
    return 256 - ((t * s_fog) >> 8);
}

/* Reusable identity-rotation model matrix. */
static MATRIX model;

void eng3d_init(void) {
    InitGeom();
    gte_SetGeomOffset(CENTER_X, CENTER_Y);
    gte_SetGeomScreen(CENTER_X);
}

/* ---- LookAt (from the SDK fpscam example) ---- */

static void cross_product(SVECTOR *v0, SVECTOR *v1, VECTOR *out) {
    out->vx = ((v0->vy * v1->vz) - (v0->vz * v1->vy)) >> 12;
    out->vy = ((v0->vz * v1->vx) - (v0->vx * v1->vz)) >> 12;
    out->vz = ((v0->vx * v1->vy) - (v0->vy * v1->vx)) >> 12;
}

void eng3d_set_camera(VECTOR *eye, VECTOR *at) {
    SVECTOR up = { 0, -ONE, 0 };   /* -Y is "up" in our world */
    VECTOR taxis, pos, vec;
    SVECTOR zaxis, xaxis, yaxis;

    setVector(&taxis, at->vx - eye->vx, at->vy - eye->vy, at->vz - eye->vz);
    VectorNormalS(&taxis, &zaxis);
    cross_product(&zaxis, &up, &taxis);
    VectorNormalS(&taxis, &xaxis);
    cross_product(&zaxis, &xaxis, &taxis);
    VectorNormalS(&taxis, &yaxis);

    view.m[0][0] = xaxis.vx; view.m[1][0] = yaxis.vx; view.m[2][0] = zaxis.vx;
    view.m[0][1] = xaxis.vy; view.m[1][1] = yaxis.vy; view.m[2][1] = zaxis.vy;
    view.m[0][2] = xaxis.vz; view.m[1][2] = yaxis.vz; view.m[2][2] = zaxis.vz;

    pos.vx = -eye->vx;
    pos.vy = -eye->vy;
    pos.vz = -eye->vz;

    ApplyMatrixLV(&view, &pos, &vec);
    TransMatrix(&view, &vec);

    /* model rotation is always identity; only its translation changes. */
    model.m[0][0] = ONE; model.m[0][1] = 0;   model.m[0][2] = 0;
    model.m[1][0] = 0;   model.m[1][1] = ONE; model.m[1][2] = 0;
    model.m[2][0] = 0;   model.m[2][1] = 0;   model.m[2][2] = ONE;
}

static inline uint8_t shade(uint8_t c, int s) {
    int v = (c * s) >> 8;
    return (v > 255) ? 255 : (uint8_t)v;
}

void eng3d_draw_box(int32_t cx, int32_t cy, int32_t cz,
                    int32_t hx, int32_t hy, int32_t hz,
                    uint8_t r, uint8_t g, uint8_t b,
                    uint32_t *ot, char **pri) {
    SVECTOR v[8];
    MATRIX  mvp;
    POLY_F4 *pol;
    int i, p;

    v[0].vx = -hx; v[0].vy = -hy; v[0].vz = -hz;
    v[1].vx =  hx; v[1].vy = -hy; v[1].vz = -hz;
    v[2].vx = -hx; v[2].vy =  hy; v[2].vz = -hz;
    v[3].vx =  hx; v[3].vy =  hy; v[3].vz = -hz;
    v[4].vx =  hx; v[4].vy = -hy; v[4].vz =  hz;
    v[5].vx = -hx; v[5].vy = -hy; v[5].vz =  hz;
    v[6].vx =  hx; v[6].vy =  hy; v[6].vz =  hz;
    v[7].vx = -hx; v[7].vy =  hy; v[7].vz =  hz;

    model.t[0] = cx; model.t[1] = cy; model.t[2] = cz;
    CompMatrixLV(&view, &model, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_F4 *)(*pri);

    for (i = 0; i < 6; i++) {
        gte_ldv3(&v[box_faces[i].v0], &v[box_faces[i].v1], &v[box_faces[i].v2]);
        gte_rtpt();
        gte_nclip();
        gte_stopz(&p);
        if (p < 0) continue;             /* backface */

        gte_avsz4();
        gte_stotz(&p);
        p >>= 2;
        if (p <= 0 || p >= OT_LEN) continue;

        setPolyF4(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        gte_ldv0(&v[box_faces[i].v3]);
        gte_rtps();
        gte_stsxy(&pol->x3);

        {
            int fm = fog_mul(p);
            setRGB0(pol,
                    (shade(r, face_shade[i]) * fm) >> 8,
                    (shade(g, face_shade[i]) * fm) >> 8,
                    (shade(b, face_shade[i]) * fm) >> 8);
        }

        addPrim(ot + p, pol);
        pol++;
    }

    *pri = (char *)pol;
}

void eng3d_draw_mesh(const Mesh *m, int32_t cx, int32_t cy, int32_t cz,
                     int yaw, uint32_t *ot, char **pri) {
    MATRIX  rot, mvp;
    SVECTOR rv = { 0, (short)yaw, 0 };
    POLY_F3 *pol;
    int i, p;

    /* model matrix = yaw rotation about Y + translation, composed with view */
    RotMatrix(&rv, &rot);
    rot.t[0] = cx; rot.t[1] = cy; rot.t[2] = cz;
    CompMatrixLV(&view, &rot, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_F3 *)(*pri);

    for (i = 0; i < m->tri_count; i++) {
        const uint16_t *f = &m->faces[i * 3];
        gte_ldv3(&m->verts[f[0]], &m->verts[f[1]], &m->verts[f[2]]);
        gte_rtpt();
        gte_nclip();
        gte_stopz(&p);
        if (p < 0) continue;             /* backface */

        gte_avsz3();
        gte_stotz(&p);
        p >>= 2;
        if (p <= 0 || p >= OT_LEN) continue;

        setPolyF3(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);

        const uint8_t *c = &m->colors[i * 3];
        setRGB0(pol, c[0], c[1], c[2]);

        addPrim(ot + p, pol);
        pol++;
    }

    *pri = (char *)pol;
}

void eng3d_draw_world(const Mesh *m, uint32_t *ot, char **pri) {
    POLY_F3 *pol;
    int i, p;

    /* The view matrix is already current from eng3d_set_camera. */
    gte_SetRotMatrix(&view);
    gte_SetTransMatrix(&view);

    pol = (POLY_F3 *)(*pri);

    for (i = 0; i < m->tri_count; i++) {
        const uint16_t *f = &m->faces[i * 3];
        gte_ldv3(&m->verts[f[0]], &m->verts[f[1]], &m->verts[f[2]]);
        gte_rtpt();
        gte_nclip();
        gte_stopz(&p);
        if (p < 0) continue;

        gte_avsz3();
        gte_stotz(&p);
        p >>= 2;
        if (p <= 0 || p >= OT_LEN) continue;

        setPolyF3(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);

        const uint8_t *c = &m->colors[i * 3];
        setRGB0(pol, c[0], c[1], c[2]);

        addPrim(ot + p, pol);
        pol++;
    }

    *pri = (char *)pol;
}

void eng3d_draw_texmesh(const TexMesh *m, int32_t cx, int32_t cy, int32_t cz,
                        int yaw, uint16_t tpage, uint32_t *ot, char **pri) {
    MATRIX   rot, mvp;
    SVECTOR  rv = { 0, (short)yaw, 0 };
    POLY_FT3 *pol;
    int i, p;

    RotMatrix(&rv, &rot);
    rot.t[0] = cx; rot.t[1] = cy; rot.t[2] = cz;
    CompMatrixLV(&view, &rot, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_FT3 *)(*pri);
    for (i = 0; i < m->tri_count; i++) {
        const uint16_t *f = &m->faces[i * 3];
        gte_ldv3(&m->verts[f[0]], &m->verts[f[1]], &m->verts[f[2]]);
        gte_rtpt();
        gte_nclip();
        gte_stopz(&p);
        if (p < 0) continue;
        gte_avsz3();
        gte_stotz(&p);
        p >>= 2;
        if (p <= 0 || p >= OT_LEN) continue;

        setPolyFT3(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        pol->u0 = m->uvs[f[0]*2]; pol->v0 = m->uvs[f[0]*2+1];
        pol->u1 = m->uvs[f[1]*2]; pol->v1 = m->uvs[f[1]*2+1];
        pol->u2 = m->uvs[f[2]*2]; pol->v2 = m->uvs[f[2]*2+1];
        pol->tpage = tpage;
        pol->clut = 0;
        int fm = fog_mul(p) >> 1;        /* ~128 neutral modulation */
        setRGB0(pol, fm, fm, fm);
        addPrim(ot + p, pol);
        pol++;
    }
    *pri = (char *)pol;
}

void eng3d_draw_floor(int32_t cx, int32_t cz, int32_t half,
                      uint8_t r, uint8_t g, uint8_t b,
                      uint32_t *ot, char **pri) {
    SVECTOR v[4];
    MATRIX  mvp;
    POLY_F4 *pol;
    int p;

    v[0].vx = -half; v[0].vy = 0; v[0].vz = -half;
    v[1].vx =  half; v[1].vy = 0; v[1].vz = -half;
    v[2].vx = -half; v[2].vy = 0; v[2].vz =  half;
    v[3].vx =  half; v[3].vy = 0; v[3].vz =  half;

    model.t[0] = cx; model.t[1] = 0; model.t[2] = cz;
    CompMatrixLV(&view, &model, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_F4 *)(*pri);

    gte_ldv3(&v[0], &v[1], &v[2]);
    gte_rtpt();
    gte_avsz4();
    gte_stotz(&p);
    p >>= 2;
    if (p > 0 && p < OT_LEN) {
        setPolyF4(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        gte_ldv0(&v[3]);
        gte_rtps();
        gte_stsxy(&pol->x3);
        int fm = fog_mul(p);
        setRGB0(pol, (r*fm)>>8, (g*fm)>>8, (b*fm)>>8);
        addPrim(ot + p, pol);
        pol++;
    }

    *pri = (char *)pol;
}

/* ---- Textured rendering ---- */

uint16_t eng3d_load_texture(const uint16_t *data, int w, int h, int vx, int vy) {
    RECT r;
    r.x = vx; r.y = vy; r.w = w; r.h = h;
    LoadImage(&r, (const uint32_t *)data);
    DrawSync(0);
    return getTPage(2, 0, vx, vy);   /* 2 = 16-bit direct color */
}

void eng3d_draw_floor_tex(int32_t cx, int32_t cz, int32_t half,
                          uint16_t tpage, uint8_t shade,
                          uint32_t *ot, char **pri) {
    SVECTOR v[4];
    MATRIX  mvp;
    POLY_FT4 *pol;
    int p;

    v[0].vx = -half; v[0].vy = 0; v[0].vz = -half;
    v[1].vx =  half; v[1].vy = 0; v[1].vz = -half;
    v[2].vx = -half; v[2].vy = 0; v[2].vz =  half;
    v[3].vx =  half; v[3].vy = 0; v[3].vz =  half;

    model.t[0] = cx; model.t[1] = 0; model.t[2] = cz;
    CompMatrixLV(&view, &model, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_FT4 *)(*pri);
    gte_ldv3(&v[0], &v[1], &v[2]);
    gte_rtpt();
    gte_avsz4();
    gte_stotz(&p);
    p >>= 2;
    if (p > 0 && p < OT_LEN) {
        setPolyFT4(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        gte_ldv0(&v[3]);
        gte_rtps();
        gte_stsxy(&pol->x3);
        setUVWH(pol, 0, 0, 127, 127);
        pol->tpage = tpage;
        pol->clut = 0;
        int fm = (shade * fog_mul(p)) >> 8;
        setRGB0(pol, fm, fm, fm);
        addPrim(ot + p, pol);
        pol++;
    }
    *pri = (char *)pol;
}

void eng3d_draw_floor_tex_h(int32_t cx, int32_t cz, int32_t half,
                            int16_t ynw, int16_t yne, int16_t ysw, int16_t yse,
                            uint16_t tpage, uint8_t shade,
                            uint32_t *ot, char **pri) {
    SVECTOR v[4];
    MATRIX  mvp;
    POLY_FT4 *pol;
    int p;

    v[0].vx = -half; v[0].vy = ynw; v[0].vz = -half;
    v[1].vx =  half; v[1].vy = yne; v[1].vz = -half;
    v[2].vx = -half; v[2].vy = ysw; v[2].vz =  half;
    v[3].vx =  half; v[3].vy = yse; v[3].vz =  half;

    model.t[0] = cx; model.t[1] = 0; model.t[2] = cz;
    CompMatrixLV(&view, &model, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_FT4 *)(*pri);
    gte_ldv3(&v[0], &v[1], &v[2]);
    gte_rtpt();
    gte_avsz4();
    gte_stotz(&p);
    p >>= 2;
    if (p > 0 && p < OT_LEN) {
        setPolyFT4(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        gte_ldv0(&v[3]);
        gte_rtps();
        gte_stsxy(&pol->x3);
        setUVWH(pol, 0, 0, 127, 127);
        pol->tpage = tpage;
        pol->clut = 0;
        int fm = (shade * fog_mul(p)) >> 8;
        setRGB0(pol, fm, fm, fm);
        addPrim(ot + p, pol);
        pol++;
    }
    *pri = (char *)pol;
}

void eng3d_draw_box_tex(int32_t cx, int32_t cy, int32_t cz,
                        int32_t hx, int32_t hy, int32_t hz,
                        uint16_t tpage, uint32_t *ot, char **pri) {
    SVECTOR v[8];
    MATRIX  mvp;
    POLY_FT4 *pol;
    int i, p;
    /* per-face brightness centred on 128 (neutral texture modulation) */
    static const int tshade[6] = { 110, 110, 140, 70, 95, 125 };

    v[0].vx=-hx; v[0].vy=-hy; v[0].vz=-hz;
    v[1].vx= hx; v[1].vy=-hy; v[1].vz=-hz;
    v[2].vx=-hx; v[2].vy= hy; v[2].vz=-hz;
    v[3].vx= hx; v[3].vy= hy; v[3].vz=-hz;
    v[4].vx= hx; v[4].vy=-hy; v[4].vz= hz;
    v[5].vx=-hx; v[5].vy=-hy; v[5].vz= hz;
    v[6].vx= hx; v[6].vy= hy; v[6].vz= hz;
    v[7].vx=-hx; v[7].vy= hy; v[7].vz= hz;

    model.t[0]=cx; model.t[1]=cy; model.t[2]=cz;
    CompMatrixLV(&view, &model, &mvp);
    gte_SetRotMatrix(&mvp);
    gte_SetTransMatrix(&mvp);

    pol = (POLY_FT4 *)(*pri);
    for (i = 0; i < 6; i++) {
        gte_ldv3(&v[box_faces[i].v0], &v[box_faces[i].v1], &v[box_faces[i].v2]);
        gte_rtpt();
        gte_nclip();
        gte_stopz(&p);
        if (p < 0) continue;
        gte_avsz4();
        gte_stotz(&p);
        p >>= 2;
        if (p <= 0 || p >= OT_LEN) continue;

        setPolyFT4(pol);
        gte_stsxy0(&pol->x0);
        gte_stsxy1(&pol->x1);
        gte_stsxy2(&pol->x2);
        gte_ldv0(&v[box_faces[i].v3]);
        gte_rtps();
        gte_stsxy(&pol->x3);
        setUVWH(pol, 0, 0, 127, 127);
        pol->tpage = tpage;
        pol->clut = 0;
        int fm = (tshade[i] * fog_mul(p)) >> 8;
        setRGB0(pol, fm, fm, fm);
        addPrim(ot + p, pol);
        pol++;
    }
    *pri = (char *)pol;
}
