#include "cutter.h"
#include "core/editor.h"
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
            ARRLIST_Facet_insert(list, (Facet){ "", 45.0f, 0.0f, 1, RADIAL_SYMMETRY }, index);
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
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 80, -LINE_HEIGHT);
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
        UIMoveCursor(20 + UITextWidth(nbuffer) + 5 + llen + 50 + 10 + 80, -LINE_HEIGHT);
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

static void UpdateCutterPanel(float width, float height) {
    if (g_edited && g_override_geometry) {
        // recreate Geometry here
        g_edited = FALSE;
    }
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
