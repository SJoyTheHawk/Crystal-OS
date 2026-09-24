#include "bus_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
static void path(const char *name,char *out,size_t n){snprintf(out,n,"/spiffs/bus_%s",name?name:"cache");}
esp_err_t bus_cache_init(void){return ESP_OK;}
bool bus_cache_is_valid(const char *name,unsigned max_age){(void)name;(void)max_age;return false;}
esp_err_t bus_cache_save(const char *name,const char *data,size_t len){if(!name||!data)return ESP_ERR_INVALID_ARG;char p[96];path(name,p,sizeof(p));FILE *f=fopen(p,"wb");if(!f)return ESP_FAIL;size_t n=fwrite(data,1,len,f);fclose(f);return n==len?ESP_OK:ESP_FAIL;}
esp_err_t bus_cache_load(const char *name,char **data,size_t *len){if(!name||!data||!len)return ESP_ERR_INVALID_ARG;char p[96];path(name,p,sizeof(p));FILE *f=fopen(p,"rb");if(!f)return ESP_ERR_NOT_FOUND;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0){fclose(f);return ESP_FAIL;}char *b=heap_caps_malloc((size_t)n+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!b){fclose(f);return ESP_ERR_NO_MEM;}size_t got=fread(b,1,(size_t)n,f);fclose(f);b[got]=0;*data=b;*len=got;return ESP_OK;}
void bus_cache_free(char *data){if(data)heap_caps_free(data);}
esp_err_t bus_cache_clear_all(void){remove("/spiffs/bus_routes.json");remove("/spiffs/bus_stops.json");return ESP_OK;}
