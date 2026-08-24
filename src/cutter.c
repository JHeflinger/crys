#include "cutter.h"
#include "core/editor.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <core/entrypoint.h>
#include <data/colors.h>
#include <data/input.h>
#include <math.h>

#define MAX_BLUEPRINT_NAME_SIZE 2048

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

typedef struct {
    vec3 v[3];
    MaterialID material;
} CutterTriangle;
DECLARE_ARRLIST(CutterTriangle);
IMPL_ARRLIST(CutterTriangle);

typedef struct {
    char name[MAX_BLUEPRINT_NAME_SIZE];
    float angle;
    float depth;
    size_t index;
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

static float CutterPlaneDistance(CutterPlane plane, vec3 p) {
    return glm_vec3_dot(plane.normal, p) - plane.distance;
}

static void CutterPlaneIntersection(CutterPlane plane, vec3 p1, vec3 p2, vec3 out) {
    float d1 = CutterPlaneDistance(plane, p1);
    float d2 = CutterPlaneDistance(plane, p2);
    float denom = d1 - d2;
    if (fabsf(denom) < 0.000001f) {
        glm_vec3_copy(p1, out);
        return;
    }
    float t = d1 / denom;
    glm_vec3_sub(p2, p1, out);
    glm_vec3_scale(out, t, out);
    glm_vec3_add(p1, out, out);
}

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

static float IndexToRadians(size_t index) {
    return IndexToDegree(index) * (PI / 180.0f);
}

static void DrawAddFacetButton(ARRLIST_Facet* list, size_t index) {
    Vector2 thispos = (Vector2){ UIGetCursor().x + UIGetPosition().x + 5, UIGetCursor().y + UIGetPosition().y + 5 };
    BOOL mhovered = CheckCollisionPointRec(GetMousePosition(), (Rectangle){ thispos.x, thispos.y, 10, 10});
    DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
    if (mhovered) {
        DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 5, 10, 10, (Color){ 255, 255, 255, 150 });
        if (InputButtonPressed(IK_MOUSELEFT)) {
            ARRLIST_Facet_insert(list, (Facet){ "", 45.0f, 0.0f, 12, RADIAL_SYMMETRY }, index);
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
            g_edited |= UIDragFloat(&(g_pavillion_facets.data[i].depth), 0, 1.0f, 0.01f, 50);
        } else {
            UIDrawText("%.3f", g_pavillion_facets.data[i].depth);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 80, -LINE_HEIGHT);
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
            g_edited |= UIDragFloat(&(g_crown_facets.data[i].depth), 0, 1.0f, 0.01f, 50);
        } else {
            UIDrawText("%.3f", g_crown_facets.data[i].depth);
        }
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 70 + 80, -LINE_HEIGHT);
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

static size_t CutterClipTriangle(CutterPlane plane, const CutterTriangle* triangle, vec3 out[4]) {
    size_t count = 0;
    vec3* input = (vec3*)triangle->v;
    for (size_t i = 0; i < 3; i++) {
        vec3 a;
        vec3 b;
        glm_vec3_copy(input[i], a);
        glm_vec3_copy(input[(i + 1) % 3], b);
        float da = CutterPlaneDistance(plane, a);
        float db = CutterPlaneDistance(plane, b);
        BOOL a_inside = da <= 0.000001f;
        BOOL b_inside = db <= 0.000001f;
        if (a_inside && b_inside) {
            glm_vec3_copy(b, out[count++]);
        }
        else if (a_inside && !b_inside) {
            CutterPlaneIntersection(plane, a, b, out[count++]);
        }
        else if (!a_inside && b_inside) {
            CutterPlaneIntersection(plane, a, b, out[count++]);
            glm_vec3_copy(b, out[count++]);
        }
    }
    return count;
}


static int CutterAddUniquePoint(vec3* points, int count, int max_points, vec3 point) {
    const float epsilon = 0.00001f;
    for (int i = 0; i < count; i++) {
        vec3 d;
        glm_vec3_sub(points[i], point, d);
        if (glm_vec3_norm2(d) < epsilon * epsilon)
            return count;
    }
    if (count < max_points) {
        glm_vec3_copy(point, points[count]);
        return count + 1;
    }
    return count;
}


static void CutterSortPoints(vec3* points, int count, vec3 normal) {
    if (count < 3)
        return;
    vec3 reference = { 0.0f, 1.0f, 0.0f };
    if (fabsf(glm_vec3_dot(normal, reference)) > 0.9f) {
        reference[0] = 1.0f;
        reference[1] = 0.0f;
        reference[2] = 0.0f;
    }
    vec3 axis_x;
    vec3 axis_y;
    glm_vec3_cross(reference, normal, axis_x);
    glm_vec3_normalize(axis_x);
    glm_vec3_cross(normal, axis_x, axis_y);
    glm_vec3_normalize(axis_y);
    vec3 center = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < count; i++)
        glm_vec3_add(center, points[i], center);
    glm_vec3_scale(center, 1.0f / (float)count, center);
    for (int i = 1; i < count; i++) {
        vec3 key;
        glm_vec3_copy(points[i], key);
        vec3 d;
        glm_vec3_sub(key, center, d);
        float key_angle = atan2f(
            glm_vec3_dot(d, axis_y),
            glm_vec3_dot(d, axis_x));
        int j = i - 1;
        while (j >= 0) {
            vec3 dj;
            glm_vec3_sub(points[j], center, dj);
            float angle_j = atan2f(
                glm_vec3_dot(dj, axis_y),
                glm_vec3_dot(dj, axis_x));
            if (angle_j <= key_angle)
                break;
            glm_vec3_copy(points[j], points[j + 1]);
            j--;
        }
        glm_vec3_copy(key, points[j + 1]);
    }
}


static void CutterClipMesh(ARRLIST_CutterTriangle* mesh, CutterPlane plane) {
    ARRLIST_CutterTriangle result = { 0 };
    vec3 cut_points[256];
    int cut_point_count = 0;
    for (size_t i = 0; i < mesh->size; i++) {
        CutterTriangle* triangle = &mesh->data[i];
        vec3 polygon[4];
        size_t count = CutterClipTriangle(plane, triangle, polygon);
        if (count >= 3) {
            for (size_t j = 1; j + 1 < count; j++) {
                CutterTriangle out = { 0 };
                glm_vec3_copy(polygon[0], out.v[0]);
                glm_vec3_copy(polygon[j], out.v[1]);
                glm_vec3_copy(polygon[j + 1], out.v[2]);
                out.material = triangle->material;
                ARRLIST_CutterTriangle_add(&result, out);
            }
        }
        for (int edge = 0; edge < 3; edge++) {
            vec3 a;
            vec3 b;
            glm_vec3_copy(triangle->v[edge], a);
            glm_vec3_copy(triangle->v[(edge + 1) % 3], b);
            float da = CutterPlaneDistance(plane, a);
            float db = CutterPlaneDistance(plane, b);
            if (fabsf(da) < 0.00001f) {
                cut_point_count = CutterAddUniquePoint(cut_points, cut_point_count, 256, a);
            }
            if ((da < 0.0f && db > 0.0f) ||
                (da > 0.0f && db < 0.0f)) {
                vec3 intersection;
                CutterPlaneIntersection(plane, a, b, intersection);
                cut_point_count = CutterAddUniquePoint(cut_points, cut_point_count, 256, intersection);
            }
        }
    }
    if (cut_point_count >= 3) {
        CutterSortPoints(cut_points, cut_point_count, plane.normal);
        for (int i = 1; i < cut_point_count - 1; i++) {
            CutterTriangle cap = { 0 };
            glm_vec3_copy(cut_points[0], cap.v[0]);
            glm_vec3_copy(cut_points[i + 1], cap.v[1]);
            glm_vec3_copy(cut_points[i], cap.v[2]);
            cap.material = 0;
            ARRLIST_CutterTriangle_add(&result, cap);
        }
    }
    ARRLIST_CutterTriangle_clear(mesh);
    *mesh = result;
}

static void CutterCreateCube(ARRLIST_CutterTriangle* mesh) {
    vec3 v[8] = {
        { -1.0f, -1.0f, -1.0f },
        { -1.0f,  1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f },
        {  1.0f, -1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f },
        { -1.0f,  1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        {  1.0f, -1.0f,  1.0f }
    };
    size_t faces[][3] = {
        { 3, 1, 0 },
        { 3, 2, 1 },
        { 2, 5, 1 },
        { 2, 6, 5 },
        { 7, 2, 3 },
        { 7, 6, 2 },
        { 3, 0, 4 },
        { 4, 7, 3 },
        { 5, 6, 7 },
        { 7, 4, 5 },
        { 0, 1, 5 },
        { 5, 4, 0 }
    };
    for (size_t i = 0; i < 12; i++) {
        CutterTriangle triangle = { 0 };
        glm_vec3_copy(v[faces[i][0]], triangle.v[0]);
        glm_vec3_copy(v[faces[i][1]], triangle.v[1]);
        glm_vec3_copy(v[faces[i][2]], triangle.v[2]);
        triangle.material = 0;
        ARRLIST_CutterTriangle_add(mesh, triangle);
    }
}

static CutterPlane CutterMakePlane(const Facet* facet, BOOL pavilion, size_t index) {
    float theta = IndexToRadians(index);
    float angle = facet->angle * (PI / 180.0f);
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

static void CutterAddUniqueIndex(size_t* indices, size_t* count, size_t max, size_t index, size_t wheel) {
    index %= wheel;
    for (size_t i = 0; i < *count; i++) {
        if (indices[i] == index) return;
    }
    if (*count < max) indices[(*count)++] = index;
}

static size_t CutterFacetIndices(const Facet* facet, size_t out[512]) {
    size_t wheel = g_index_values[(size_t)g_index_type];
    size_t count = 0;
    size_t base = facet->index % wheel;
    switch (facet->symmetry) {
        case NO_SYMMETRY:
            CutterAddUniqueIndex(out, &count, 512, base, wheel);
            break;
        case RADIAL_SYMMETRY: {
            if (base == 0) {
                CutterAddUniqueIndex(out, &count, 512, 0, wheel);
                break;
            }
            for (size_t i = base; i <= wheel; i += base) {
                CutterAddUniqueIndex(out, &count, 512, i, wheel);
            }
            break;
        }
        case DUAL_SYMMETRY:
            CutterAddUniqueIndex(out, &count, 512, base, wheel);
            CutterAddUniqueIndex(out, &count, 512, (wheel - base) % wheel, wheel);
            break;
        case QUAD_SYMMETRY: {
            size_t half = wheel / 2;
            CutterAddUniqueIndex(out, &count, 512, base, wheel);
            CutterAddUniqueIndex(out, &count, 512, (wheel - base) % wheel, wheel);
            CutterAddUniqueIndex(out, &count, 512, (base + half) % wheel, wheel);
            CutterAddUniqueIndex(out, &count, 512, (half + wheel - base) % wheel, wheel);
            break;
        }
        default:
            break;
    }
    return count;
}

static void CutterApplyFacets(ARRLIST_CutterTriangle* mesh, ARRLIST_Facet* facets, BOOL pavilion) {
    for (size_t i = 0; i < facets->size; i++) {
        Facet* facet = &facets->data[i];
        size_t indices[512];
        size_t index_count = CutterFacetIndices(facet, indices);
        for (size_t j = 0; j < index_count; j++) {
            CutterPlane plane = CutterMakePlane(facet, pavilion, indices[j]);
            CutterClipMesh(mesh, plane);
        }
    }
}

static void CutterSubmitMesh(ARRLIST_CutterTriangle* mesh) {
    ClearTriangles();
    ClearVertices();
    ClearNormals();
    ClearMeshDescriptors();
    size_t vertex_base = NumVertices();
    size_t triangle_base = NumTriangles();
    for (size_t i = 0; i < mesh->size; i++) {
        CutterTriangle* triangle = &mesh->data[i];
        SubmitVertex(triangle->v[0]);
        SubmitVertex(triangle->v[1]);
        SubmitVertex(triangle->v[2]);
        VertexID base = vertex_base + (VertexID)(i * 3);
        SubmitTriangle((Triangle){
            base + 0,
            base + 1,
            base + 2,
            (uint32_t)-1,
            (uint32_t)-1,
            (uint32_t)-1,
            triangle->material
        });
    }

    if (mesh->size > 0) {
        vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
        vec3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (size_t i = 0; i < mesh->size; i++) {
            for (size_t j = 0; j < 3; j++) {
                glm_vec3_minv(min, mesh->data[i].v[j], min);
                glm_vec3_maxv(max, mesh->data[i].v[j], max);
            }
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
                NumVertices() - 1,
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
    ARRLIST_CutterTriangle mesh = { 0 };
    CutterCreateCube(&mesh);
    CutterApplyFacets(&mesh, &g_pavillion_facets, TRUE);
    CutterApplyFacets(&mesh, &g_crown_facets, FALSE);
    CutterSubmitMesh(&mesh);
    ARRLIST_CutterTriangle_clear(&mesh);
}

static void DrawCutterPanel(float width, float height) {
    g_edited = FALSE;
    float boxwidth = width - 20 - 160;
    float boxstride = width - boxwidth - 20;
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
    UIDrawText("Stats");
    UIMoveCursor(boxstride + 5, -LINE_HEIGHT + 5);
    DrawRectangle(UIGetCursor().x - 5, UIGetCursor().y - 2, boxwidth, LINE_HEIGHT * 4 + 4, (Color){ 255, 255, 255, 120 });
    UIDrawText("L/W/H Ratio: ?/?/?");
    UIMoveCursor(boxstride + 5, 0);
    UIDrawText("Preservation: 54.78%%");
    UIMoveCursor(boxstride + 5, 0);
    UIDrawText("Facets: 18");
    UIMoveCursor(boxstride + 5, 0);
    UIDrawText("Critical RI: 1.56");
    UIMoveCursor(0, 2);
    UIDivider(width - 20);
    UIDropdownSection("Pavillion", width - 20, DrawPavillionSection, NULL);
    UIDropdownSection("Crown", width - 20, DrawCrownSection, NULL);
    UIDivider(width - 20);
    UIDrawText("Import");
    UIDrawText("Export");
    UIDrawText("Save As...");
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
