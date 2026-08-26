#include "cutter.h"
#include "core/editor.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <core/entrypoint.h>
#include <util/logger.h>
#include <data/colors.h>
#include <data/input.h>
#include <easyfile.h>
#include <raymath.h>
#include <math.h>
#include <nfd.h>

#define MAX_BLUEPRINT_NAME_SIZE 2048
#define CUT_EPSILON 0.000001f
#define CUT_MAX_VERTICES 4096
#define CUT_MAX_FACE_VERTS 512

typedef enum {
    INDEX_60 = 0,
    INDEX_80 = 1,
    INDEX_96 = 2,
    INDEX_180 = 3,
    INDEX_360 = 4
} IndexWheelType;

typedef enum {
    NO_SYMMETRY = 0,
    RADIAL_SYMMETRY = 1,
    DUAL_SYMMETRY = 2,
    QUAD_SYMMETRY = 3
} SymmetryType;

typedef struct {
    vec3 normal;
    float distance;
} CutterPlane;

typedef enum {
    CUT_OUTSIDE,
    CUT_INSIDE,
    CUT_ON_PLANE
} CutterClassification;

typedef struct {
    vec3 position;
} CutterVertex;
DECLARE_ARRLIST(CutterVertex);
IMPL_ARRLIST(CutterVertex);

typedef struct {
    size_t count;
    size_t vertices[CUT_MAX_FACE_VERTS];
    MaterialID material;
} CutterFace;
DECLARE_ARRLIST(CutterFace);
IMPL_ARRLIST(CutterFace);

typedef struct {
    size_t a;
    size_t b;
} CutterEdgeKey;

typedef struct {
    CutterEdgeKey edge;
    size_t vertex;
} CutterIntersection;
DECLARE_ARRLIST(CutterIntersection);
IMPL_ARRLIST(CutterIntersection);

typedef struct {
    ARRLIST_CutterVertex vertices;
    ARRLIST_CutterFace faces;
} CutterMesh;

typedef struct {
    char name[MAX_BLUEPRINT_NAME_SIZE];
    float angle;
    float depth;
    size_t index;
    size_t offset;
    SymmetryType symmetry;
} Facet;
DECLARE_ARRLIST(Facet);
IMPL_ARRLIST(Facet);

static char g_cut_name[MAX_BLUEPRINT_NAME_SIZE] = "Untitled Cut";
static char* g_index_type_names[5] = { "60", "80", "96", "180", "360" };
static char* g_symmetry_names[4] = { "no symmetry", "radial", "dual", "quad" };
static const size_t g_index_values[5] = { 60, 80, 96, 180, 360 };
static IndexWheelType g_index_type = INDEX_96;
static BOOL g_override_geometry = TRUE;
static ARRLIST_Facet g_pavillion_facets = { 0 };
static ARRLIST_Facet g_crown_facets = { 0 };
static BOOL g_edited = FALSE;
static Vector3 g_stone_dims = { 1.0f, 1.0f, 1.0f };
static size_t g_facetcount = 0;
static Vector3 g_enclosure = { 0 };
static char g_savepath[2048] = "____DOES_NOT_EXIST____";

static size_t DropdownSelectIndexWheel(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) {
        return (size_t)g_index_type;
    } else {
        g_edited = TRUE;
        g_index_type = (IndexWheelType)index;
    }
    return index;
}

static size_t DropdownSelectSymmetry(void* data, size_t index, BOOL cancel) {
    SymmetryType* symmetry = (SymmetryType*)data;
    if (index == (size_t)-1) {
        return (size_t)(*symmetry);
    } else {
        g_edited = TRUE;
        *symmetry = (SymmetryType)index;
    }
    return index;
}

static float IndexToDegree(size_t index) {
    float fi = (float)index;
    float wi = (float)g_index_values[(size_t)g_index_type];
    return (fi / wi) * 360.0f;
}

static void DrawAddFacetButton(ARRLIST_Facet* list, size_t index) {
    Vector2 thispos = (Vector2){ UIGetCursor().x + UIGetPosition().x + 5, UIGetCursor().y + UIGetPosition().y + 5 };
    BOOL mhovered = CheckCollisionPointRec(GetMousePosition(), (Rectangle){ thispos.x, thispos.y, 10, 10});
    DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
    if (mhovered) {
        DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
        if (InputButtonPressed(IK_MOUSELEFT)) {
            ARRLIST_Facet_insert(list, (Facet){ "", 45.0f, 0.0f, 12, 0, RADIAL_SYMMETRY }, index);
            g_edited = TRUE;
        }
    }
    DrawRectangle(UIGetCursor().x + 9, UIGetCursor().y + 7, 2, 6, (Color){ 190, 190, 190, 255 });
    DrawRectangle(UIGetCursor().x + 7, UIGetCursor().y + 9, 6, 2, (Color){ 190, 190, 190, 255 });
}

static void DrawRemoveFacetButton(ARRLIST_Facet* list, size_t index) {
    Vector2 thispos = (Vector2){ UIGetCursor().x + UIGetPosition().x + 5, UIGetCursor().y + UIGetPosition().y + 5 };
    BOOL mhovered = CheckCollisionPointRec(GetMousePosition(), (Rectangle){ thispos.x, thispos.y, 10, 10});
    DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
    if (mhovered) {
        DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
        if (InputButtonPressed(IK_MOUSELEFT)) {
            ARRLIST_Facet_remove(list, index);
            g_edited = TRUE;
        }
    }
    DrawRectangle(UIGetCursor().x + 7, UIGetCursor().y + 9, 6, 2, (Color){ 190, 190, 190, 255 });
}

static void DrawPavillionSection(size_t width, void* param) {
    char nbuffer[64] = { 0 };
    static size_t s_activated = -1;
    for (size_t i = 0; i < g_pavillion_facets.size; i++) {
        UIMoveCursor(20, 0);
        Vector2 thispos = (Vector2){ UIGetCursor().x + UIGetPosition().x, UIGetCursor().y + UIGetPosition().y };
        BOOL mhovered = CheckCollisionPointRec(GetMousePosition(), (Rectangle){ thispos.x, thispos.y, width - 20, LINE_HEIGHT});
        if (mhovered || s_activated == i) {
            DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, LINE_HEIGHT, 
                s_activated == i ? 
                    (Color) {255, 255, 255, 100 } :
                    (Color){ 255, 255, 255, 150 });
            if (InputButtonPressed(IK_MOUSELEFT)) s_activated = i;
            float hop = width - 55;
            float skip = 15;
            UIMoveCursor(hop, 0);
            DrawAddFacetButton(&g_pavillion_facets, i);
            UIMoveCursor(skip, 0);
            DrawRemoveFacetButton(&g_pavillion_facets, i);
            UIMoveCursor(-hop, 0);
            UIMoveCursor(-skip, 0);
        }
        sprintf(nbuffer, "%d.", (int)i + 1);
        UIDrawText(nbuffer);
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5, -LINE_HEIGHT);
        float rads = g_pavillion_facets.data[i].angle * (PI / 180.0f);
        float llen = 16.0f;
        float xrot = UIGetCursor().x + llen * cosf(rads);
        float yrot = UIGetCursor().y + 16 - llen * sinf(rads);
        DrawLine(UIGetCursor().x, UIGetCursor().y + 16, UIGetCursor().x + llen, UIGetCursor().y + 16, MappedColor(UI_TEXT_COLOR));
        DrawLine(UIGetCursor().x, UIGetCursor().y + 16, xrot, yrot, MappedColor(UI_TEXT_COLOR));
        DrawCircleSectorLines((Vector2){ UIGetCursor().x, UIGetCursor().y + 16 }, llen/2.0f, 0.0f, -g_pavillion_facets.data[i].angle, 10, MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(llen + 5, 0);
        if (s_activated == i) {
            g_edited |= UIDragFloat(&(g_pavillion_facets.data[i].angle), 0, 90.0f, 0.01f, 50);
        } else {
            UIDrawText("%.3f", g_pavillion_facets.data[i].angle);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10, -LINE_HEIGHT);
        if (g_pavillion_facets.data[i].index > g_index_values[(size_t)g_index_type]) g_pavillion_facets.data[i].index = g_index_values[(size_t)g_index_type];
        DrawCircle(UIGetCursor().x + 10, UIGetCursor().y + 10, 5, MappedColor(UI_TEXT_COLOR));
        DrawRectanglePro((Rectangle){ UIGetCursor().x + 10, UIGetCursor().y + 10, 2, 8 }, (Vector2){ 1, 8 }, IndexToDegree(g_pavillion_facets.data[i].index), MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragSize(&(g_pavillion_facets.data[i].index), 1, g_index_values[(size_t)g_index_type], 1, 50);
        } else {
            UIDrawText("%d", (int)g_pavillion_facets.data[i].index);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70, -LINE_HEIGHT);
        DrawRectangle(UIGetCursor().x + 8, UIGetCursor().y + 4, 4, 8, MappedColor(UI_TEXT_COLOR));
        DrawTriangle(
            (Vector2){ UIGetCursor().x + 15, UIGetCursor().y + 12 },
            (Vector2){ UIGetCursor().x + 5, UIGetCursor().y + 12 },
            (Vector2){ UIGetCursor().x + 9, UIGetCursor().y + 16 },
            MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragFloat(&(g_pavillion_facets.data[i].depth), -FLT_MAX, FLT_MAX, 0.001f, 50);
        } else {
            UIDrawText("%.3f", g_pavillion_facets.data[i].depth);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 70, -LINE_HEIGHT);
        if (g_pavillion_facets.data[i].offset > g_index_values[(size_t)g_index_type]) g_pavillion_facets.data[i].offset = g_index_values[(size_t)g_index_type];
        DrawCircle(UIGetCursor().x + 10, UIGetCursor().y + 10, 5, MappedColor(UI_TEXT_COLOR));
        DrawRectanglePro((Rectangle){ UIGetCursor().x + 10, UIGetCursor().y + 10, 2, 8 }, (Vector2){ 1, 8 }, IndexToDegree(g_pavillion_facets.data[i].offset), MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragSize(&(g_pavillion_facets.data[i].offset), 0, g_index_values[(size_t)g_index_type], 1, 50);
        } else {
            UIDrawText("%d", (int)g_pavillion_facets.data[i].offset);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 70 + 80, -LINE_HEIGHT);
        if (s_activated == i) {
            UIDropdownMenu(100, 4, g_symmetry_names, DropdownSelectSymmetry, &(g_pavillion_facets.data[i].symmetry));
        } else {
            UIDrawText(g_symmetry_names[(size_t)g_pavillion_facets.data[i].symmetry]);
        }
    }
    UIMoveCursor(10, 0);
    DrawAddFacetButton(&g_pavillion_facets, g_pavillion_facets.size);
    UIMoveCursor(-10, LINE_HEIGHT);
}

static void DrawCrownSection(size_t width, void* param) {
    char nbuffer[64] = { 0 };
    static size_t s_activated = -1;
    for (size_t i = 0; i < g_crown_facets.size; i++) {
        UIMoveCursor(20, 0);
        Vector2 thispos = (Vector2){ UIGetCursor().x + UIGetPosition().x, UIGetCursor().y + UIGetPosition().y };
        BOOL mhovered = CheckCollisionPointRec(GetMousePosition(), (Rectangle){ thispos.x, thispos.y, width - 20, LINE_HEIGHT});
        if (mhovered || s_activated == i) {
            DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, LINE_HEIGHT, 
                s_activated == i ? 
                    (Color) {255, 255, 255, 100 } :
                    (Color){ 255, 255, 255, 150 });
            if (InputButtonPressed(IK_MOUSELEFT)) s_activated = i;
            float hop = width - 55;
            float skip = 15;
            UIMoveCursor(hop, 0);
            DrawAddFacetButton(&g_crown_facets, i);
            UIMoveCursor(skip, 0);
            DrawRemoveFacetButton(&g_crown_facets, i);
            UIMoveCursor(-hop, 0);
            UIMoveCursor(-skip, 0);
        }
        sprintf(nbuffer, "%d.", (int)i + 1);
        UIDrawText(nbuffer);
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5, -LINE_HEIGHT);
        float rads = g_crown_facets.data[i].angle * (PI / 180.0f);
        float llen = 16.0f;
        float xrot = UIGetCursor().x + llen * cosf(rads);
        float yrot = UIGetCursor().y + 16 - llen * sinf(rads);
        DrawLine(UIGetCursor().x, UIGetCursor().y + 16, UIGetCursor().x + llen, UIGetCursor().y + 16, MappedColor(UI_TEXT_COLOR));
        DrawLine(UIGetCursor().x, UIGetCursor().y + 16, xrot, yrot, MappedColor(UI_TEXT_COLOR));
        DrawCircleSectorLines((Vector2){ UIGetCursor().x, UIGetCursor().y + 16 }, llen/2.0f, 0.0f, -g_crown_facets.data[i].angle, 10, MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(llen + 5, 0);
        if (s_activated == i) {
            g_edited |= UIDragFloat(&(g_crown_facets.data[i].angle), 0, 90.0f, 0.01f, 50);
        } else {
            UIDrawText("%.3f", g_crown_facets.data[i].angle);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10, -LINE_HEIGHT);
        if (g_crown_facets.data[i].index > g_index_values[(size_t)g_index_type]) g_crown_facets.data[i].index = g_index_values[(size_t)g_index_type];
        DrawCircle(UIGetCursor().x + 10, UIGetCursor().y + 10, 5, MappedColor(UI_TEXT_COLOR));
        DrawRectanglePro((Rectangle){ UIGetCursor().x + 10, UIGetCursor().y + 10, 2, 8 }, (Vector2){ 1, 8 }, IndexToDegree(g_crown_facets.data[i].index), MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragSize(&(g_crown_facets.data[i].index), 1, g_index_values[(size_t)g_index_type], 1, 50);
        } else {
            UIDrawText("%d", (int)g_crown_facets.data[i].index);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70, -LINE_HEIGHT);
        DrawRectangle(UIGetCursor().x + 8, UIGetCursor().y + 4, 4, 8, MappedColor(UI_TEXT_COLOR));
        DrawTriangle(
            (Vector2){ UIGetCursor().x + 15, UIGetCursor().y + 12 },
            (Vector2){ UIGetCursor().x + 5, UIGetCursor().y + 12 },
            (Vector2){ UIGetCursor().x + 9, UIGetCursor().y + 16 },
            MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragFloat(&(g_crown_facets.data[i].depth), -FLT_MAX, FLT_MAX, 0.001f, 50);
        } else {
            UIDrawText("%.3f", g_crown_facets.data[i].depth);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 70, -LINE_HEIGHT);
        if (g_crown_facets.data[i].offset > g_index_values[(size_t)g_index_type]) g_crown_facets.data[i].offset = g_index_values[(size_t)g_index_type];
        DrawCircle(UIGetCursor().x + 10, UIGetCursor().y + 10, 5, MappedColor(UI_TEXT_COLOR));
        DrawRectanglePro((Rectangle){ UIGetCursor().x + 10, UIGetCursor().y + 10, 2, 8 }, (Vector2){ 1, 8 }, IndexToDegree(g_crown_facets.data[i].offset), MappedColor(UI_TEXT_COLOR));
        UIMoveCursor(20, 0);
        if (s_activated == i) {
            g_edited |= UIDragSize(&(g_crown_facets.data[i].offset), 0, g_index_values[(size_t)g_index_type], 1, 50);
        } else {
            UIDrawText("%d", (int)g_crown_facets.data[i].offset);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 70 + 80, -LINE_HEIGHT);
        if (s_activated == i) {
            UIDropdownMenu(100, 4, g_symmetry_names, DropdownSelectSymmetry, &(g_crown_facets.data[i].symmetry));
        } else {
            UIDrawText(g_symmetry_names[(size_t)g_crown_facets.data[i].symmetry]);
        }
    }
    UIMoveCursor(10, 0);
    DrawAddFacetButton(&g_crown_facets, g_crown_facets.size);
    UIMoveCursor(-10, LINE_HEIGHT);
}

static float CutterPlaneDistance(CutterPlane plane, vec3 p) {
    return glm_vec3_dot(plane.normal, p) - plane.distance;
}

static size_t CutterAddVertex(CutterMesh* mesh, vec3 position) {
    EZ_ASSERT(mesh->vertices.size < CUT_MAX_VERTICES, "Cutter vertex limit exceeded");
    CutterVertex vertex;
    glm_vec3_copy(position, vertex.position);
    size_t id = mesh->vertices.size;
    ARRLIST_CutterVertex_add(&mesh->vertices, vertex);
    return id;
}

static size_t CutterAddFace(CutterMesh* mesh, const size_t* vertices, size_t count, MaterialID material) {
    EZ_ASSERT(count >= 3, "Cannot create cutter face with fewer than 3 vertices");
    EZ_ASSERT(count <= CUT_MAX_FACE_VERTS, "Cutter face vertex limit exceeded");
    CutterFace face = { 0 };
    face.count = count;
    face.material = material;
    for (size_t i = 0; i < count; i++)
        face.vertices[i] = vertices[i];
    size_t id = mesh->faces.size;
    ARRLIST_CutterFace_add(&mesh->faces, face);
    return id;
}

static void CutterClearMesh(CutterMesh* mesh) {
    ARRLIST_CutterVertex_clear(&mesh->vertices);
    ARRLIST_CutterFace_clear(&mesh->faces);
}

static void CutterCanonicalEdge(size_t a, size_t b, CutterEdgeKey* out) {
    if (a < b) {
        out->a = a;
        out->b = b;
    } else {
        out->a = b;
        out->b = a;
    }
}

static BOOL CutterFindIntersection(ARRLIST_CutterIntersection* intersections, CutterEdgeKey edge, size_t* vertex) {
    for (size_t i = 0; i < intersections->size; i++) {
        CutterIntersection* intersection = &intersections->data[i];
        if (intersection->edge.a == edge.a && intersection->edge.b == edge.b) {
            *vertex = intersection->vertex;
            return TRUE;
        }
    }
    return FALSE;
}

static size_t CutterGetIntersection(CutterMesh* mesh, ARRLIST_CutterIntersection* intersections, CutterPlane plane, size_t a, size_t b) {
    CutterEdgeKey edge;
    CutterCanonicalEdge(a, b, &edge);
    size_t existing;
    if (CutterFindIntersection(intersections, edge, &existing)) return existing;
    vec3 pa;
    vec3 pb;
    glm_vec3_copy(mesh->vertices.data[a].position, pa);
    glm_vec3_copy(mesh->vertices.data[b].position, pb);
    float da = glm_vec3_dot(plane.normal, pa) - plane.distance;
    float db = glm_vec3_dot(plane.normal, pb) - plane.distance;
    float denominator = da - db;
    vec3 position;
    if (fabsf(denominator) < CUT_EPSILON) {
        if (fabsf(da) < fabsf(db)) glm_vec3_copy(pa, position);
        else glm_vec3_copy(pb, position);
    } else {
        float t = da / denominator;
        glm_vec3_sub(pb, pa, position);
        glm_vec3_scale(position, t, position);
        glm_vec3_add(pa, position, position);
    }
    size_t id = CutterAddVertex(mesh, position);
    CutterIntersection intersection = { edge, id };
    ARRLIST_CutterIntersection_add(intersections, intersection);
    return id;
}

static CutterClassification CutterClassify(CutterPlane plane, vec3 position) {
    float distance = CutterPlaneDistance(plane, position);
    if (distance > CUT_EPSILON) return CUT_OUTSIDE;
    if (distance < -CUT_EPSILON) return CUT_INSIDE;
    return CUT_ON_PLANE;
}

static size_t CutterClipFace(CutterMesh* mesh, CutterFace* face, CutterPlane plane, ARRLIST_CutterIntersection* intersections, size_t* output) {
    size_t output_count = 0;
    for (size_t i = 0; i < face->count; i++) {
        size_t a = face->vertices[i];
        size_t b = face->vertices[(i + 1) % face->count];
        vec3 pa;
        vec3 pb;
        glm_vec3_copy(mesh->vertices.data[a].position, pa);
        glm_vec3_copy(mesh->vertices.data[b].position, pb);
        CutterClassification ca = CutterClassify(plane, pa);
        CutterClassification cb = CutterClassify(plane, pb);
        BOOL a_inside = ca != CUT_OUTSIDE;
        BOOL b_inside = cb != CUT_OUTSIDE;
        if (a_inside && b_inside) {
            output[output_count++] = b;
        } else if (a_inside && !b_inside) {
            size_t intersection = CutterGetIntersection(mesh, intersections, plane, a, b);
            output[output_count++] = intersection;
        } else if (!a_inside && b_inside) {
            size_t intersection = CutterGetIntersection(mesh, intersections, plane, a, b);
            output[output_count++] = intersection;
            output[output_count++] = b;
        }
    }
    size_t compact_count = 0;
    for (size_t i = 0; i < output_count; i++) {
        if (compact_count > 0 && output[compact_count - 1] == output[i]) continue;
        output[compact_count++] = output[i];
    }
    if (compact_count >= 2 && output[0] == output[compact_count - 1]) compact_count--;
    return compact_count;
}

static BOOL CutterContainsVertex(size_t* vertices, size_t count, size_t vertex) {
    for (size_t i = 0; i < count; i++) {
        if (vertices[i] == vertex) return TRUE;
    }
    return FALSE;
}

static void CutterAddCapVertex(CutterMesh* mesh, CutterPlane plane, size_t* cap_vertices, size_t* cap_count, size_t vertex) {
    if (CutterContainsVertex(cap_vertices, *cap_count, vertex)) return;
    vec3 position;
    glm_vec3_copy(mesh->vertices.data[vertex].position, position);
    if (fabsf(CutterPlaneDistance(plane, position)) > CUT_EPSILON) return;
    EZ_ASSERT(*cap_count < CUT_MAX_FACE_VERTS, "Cutter cap vertex limit exceeded");
    cap_vertices[(*cap_count)++] = vertex;
}

static void CutterSortCapVertices(CutterMesh* mesh, CutterPlane plane, size_t* vertices, size_t count) {
    if (count < 3) return;
    vec3 reference = { 0.0f, 1.0f, 0.0f };
    if (fabsf(glm_vec3_dot(reference, plane.normal)) > 0.9f) {
        reference[0] = 1.0f;
        reference[1] = 0.0f;
        reference[2] = 0.0f;
    }
    vec3 axis_x;
    vec3 axis_y;
    glm_vec3_cross(reference, plane.normal, axis_x);
    glm_vec3_normalize(axis_x);
    glm_vec3_cross(plane.normal, axis_x, axis_y);
    glm_vec3_normalize(axis_y);
    vec3 center = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < count; i++) {
        glm_vec3_add(center,mesh->vertices.data[vertices[i]].position, center);
    }
    glm_vec3_scale(center, 1.0f / (float)count, center);
    for (size_t i = 1; i < count; i++) {
        size_t key = vertices[i];
        vec3 d;
        glm_vec3_sub(mesh->vertices.data[key].position, center, d);
        float key_angle = atan2f(glm_vec3_dot(d, axis_y), glm_vec3_dot(d, axis_x));
        size_t j = i;
        while (j > 0) {
            size_t previous = vertices[j - 1];
            glm_vec3_sub(mesh->vertices.data[previous].position, center, d);
            float previous_angle = atan2f(glm_vec3_dot(d, axis_y), glm_vec3_dot(d, axis_x));
            if (previous_angle <= key_angle) break;
            vertices[j] = vertices[j - 1];
            j--;
        }
        vertices[j] = key;
    }
}

static void CutterCutPlane(CutterMesh* mesh, CutterPlane plane) {
    ARRLIST_CutterIntersection intersections = { 0 };
    ARRLIST_CutterFace new_faces = { 0 };
    size_t cap_vertices[CUT_MAX_FACE_VERTS];
    size_t cap_count = 0;
    for (size_t i = 0; i < mesh->faces.size; i++) {
        CutterFace* face = &mesh->faces.data[i];
        size_t clipped[CUT_MAX_FACE_VERTS + 1];
        size_t clipped_count = CutterClipFace(mesh, face, plane, &intersections, clipped);
        if (clipped_count < 3) continue;
        CutterFace new_face = { 0 };
        new_face.count = clipped_count;
        new_face.material = face->material;
        for (size_t j = 0; j < clipped_count; j++) {
            new_face.vertices[j] = clipped[j];
            CutterAddCapVertex(mesh, plane, cap_vertices, &cap_count, clipped[j]);
        }
        ARRLIST_CutterFace_add(&new_faces, new_face);
    }
    if (cap_count >= 3) {
        CutterSortCapVertices(mesh, plane, cap_vertices, cap_count);
        CutterFace cap = { 0 };
        cap.count = cap_count;
        for (size_t i = 0; i < cap_count; i++) {
            cap.vertices[i] = cap_vertices[i];
        }
        cap.material = 0;
        vec3 a;
        vec3 b;
        vec3 c;
        glm_vec3_copy(mesh->vertices.data[cap.vertices[0]].position, a);
        glm_vec3_copy(mesh->vertices.data[cap.vertices[1]].position, b);
        glm_vec3_copy(mesh->vertices.data[cap.vertices[2]].position, c);
        vec3 ab;
        vec3 ac;
        vec3 normal;
        glm_vec3_sub(b, a, ab);
        glm_vec3_sub(c, a, ac);
        glm_vec3_cross(ab, ac, normal);
        if (glm_vec3_dot(normal, plane.normal) > 0.0f) {
            for (size_t i = 0; i < cap_count / 2; i++) {
                size_t temp = cap.vertices[i];
                cap.vertices[i] = cap.vertices[cap_count - 1 - i];
                cap.vertices[cap_count - 1 - i] = temp;
            }
        }
        ARRLIST_CutterFace_add(&new_faces, cap);
    }
    ARRLIST_CutterFace_clear(&mesh->faces);
    mesh->faces = new_faces;
    ARRLIST_CutterIntersection_clear(&intersections);
}

static void CutterCreateCube(CutterMesh* mesh) {
    vec3 positions[8] = {
        { -g_stone_dims.x, -g_stone_dims.y, -g_stone_dims.z },
        { -g_stone_dims.x,  g_stone_dims.y, -g_stone_dims.z },
        {  g_stone_dims.x,  g_stone_dims.y, -g_stone_dims.z },
        {  g_stone_dims.x, -g_stone_dims.y, -g_stone_dims.z },
        { -g_stone_dims.x, -g_stone_dims.y,  g_stone_dims.z },
        { -g_stone_dims.x,  g_stone_dims.y,  g_stone_dims.z },
        {  g_stone_dims.x,  g_stone_dims.y,  g_stone_dims.z },
        {  g_stone_dims.x, -g_stone_dims.y,  g_stone_dims.z }
    };
    for (size_t i = 0; i < 8; i++) CutterAddVertex(mesh, positions[i]);
    size_t faces[6][4] = {
        { 0, 3, 2, 1 },
        { 4, 5, 6, 7 },
        { 0, 1, 5, 4 },
        { 3, 7, 6, 2 },
        { 0, 4, 7, 3 },
        { 1, 2, 6, 5 }
    };
    for (size_t i = 0; i < 6; i++) CutterAddFace(mesh, faces[i], 4, 0);
}

static CutterPlane CutterMakePlane(const Facet* facet, BOOL pavilion, size_t index) {
    float theta = IndexToDegree(index) * (PI / 180.0f);
    float angle = (90.0f - facet->angle) * (PI / 180.0f);
    float radial = cosf(angle);
    float vertical = sinf(angle);
    CutterPlane plane = { 0 };
    plane.normal[0] = cosf(theta) * radial;
    plane.normal[1] = pavilion ? -vertical : vertical;
    plane.normal[2] = sinf(theta) * radial;
    glm_vec3_normalize(plane.normal);
    plane.distance = 1.0f - facet->depth;
    return plane;
}

static void CutterAddUniqueIndex(size_t* indices, size_t* count, size_t index, size_t wheel) {
    index %= wheel;
    for (size_t i = 0; i < *count; i++) {
        if (indices[i] == index) return;
    }
    indices[(*count)++] = index;
}

static size_t CutterFacetIndices(const Facet* facet, size_t* output) {
    size_t wheel = g_index_values[(size_t)g_index_type];
    size_t count = 0;
    size_t index = facet->index % wheel;
    size_t offset = facet->offset;
    switch (facet->symmetry) {
        case NO_SYMMETRY:
            CutterAddUniqueIndex(output, &count, index + offset, wheel);
            break;
        case RADIAL_SYMMETRY:
            if (index == 0) {
                CutterAddUniqueIndex(output, &count, 0 + offset, wheel);
                break;
            }
            for (size_t i = index; i <= wheel; i += index) {
                CutterAddUniqueIndex(output, &count, i + offset, wheel);
            }
            break;
        case DUAL_SYMMETRY:
            CutterAddUniqueIndex(output, &count, index + offset, wheel);
            CutterAddUniqueIndex(output, &count, ((wheel - index) % wheel) + offset, wheel);
            break;
        case QUAD_SYMMETRY: {
            size_t half = wheel / 2;
            CutterAddUniqueIndex(output, &count, index + offset, wheel);
            CutterAddUniqueIndex(output, &count, ((wheel - index) % wheel) + offset, wheel);
            CutterAddUniqueIndex(output, &count, ((index + half) % wheel) + offset, wheel);
            CutterAddUniqueIndex(output, &count, ((half + wheel - index) % wheel) + offset, wheel);
            break;
        }
        default:
            break;
    }
    return count;
}

static void CutterApplyFacets(CutterMesh* mesh, ARRLIST_Facet* facets, BOOL pavilion) {
    size_t indices[512];
    for (size_t i = 0; i < facets->size; i++) {
        Facet* facet = &facets->data[i];
        size_t count = CutterFacetIndices(facet, indices);
        for (size_t j = 0; j < count; j++) {
            CutterPlane plane = CutterMakePlane(facet, pavilion, indices[j]);
            CutterCutPlane(mesh, plane);
        }
    }
}

static void SlowMinMax(vec3 min, vec3 max, vec3 v) {
    if (v[0] < min[0]) min[0] = v[0];
    if (v[1] < min[1]) min[1] = v[1];
    if (v[2] < min[2]) min[2] = v[2];
    if (v[0] > max[0]) max[0] = v[0];
    if (v[1] > max[1]) max[1] = v[1];
    if (v[2] > max[2]) max[2] = v[2];
}

static void CutterSubmitMesh(CutterMesh* mesh) {
    ClearScene(FALSE);
    size_t vertex_base = NumVertices();
    size_t triangle_base = NumTriangles();
    for (size_t i = 0; i < mesh->vertices.size; i++) {
        SubmitVertex(mesh->vertices.data[i].position);
    }
    vec3 fmin = { FLT_MAX, FLT_MAX, FLT_MAX };
    vec3 fmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < mesh->faces.size; i++) {
        CutterFace* face = &mesh->faces.data[i];
        for (size_t j = 1; j + 1 < face->count; j++) {
            VertexID a = vertex_base + face->vertices[0];
            VertexID b = vertex_base + face->vertices[j];
            VertexID c = vertex_base + face->vertices[j + 1];
            SlowMinMax(fmin, fmax, VertexReference(a));
            SlowMinMax(fmin, fmax, VertexReference(b));
            SlowMinMax(fmin, fmax, VertexReference(c));
            SubmitTriangle((Triangle){
                a, b, c,
                (VertexID)-1,
                (VertexID)-1,
                (VertexID)-1,
                face->material
            });
        }
    }
    g_enclosure = (Vector3){ fmax[0] - fmin[0], fmax[1] - fmin[1], fmax[2] - fmin[2] };
    if (mesh->vertices.size > 0 && mesh->faces.size > 0) {
        vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
        vec3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (size_t i = 0; i < mesh->vertices.size; i++) {
            glm_vec3_minv(min, mesh->vertices.data[i].position, min);
            glm_vec3_maxv(max, mesh->vertices.data[i].position, max);
        }
        vec3 center;
        vec3 extents;
        glm_vec3_add(min, max, center);
        glm_vec3_scale(center, 0.5f, center);
        glm_vec3_sub(max, min, extents);
        glm_vec3_scale(extents, 0.5f, extents);
        SubmitMeshDescriptor(
            (MeshDescriptor){
                FALSE,
                vertex_base,
                vertex_base + mesh->vertices.size - 1,
                triangle_base,
                NumTriangles() - 1,
                0,
                (uint32_t)-1,
                { 0 },
                INLINEV3(extents),
                { 0 },
                { 0 },
                { 1.0f, 1.0f, 1.0f },
                GLM_MAT4_IDENTITY_INIT
            },
            g_cut_name);
    }
    UpdateVertices();
    UpdateTriangles();
    UpdateMeshes();
}

static void UpdateCutterPanel(float width, float height) {
    if (!g_edited || !g_override_geometry) return;
    g_edited = FALSE;
    CutterMesh mesh = { 0 };
    CutterCreateCube(&mesh);
    CutterApplyFacets(&mesh, &g_pavillion_facets, TRUE);
    CutterApplyFacets(&mesh, &g_crown_facets, FALSE);
    CutterSubmitMesh(&mesh);
    g_facetcount = mesh.faces.size;
    CutterClearMesh(&mesh);
}

static void SaveCutterDesign() {
    FILE* file = fopen(g_savepath, "wb");
    fwrite(g_cut_name, MAX_BLUEPRINT_NAME_SIZE, 1, file);
    fwrite(&g_index_type, sizeof(IndexWheelType), 1, file);
    fwrite(&g_override_geometry, sizeof(BOOL), 1, file);
    fwrite(&g_stone_dims, sizeof(Vector3), 1, file);
    fwrite(&g_pavillion_facets, sizeof(ARRLIST_Facet), 1, file);
    fwrite(g_pavillion_facets.data, sizeof(Facet), g_pavillion_facets.size, file);
    fwrite(&g_crown_facets, sizeof(ARRLIST_Facet), 1, file);
    fwrite(g_crown_facets.data, sizeof(Facet), g_crown_facets.size, file);
    fclose(file);
    loginfo("Successfully saved stone design to \"%s\"", g_savepath);
}

static void LoadCutterDesign() {
    FILE* file = fopen(g_savepath, "rb");
    fread(g_cut_name, MAX_BLUEPRINT_NAME_SIZE, 1, file);
    fread(&g_index_type, sizeof(IndexWheelType), 1, file);
    fread(&g_override_geometry, sizeof(BOOL), 1, file);
    fread(&g_stone_dims, sizeof(Vector3), 1, file);
    ARRLIST_Facet_clear(&g_pavillion_facets);
    ARRLIST_Facet_clear(&g_crown_facets);
    fread(&g_pavillion_facets, sizeof(ARRLIST_Facet), 1, file);
    g_pavillion_facets.data = EZ_ALLOC(sizeof(Facet), g_pavillion_facets.maxsize);
    fread(g_pavillion_facets.data, sizeof(Facet), g_pavillion_facets.size, file);
    fread(&g_crown_facets, sizeof(ARRLIST_Facet), 1, file);
    g_crown_facets.data = EZ_ALLOC(sizeof(Facet), g_crown_facets.maxsize);
    fread(g_crown_facets.data, sizeof(Facet), g_crown_facets.size, file);
    fclose(file);
    g_edited = TRUE;
    loginfo("Successfully loaded stone design from \"%s\"", g_savepath);
}

static void DrawCutterPanel(float width, float height) {
    g_edited = FALSE;
    float boxwidth = width - 20 - 160;
    float boxstride = width - boxwidth - 20;
    float component_width = (width - 20 - 160 - (3 * 16) - (2 * 10)) / 3.0f;
    UITextInput("Name", g_cut_name, MAX_BLUEPRINT_NAME_SIZE, width - 20, FALSE);
    UIDivider(width - 20);
    UIDrawText("Index Wheel");
    UIMoveCursor(boxstride, -LINE_HEIGHT);
    UIDropdownMenu(boxwidth, 5, g_index_type_names, DropdownSelectIndexWheel, NULL);
    UIDrawText("Override Geometry");
    UIMoveCursor(boxstride - 2, -LINE_HEIGHT);
    BOOL old = g_override_geometry;
    UICheckbox(&g_override_geometry);
    if (!old && g_override_geometry) g_edited = TRUE;
    UIDrawText("Base Stone");
    UIMoveCursor(boxstride + 7, 5 - LINE_HEIGHT);
    DrawRectangle(UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18, RED);
    if (CheckCollisionPointRec(Vector2Subtract(GetMousePosition(), UIGetPosition()), (Rectangle){UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18}) &&
        InputButtonPressed(IK_MOUSELEFT)) {
        g_stone_dims.x = 1.0f;
    }
    UIDrawText("x");
    UIMoveCursor(boxstride + 19, -20);
    g_edited |= UIDragFloat(&(g_stone_dims.x), -FLT_MAX, FLT_MAX, 0.001f, component_width);
    UIMoveCursor(boxstride + component_width + 33, -20);
    DrawRectangle(UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18, GREEN);
    if (CheckCollisionPointRec(Vector2Subtract(GetMousePosition(), UIGetPosition()), (Rectangle){UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18}) &&
        InputButtonPressed(IK_MOUSELEFT)) {
        g_stone_dims.y = 1.0f;
    }
    UIDrawText("y");
    UIMoveCursor(boxstride + component_width + 44, -20);
    g_edited |= UIDragFloat(&(g_stone_dims.y), -FLT_MAX, FLT_MAX, 0.001f, component_width);
    UIMoveCursor(boxstride + (2*component_width) + 58, -20);
    DrawRectangle(UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18, BLUE);
    if (CheckCollisionPointRec(Vector2Subtract(GetMousePosition(), UIGetPosition()), (Rectangle){UIGetCursor().x - 5, UIGetCursor().y + 1, 20, 18}) &&
        InputButtonPressed(IK_MOUSELEFT)) {
        g_stone_dims.z = 1.0f;
    }
    UIDrawText("z");
    UIMoveCursor(boxstride + (2*component_width) + 69, -20);
    g_edited |= UIDragFloat(&(g_stone_dims.z), -FLT_MAX, FLT_MAX, 0.001f, component_width);
    float cri_ri = 1.0f;
    for (size_t i = 0; i < g_pavillion_facets.size; i++) {
        float ri = 1.0f / sinf(g_pavillion_facets.data[i].angle * M_PI / 180.0f);
        if (ri > cri_ri) cri_ri = ri;
    }
    UIDrawText("Stats");
    UIMoveCursor(boxstride + 5, -LINE_HEIGHT + 5);
    DrawRectangle(UIGetCursor().x - 5, UIGetCursor().y - 2, boxwidth, LINE_HEIGHT * 3 + 4, (Color){ 255, 255, 255, 120 });
    UIDrawText("L/W/H Ratio: 1.00 / %.2f / %.2f", g_enclosure.y / g_enclosure.x, g_enclosure.z / g_enclosure.x);
    UIMoveCursor(boxstride + 5, 0);
    UIDrawText("Facets: %d", (int)g_facetcount);
    UIMoveCursor(boxstride + 5, 0);
    UIDrawText("Critical RI: %.2f", cri_ri);
    UIMoveCursor(0, 2);
    UIDivider(width - 20);
    UIDropdownSection("Pavillion", width - 20, DrawPavillionSection, NULL);
    UIDropdownSection("Crown", width - 20, DrawCrownSection, NULL);
    UIDivider(width - 20);
    float bw = (width - 20 - 20) / 3.0f;
    if (UIButton("Save", bw)) {
        if (ez_file_exists(g_savepath)) {
            SaveCutterDesign();
        } else {
            nfdchar_t* outpath = NULL;
            nfdresult_t result = NFD_SaveDialog("crys", NULL, &outpath);
            if (result == NFD_OKAY) {
                sprintf(g_savepath, "%s%s", outpath, strstr(outpath, ".crys") ? "" : ".crys");
                SaveCutterDesign();
            } else if (result == NFD_CANCEL) {
                logtrace("Save cancelled by user");
            } else {
                logerror("Unable to save file due to NFD error: %s", NFD_GetError());
            }
        }
    }
    UIMoveCursor(bw + 10, -LINE_HEIGHT);
    if (UIButton("Save As...", bw)) {
        nfdchar_t* outpath = NULL;
        nfdresult_t result = NFD_SaveDialog("crys", NULL, &outpath);
        if (result == NFD_OKAY) {
            sprintf(g_savepath, "%s%s", outpath, strstr(outpath, ".crys") ? "" : ".crys");
            SaveCutterDesign();
        } else if (result == NFD_CANCEL) {
            logtrace("Save cancelled by user");
        } else {
            logerror("Unable to save file due to NFD error: %s", NFD_GetError());
        }
    }
    UIMoveCursor(bw + 10 + bw + 10, -LINE_HEIGHT);
    if (UIButton("Load", bw)) {
        nfdchar_t* outpath = NULL;
        nfdresult_t result = NFD_OpenDialog("crys", NULL, &outpath);
        if (result == NFD_OKAY) {
            sprintf(g_savepath, "%s%s", outpath, strstr(outpath, ".crys") ? "" : ".crys");
            LoadCutterDesign();
        } else if (result == NFD_CANCEL) {
            logtrace("Load cancelled by user");
        } else {
            logerror("Unable to open file due to NFD error: %s", NFD_GetError());
        }
    }
}

static void CleanCutterPanel() {
    ARRLIST_Facet_clear(&g_pavillion_facets);
    ARRLIST_Facet_clear(&g_crown_facets);
}

void InjectCutterPanel() {
    ARRLIST_Panel_add(EditorSharedPanels(), GenerateCutterPanel());
    EditorDefaultUIConfig()->data[EditorDefaultUIConfig()->size - 1].vine = TRUE;
    ARRLIST_UIConfig_add(EditorDefaultUIConfig(), (UIConfig){"Cutter", 0.0f, FALSE, FALSE, FALSE, FALSE});
}

Panel GenerateCutterPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Cutter");
	p.draw = DrawCutterPanel;
    p.update = UpdateCutterPanel;
    p.clean = CleanCutterPanel;
    p.scrollable = TRUE;
	return p;
}

REGISTER_PRELOAD(InjectCutterPanel);
