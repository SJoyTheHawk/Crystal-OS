#include "bus_normalize.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
void bus_normalize_stop_id(char *s){if(!s)return;for(;*s;s++)*s=(char)toupper((unsigned char)*s);}
char bus_normalize_direction(const char *s,bus_operator_t op){if(!s||!*s)return 'O';if(op==BUS_OP_KMB)return s[0];if(strcasecmp(s,"inbound")==0)return 'I';return 'O';}
uint8_t bus_normalize_service_type(const char *s,bus_operator_t op){if(op!=BUS_OP_KMB||!s||!*s)return 1;int n=atoi(s);return (n>0&&n<256)?(uint8_t)n:1;}
