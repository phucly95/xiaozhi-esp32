#include "font_awesome.h"
#include <string.h>
#include <stddef.h>

typedef struct {
    const char* name;
    const char* icon;
} font_awesome_map_t;

static const font_awesome_map_t font_map[] = {
    {"neutral", FONT_AWESOME_NEUTRAL},
    {"happy",   "\xEF\x84\x98"}, // 
    {"sad",     "\xEF\x84\x99"}, // 
    {"angry",   "\xEF\x96\x96"}, // 
    {"fear",    "\xEF\x96\xB4"}, // 
    {"surprise","\xEF\x9\x82"}, //  (Approx)
    // Add more mappings as needed
    {NULL, NULL}
};

const char* font_awesome_get_utf8(const char* name) {
    if (name == NULL) {
        return NULL;
    }
    
    for (int i = 0; font_map[i].name != NULL; i++) {
        if (strcmp(font_map[i].name, name) == 0) {
            return font_map[i].icon;
        }
    }
    return NULL;
}
