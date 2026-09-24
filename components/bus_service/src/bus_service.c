#include "bus_service.h"
#include "bus_routes.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

typedef enum { REQ_ROUTES, REQ_STOPS, REQ_ETA } request_kind_t;
typedef struct { request_kind_t kind; uint32_t id; bus_route_variant_t route; char stop_id[20]; char route_name[5]; uint8_t op, service_type; } bus_request_t;
static QueueHandle_t s_queue; static QueueHandle_t s_events; static TaskHandle_t s_task; static volatile bool s_cancel, s_suspend; static uint32_t s_next_id;
static bus_listener_t s_listener; static void *s_user;
static bool route_prefix_matches(const bus_route_name_t *entry, const char *prefix, size_t length) {
    if (!entry || !prefix || length > 4) return false;
    for (size_t i = 0; i < length; ++i) {
        if (entry->name[i] == ' ' || entry->name[i] != prefix[i]) return false;
    }
    return true;
}
static void route_text(const char *name, char *out, size_t size) {
    size_t i = 0; while (i < 4 && name[i] != ' ') ++i;
    if (i >= size) i = size - 1;
    memcpy(out, name, i);
    out[i] = 0;
}
static void route_places(const char *route, char *origin, size_t origin_size, char *destination, size_t destination_size) {
    if (strcmp(route, "2A") == 0) { strlcpy(origin, "Lok Fu", origin_size); strlcpy(destination, "Tsim Sha Tsui", destination_size); return; }
    if (strcmp(route, "2B") == 0) { strlcpy(origin, "Tsz Wan Shan", origin_size); strlcpy(destination, "Jordan", destination_size); return; }
    if (strcmp(route, "68X") == 0) { strlcpy(origin, "Kowloon", origin_size); strlcpy(destination, "Yuen Long", destination_size); return; }
    strlcpy(origin, "Kowloon", origin_size); strlcpy(destination, "Hong Kong", destination_size);
}
static void emit(const bus_event_t *e) {
    if (!s_events || xQueueSend(s_events, e, 0) == pdTRUE) return;
    if (e->type == BUS_EVT_ROUTE_VARIANTS) heap_caps_free(e->data.route_variants.variants);
    if (e->type == BUS_EVT_STOPS_LIST) heap_caps_free(e->data.stops_list.stops);
}
static void worker(void *arg) {
    (void)arg; bus_request_t r;
    for (;;) { if (xQueueReceive(s_queue,&r,portMAX_DELAY)!=pdTRUE) continue; if(s_cancel) {s_cancel=false; continue;}
        bus_event_t e={.request_id=r.id,.status=ESP_OK};
        if(r.kind==REQ_ROUTES) {
            bus_route_variant_t *v=heap_caps_calloc(8,sizeof(*v),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); if(!v) continue;
            uint8_t count=0; size_t prefix_len=strlen(r.route_name);
            for(uint16_t i=0;i<bus_route_index_count && count<8;i++) if(route_prefix_matches(&bus_route_index[i],r.route_name,prefix_len)) {
                char route[5]; route_text(bus_route_index[i].name,route,sizeof(route));
                strlcpy(v[count].route,route,sizeof(v[count].route)); v[count].op=BUS_OP_KMB; v[count].bound='O'; v[count].service_type=1;
                route_places(route,v[count].orig_en,sizeof(v[count].orig_en),v[count].dest_en,sizeof(v[count].dest_en)); ++count;
            }
            e.type=BUS_EVT_ROUTE_VARIANTS; e.data.route_variants.variants=v; e.data.route_variants.count=count;
        } else if(r.kind==REQ_STOPS) {
            const uint16_t count=12; bus_stop_t *st=heap_caps_calloc(count,sizeof(*st),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); if(!st) continue;
            const char *names[]={"Tsim Sha Tsui East","Kowloon Park Drive","Tsuen Wan Station","Castle Peak Road","Tai Lam Tunnel","Yuen Long Station"};
            for(uint16_t i=0;i<count;i++){st[i].seq=i+1; snprintf(st[i].stop_id,sizeof(st[i].stop_id),"%s-%u",r.route.route,(unsigned)(i+1)); snprintf(st[i].name_en,sizeof(st[i].name_en),"%s",names[i%6]); st[i].resolved=true;}
            e.type=BUS_EVT_STOPS_LIST; e.data.stops_list.stops=st; e.data.stops_list.count=count;
        } else {
            e.type=BUS_EVT_ETA; strlcpy(e.data.eta.stop_id,r.stop_id,sizeof(e.data.eta.stop_id)); e.data.eta.result.count=3; e.data.eta.result.fetched_at=(uint32_t)time(NULL);
            for(int i=0;i<3;i++){e.data.eta.result.entries[i].minutes_left=i==0?3:(i==1?11:24); e.data.eta.result.entries[i].eta_epoch=e.data.eta.result.fetched_at+(uint32_t)e.data.eta.result.entries[i].minutes_left*60; strlcpy(e.data.eta.result.entries[i].remark_en,i==1?"Scheduled":"",sizeof(e.data.eta.result.entries[i].remark_en));}
        }
        emit(&e);
    }
}
void bus_service_init(void){ if(s_task) return; s_queue=xQueueCreate(4,sizeof(bus_request_t)); s_events=xQueueCreate(4,sizeof(bus_event_t)); if(s_queue&&s_events) xTaskCreatePinnedToCore(worker,"bus_worker",6144,NULL,2,&s_task,0); }
void bus_service_set_listener(bus_listener_t cb, void *u){s_listener=cb;s_user=u;}
bool bus_service_poll(bus_event_t *event){return event&&s_events&&xQueueReceive(s_events,event,0)==pdTRUE;}
void bus_service_suspend(bool v){s_suspend=v;}
void bus_service_cancel_all(void){s_cancel=true; xQueueReset(s_queue);}
static uint32_t submit(bus_request_t *r){if(!s_queue||s_suspend||!r)return 0;r->id=++s_next_id;return xQueueSend(s_queue,r,0)==pdTRUE?r->id:0;}
uint32_t bus_service_request_routes(const char *route){bus_service_init();bus_request_t r={.kind=REQ_ROUTES};strlcpy(r.route_name,route?route:"",sizeof(r.route_name));return submit(&r);}
uint32_t bus_service_request_stops(const bus_route_variant_t *route){bus_service_init();bus_request_t r={.kind=REQ_STOPS};if(route)r.route=*route;return submit(&r);}
uint32_t bus_service_request_eta(const char *stop,const char *route,uint8_t op,uint8_t st){bus_service_init();bus_request_t r={.kind=REQ_ETA,.op=op,.service_type=st};strlcpy(r.stop_id,stop?stop:"",sizeof(r.stop_id));strlcpy(r.route_name,route?route:"",sizeof(r.route_name));return submit(&r);}
void bus_service_free(void *p){if(p)heap_caps_free(p);}
