#include "ui/circle_fill.h"
#include "ui/row_cache.h"
#include "esp_heap_caps.h"
#include "src/draw/sw/lv_draw_sw.h"
#include <stdlib.h>

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 1
#error "Launcher background cache requires the existing swapped RGB565 format"
#endif

#define SIZE 412
#define CACHE_BYTES (16U*1024U)

typedef struct {
    chronvs_row_cache_t rows;
    void *storage;
    const bool *reference;
    lv_color_t color;
    uint32_t backdrop;
    bool recording;
    lv_area_t pending;
#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
    uint32_t hits;
#endif
} background_t;

static uint32_t backdrop_signature(lv_obj_t *object) {
    uint32_t signature=2166136261U;
    for (lv_obj_t *p=lv_obj_get_parent(object);p;p=lv_obj_get_parent(p)) {
        signature=(signature ^ lv_obj_get_style_bg_color(p,0).full)*16777619U;
        signature=(signature ^ lv_obj_get_style_bg_opa(p,0))*16777619U;
    }
    return signature;
}

static bool alone(lv_obj_t *object) {
    lv_obj_t *parent=lv_obj_get_parent(object);
    for (unsigned i=0;i<lv_obj_get_child_cnt(parent);++i) {
        lv_obj_t *other=lv_obj_get_child(parent,i);
        if (other!=object && !lv_obj_has_flag(other,LV_OBJ_FLAG_HIDDEN)) return false;
    }
    return true;
}

static void replay(const chronvs_row_cache_t *cache,lv_draw_ctx_t *ctx,const lv_area_t *clip) {
    const int stride=lv_area_get_width(ctx->buf_area);
    for (int y=clip->y1;y<=clip->y2;++y) {
        uint16_t *dest=(uint16_t *)ctx->buf+(y-ctx->buf_area->y1)*stride+clip->x1-ctx->buf_area->x1;
        unsigned start=0;
        for (unsigned i=cache->offsets[y];i<cache->offsets[y+1];++i) {
            const unsigned end=cache->runs[i]>>16;
            const uint16_t color=(uint16_t)cache->runs[i];
            int first=LV_MAX((int)start,clip->x1),last=LV_MIN((int)end-1,clip->x2);
            if (first<=last) {
                const unsigned count=(unsigned)(last-first+1);
                lv_color_t fill={.full=color};
                lv_color_fill((lv_color_t *)dest,fill,count);
                dest+=count;
            }
            start=end;
            if (end>(unsigned)clip->x2) break;
        }
    }
}

static void fill_event(lv_event_t *event) {
    if (lv_event_get_target(event)!=lv_event_get_current_target(event)) return;
    background_t *state=lv_event_get_user_data(event);
    const lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_DELETE) { free(state->storage); free(state); return; }
    lv_obj_draw_part_dsc_t *part=lv_event_get_param(event);
    if (!part || part->class_p!=&lv_obj_class || part->part!=LV_PART_MAIN ||
        part->type!=LV_OBJ_DRAW_PART_RECTANGLE || !part->rect_dsc || !part->draw_area) return;
    lv_draw_ctx_t *ctx=part->draw_ctx;
    if (code==LV_EVENT_DRAW_PART_END) {
        if (state->recording) {
            const uint16_t *pixels=(const uint16_t *)ctx->buf+
                (state->pending.y1-ctx->buf_area->y1)*SIZE;
            chronvs_row_cache_record(&state->rows,state->pending.y1,
                lv_area_get_height(&state->pending),pixels,SIZE);
            state->recording=false;
        }
        return;
    }
    state->recording=false;
    if (state->reference && *state->reference) return;
    lv_draw_rect_dsc_t *dsc=part->rect_dsc;
    const lv_area_t *area=part->draw_area;
    /* The cached rim is already blended against the stable backdrop. Never
     * reuse it over the watch preview, in motion, in an alpha layer, or under
     * another mask. These cases retain the unmodified LVGL vector drawing. */
    if (!ctx || area->x1 || area->y1 || area->x2!=SIZE-1 || area->y2!=SIZE-1 ||
        ctx->draw_rect!=lv_draw_sw_rect || lv_disp_get_default()->driver->screen_transp ||
        lv_draw_mask_is_any(ctx->clip_area) || !alone(lv_event_get_target(event)) ||
        dsc->radius<SIZE/2 || dsc->bg_opa!=LV_OPA_COVER ||
        dsc->blend_mode!=LV_BLEND_MODE_NORMAL || dsc->bg_grad.dir!=LV_GRAD_DIR_NONE ||
        dsc->bg_img_src || dsc->border_width || dsc->outline_width || dsc->shadow_width) return;
    const uint32_t backdrop=backdrop_signature(lv_event_get_target(event));
    if (state->color.full!=dsc->bg_color.full || state->backdrop!=backdrop) {
        chronvs_row_cache_clear(&state->rows);
        state->color=dsc->bg_color; state->backdrop=backdrop;
    }
    lv_area_t clip;
    if (!_lv_area_intersect(&clip,ctx->clip_area,area)) return;
    if (state->rows.valid) {
        replay(&state->rows,ctx,&clip);
#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
        ++state->hits;
#endif
        /* Skip only the background: LVGL still installs its original corner
         * mask after DRAW_PART_BEGIN and removes it after drawing children. */
        dsc->bg_opa=LV_OPA_TRANSP;
    } else if (!state->rows.blocked && clip.x1==0 && clip.x2==SIZE-1 &&
               ctx->buf_area->x1==0 && ctx->buf_area->x2==SIZE-1) {
        state->pending=clip; state->recording=true;
    }
}

void chronvs_circle_fill_bind(lv_obj_t *object,const bool *reference) {
    background_t *state=heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!state) return;
    state->storage=heap_caps_calloc(1,CACHE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!state->storage) { free(state); return; }
    chronvs_row_cache_init(&state->rows,state->storage,CACHE_BYTES,SIZE,SIZE);
    state->reference=reference;
    lv_obj_add_event_cb(object,fill_event,LV_EVENT_DRAW_PART_BEGIN,state);
    lv_obj_add_event_cb(object,fill_event,LV_EVENT_DRAW_PART_END,state);
    lv_obj_add_event_cb(object,fill_event,LV_EVENT_DELETE,state);
}

#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
bool chronvs_circle_fill_info(lv_obj_t *object,uint32_t *bytes,uint32_t *hits) {
    background_t *state=lv_obj_get_event_user_data(object,fill_event);
    *bytes=state ? (((SIZE+1)*sizeof(uint16_t)+3)&~3U)+state->rows.count*sizeof(uint32_t) : 0;
    *hits=state ? state->hits : 0;
    return state && state->rows.valid;
}
#endif
