#include "bus_routes.h"
#include <ctype.h>
#include <string.h>
const bus_route_name_t bus_route_index[] = {
    {{'1',' ',' ',' '},3}, {{'2',' ',' ',' '},3}, {{'2','A',' ',' '},1}, {{'2','B',' ',' '},1}, {{'2','X',' ',' '},1}, {{'3',' ',' ',' '},3},
    {{'6','8','A',' '},1}, {{'6','8','X',' '},1}, {{'9','6','0',' '},3},
    {{'2','6','4','M'},1}, {{'B','3','X',' '},1}, {{'E','2','1',' '},2},
    {{'N','2','6',' '},1}, {{'S','1',' ',' '},2}
};
const uint16_t bus_route_index_count = sizeof(bus_route_index) / sizeof(bus_route_index[0]);
static bool match(const bus_route_name_t *e, const char *p, size_t n) {
    for (size_t i=0;i<n && i<4;i++) if (toupper((unsigned char)e->name[i]) != toupper((unsigned char)p[i])) return false;
    return n <= 4;
}
uint32_t bus_route_next_mask(const char *prefix, size_t len) {
    uint32_t mask=0; if (!prefix || len >= 4) return mask;
    for (uint16_t i=0;i<bus_route_index_count;i++) if (match(&bus_route_index[i],prefix,len)) {
        char c=bus_route_index[i].name[len]; const char *p=strchr(BUS_ROUTE_CHARSET,c); if(p) mask |= 1u << (p-BUS_ROUTE_CHARSET);
    }
    return mask;
}
uint8_t bus_route_is_complete(const char *name, size_t len) {
    if (!name || len==0 || len>4) return 0;
    for (uint16_t i=0;i<bus_route_index_count;i++) if (match(&bus_route_index[i],name,len) && (len==4 || bus_route_index[i].name[len]==' ')) return 1;
    return 0;
}
