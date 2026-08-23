#include "cutter.h"
#include "core/editor.h"
#include <core/entrypoint.h>
#include <data/colors.h>
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
static IndexWheelType g_index_type = INDEX_96;
static BOOL g_override_geometry = TRUE;
static ARRLIST_Facet g_pavillion_facets = { 0 };
static ARRLIST_Facet g_crown_facets = { 0 };

static size_t DropdownSelectIndexWheel(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) {
        return (size_t)g_index_type;
    } else {
        g_index_type = (IndexWheelType)index;
    }
    return index;
}

static void DrawPavillionSection(size_t width, void* param) {
    char nbuffer[64] = { 0 };
    for (size_t i = 0; i < g_pavillion_facets.size; i++) {
        sprintf(nbuffer, "%d.", (int)i + 1);
        UIMoveCursor(20, 0);
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
        UIDragFloat(&(g_pavillion_facets.data[i].angle), 0, 90.0f, 0.01f, 50);
    }
    if (UIButton("+", 0)) {
        ARRLIST_Facet_add(&g_pavillion_facets, (Facet){ "", 45.0f, 0.0f, 0, RADIAL_SYMMETRY });
    }
}

static void DrawCrownSection(size_t width, void* param) {
    UIDrawText("yello");
}

static void InitializeCutterPanel() {
}

static void UpdateCutterPanel(float width, float height) {
}

static void DrawCutterPanel(float width, float height) {
    float boxwidth = width - 20 - 160;
    float boxstride = width - boxwidth - 20;
    UITextInput("Name", g_cut_name, MAX_BLUEPRINT_NAME_SIZE, width - 20, FALSE);
    UIDivider(width - 20);
    UIDrawText("Index Wheel");
    UIMoveCursor(boxstride, -LINE_HEIGHT);
    UIDropdownMenu(boxwidth, 5, g_index_type_names, DropdownSelectIndexWheel, NULL);
    UIDrawText("Override Geometry");
    UIMoveCursor(boxstride - 2, -LINE_HEIGHT);
    UICheckbox(&g_override_geometry);
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
