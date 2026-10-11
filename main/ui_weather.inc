/* Native, scalable weather graphics. All rendering stays on the LVGL task. */
static lv_obj_t *s_controls_page, *s_weather_page, *s_weather_status, *s_forecast_status;
static lv_obj_t *s_weather_heading, *s_weather_summary, *s_weather_fields[WEATHER_MAX_FIELDS];
static lv_obj_t *s_page_indicator, *s_header_hit, *s_wake_shield;
static lv_obj_t *s_weather_sections[3], *s_weather_tabs[3], *s_weather_modal;
static lv_obj_t *s_feels, *s_dew, *s_humidity, *s_wind, *s_gust, *s_wind_dir;
static lv_obj_t *s_pressure, *s_rain_day, *s_rain_rate, *s_uv, *s_solar, *s_sun, *s_risk, *s_tomorrow;
static lv_obj_t *s_chart_caption, *s_chart_times[4];
static lv_obj_t *s_hero_icon, *s_compass, *s_hour_chart, *s_forecast_cards[WEATHER_DAYS];
static weather_snapshot_t *s_weather_view;
static uint32_t s_weather_revision=UINT32_MAX;
static bool s_show_weather;
static unsigned s_weather_section;
static display_policy_t s_display_policy;
static int s_last_backlight=-1;

static lv_obj_t *weather_text(lv_obj_t *parent,const char *value,int x,int y,int width,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *label=make_label(parent,value,x,y,font,color);
    lv_obj_set_width(label,width);lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);return label;
}
static lv_obj_t *weather_container(lv_obj_t *parent,int x,int y,int width,int height)
{
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
static void set_page(bool weather)
{
    s_show_weather=weather&&settings_get()->weather_enabled;
    if(s_show_weather){lv_obj_add_flag(s_controls_page,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(s_weather_page,LV_OBJ_FLAG_HIDDEN);}
    else{lv_obj_remove_flag(s_controls_page,LV_OBJ_FLAG_HIDDEN);if(s_weather_page)lv_obj_add_flag(s_weather_page,LV_OBJ_FLAG_HIDDEN);}
    if(s_page_indicator)lv_label_set_text(s_page_indicator,s_show_weather?"<  Weather  2/2  >":"<  Controls  1/2  >");
}
static void header_navigation_cb(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_GESTURE){
        lv_indev_t *input=lv_indev_active();lv_dir_t dir=input?lv_indev_get_gesture_dir(input):LV_DIR_NONE;
        if(dir==LV_DIR_LEFT||dir==LV_DIR_RIGHT){set_page(!s_show_weather);lv_indev_wait_release(input);}
    }else if(lv_event_get_code(e)==LV_EVENT_SHORT_CLICKED)set_page(!s_show_weather);
}
static void wake_cb(lv_event_t *e)
{
    display_policy_touch(&s_display_policy,settings_get(),esp_timer_get_time());
    if(lv_event_get_code(e)==LV_EVENT_RELEASED)lv_obj_add_flag(s_wake_shield,LV_OBJ_FLAG_HIDDEN);
}
static void wx_line(lv_layer_t *layer,int x1,int y1,int x2,int y2,int width,uint32_t color,lv_opa_t opacity)
{
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1=(lv_point_precise_t){x1,y1};d.p2=(lv_point_precise_t){x2,y2};
    d.width=width;d.color=c(color);d.opa=opacity;d.round_start=d.round_end=1;lv_draw_line(layer,&d);
}
static void wx_disc(lv_layer_t *layer,int x,int y,int width,int height,uint32_t color,lv_opa_t opacity)
{
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=LV_RADIUS_CIRCLE;d.bg_color=c(color);d.bg_opa=opacity;
    lv_area_t area={x,y,x+width-1,y+height-1};lv_draw_rect(layer,&d,&area);
}
static void wx_triangle(lv_layer_t *layer,int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color)
{
    lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);
    d.p[0]=(lv_point_precise_t){x1,y1};d.p[1]=(lv_point_precise_t){x2,y2};d.p[2]=(lv_point_precise_t){x3,y3};d.color=c(color);d.opa=LV_OPA_COVER;lv_draw_triangle(layer,&d);
}
/* Icon coordinates are on a 100 x 100 grid, scaled to the widget. Theme
 * palettes, soft halos and crisp outlines stay consistent at either size. */
static void icon_draw(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(obj,&a);lv_layer_t *layer=lv_event_get_layer(e);
    int size=lv_obj_get_width(obj);weather_icon_t icon=(weather_icon_t)(uintptr_t)lv_obj_get_user_data(obj);
#define X(v) (a.x1+(v)*size/100)
#define Y(v) (a.y1+(v)*size/100)
#define S(v) ((v)*size/100)
    if(icon==WX_UNKNOWN){wx_disc(layer,X(31),Y(31),S(38),S(38),T->muted,50);return;}
    bool sun=icon==WX_CLEAR||icon==WX_PARTLY;
    if(sun){
        int cx=icon==WX_CLEAR?50:66,cy=icon==WX_CLEAR?45:31;
        wx_disc(layer,X(cx-31),Y(cy-31),S(62),S(62),T->warm,24);
        for(int i=0;i<8;i++){float angle=(float)i*0.785398f;wx_line(layer,X(cx+(int)(cosf(angle)*25)),Y(cy+(int)(sinf(angle)*25)),X(cx+(int)(cosf(angle)*32)),Y(cy+(int)(sinf(angle)*32)),S(3)+1,T->warm,230);}
        wx_disc(layer,X(cx-18),Y(cy-18),S(36),S(36),T->warm,255);
        wx_disc(layer,X(cx-12),Y(cy-14),S(15),S(12),T->text,90);
    }
    if(icon==WX_WIND){
        for(int i=0;i<3;i++){int y=32+i*17;wx_line(layer,X(12+i*8),Y(y),X(75-i*8),Y(y),S(4)+1,T->accent,230);wx_disc(layer,X(70-i*8),Y(y-10),S(19),S(14),T->muted,180);}return;
    }
    if(icon!=WX_CLEAR){
        uint32_t cloud=icon==WX_STORM?T->muted:T->text;
        wx_disc(layer,X(10),Y(42),S(38),S(29),cloud,255);
        wx_disc(layer,X(27),Y(24),S(48),S(45),cloud,255);
        wx_disc(layer,X(58),Y(41),S(31),S(30),cloud,255);
        wx_disc(layer,X(20),Y(51),S(60),S(22),cloud,255);
        wx_disc(layer,X(34),Y(30),S(23),S(15),T->accent_2,45);
    }
    if(icon==WX_RAIN||icon==WX_STORM)for(int i=0;i<3;i++)wx_line(layer,X(31+i*19),Y(80),X(27+i*19),Y(91),S(4)+1,T->accent,255);
    if(icon==WX_STORM){wx_triangle(layer,X(55),Y(57),X(43),Y(81),X(63),Y(74),T->warm);wx_triangle(layer,X(63),Y(72),X(51),Y(96),X(52),Y(75),T->warm);}
    if(icon==WX_SNOW)for(int i=0;i<3;i++){int x=27+i*24;wx_line(layer,X(x-4),Y(83),X(x+4),Y(91),2,T->accent,255);wx_line(layer,X(x+4),Y(83),X(x-4),Y(91),2,T->accent,255);}
    if(icon==WX_FOG)for(int i=0;i<3;i++)wx_line(layer,X(16+i*6),Y(74+i*9),X(86-i*6),Y(74+i*9),S(3)+1,T->muted,210);
#undef X
#undef Y
#undef S
}
static lv_obj_t *weather_icon(lv_obj_t *parent,int x,int y,int size,weather_icon_t kind)
{
    lv_obj_t *o=weather_container(parent,x,y,size,size);lv_obj_set_user_data(o,(void *)(uintptr_t)kind);
    lv_obj_add_event_cb(o,icon_draw,LV_EVENT_DRAW_MAIN,NULL);return o;
}
static void compass_draw(lv_event_t *e)
{
    lv_obj_t *o=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_layer_t *layer=lv_event_get_layer(e);
    int cx=(a.x1+a.x2)/2,cy=(a.y1+a.y2)/2;
    wx_disc(layer,cx-43,cy-43,86,86,T->accent,22);
    for(int i=0;i<16;i++){float angle=i*0.392699f;wx_line(layer,cx+(int)(sinf(angle)*35),cy-(int)(cosf(angle)*35),cx+(int)(sinf(angle)*40),cy-(int)(cosf(angle)*40),i%4?1:2,T->muted,180);}
    float direction=s_weather_view->current.direction;
    if(isfinite(direction)){
        float angle=direction*0.017453293f;float dx=sinf(angle),dy=-cosf(angle);
        wx_triangle(layer,cx+(int)(dx*31),cy+(int)(dy*31),cx+(int)(dy*8),cy-(int)(dx*8),cx-(int)(dy*8),cy+(int)(dx*8),T->accent);
        wx_triangle(layer,cx-(int)(dx*24),cy-(int)(dy*24),cx+(int)(dy*7),cy-(int)(dx*7),cx-(int)(dy*7),cy+(int)(dx*7),T->muted);
        wx_disc(layer,cx-4,cy-4,8,8,T->text,255);
    }
}
static void chart_draw(lv_event_t *e)
{
    lv_obj_t *o=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_layer_t *layer=lv_event_get_layer(e);
    weather_snapshot_t *w=s_weather_view;int width=lv_obj_get_width(o),height=lv_obj_get_height(o);
    for(int i=0;i<3;i++)wx_line(layer,a.x1,a.y1+i*(height-1)/2,a.x2,a.y1+i*(height-1)/2,1,T->card_border,100);
    float low=INFINITY,high=-INFINITY;
    for(unsigned i=0;i<w->hour_count;i++){float v=w->hours[i].temp;if(isfinite(v)){low=fminf(low,v);high=fmaxf(high,v);}}
    if(!isfinite(low)||w->hour_count<2)return;
    if(high-low<2){high+=1;low-=1;}
    bool previous=false;int px=0,py=0;
    for(unsigned i=0;i<w->hour_count;i++){
        int x=a.x1+8+(int)i*(width-16)/(w->hour_count-1);float chance=w->hours[i].rain_chance;
        if(isfinite(chance)){int bar=(int)(chance*(height-12)/100);wx_disc(layer,x-6,a.y2-bar,12,bar+1,T->accent_2,95);}
        float temp=w->hours[i].temp;
        if(isfinite(temp)){
            int y=a.y2-6-(int)((temp-low)/(high-low)*(height-12));
            if(previous){wx_line(layer,px,py,x,y,7,T->accent,30);wx_line(layer,px,py,x,y,3,T->accent,255);}
            wx_disc(layer,x-3,y-3,6,6,T->text,255);px=x;py=y;previous=true;
        }else previous=false;
    }
}
static void weather_section(unsigned section)
{
    s_weather_section=section;
    for(unsigned i=0;i<3;i++){
        if(i==section){lv_obj_remove_flag(s_weather_sections[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_bg_color(s_weather_tabs[i],c(T->accent),0);lv_obj_set_style_text_color(s_weather_tabs[i],c(T->screen_bg),0);}
        else{lv_obj_add_flag(s_weather_sections[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_style_bg_color(s_weather_tabs[i],c(T->card_bg),0);lv_obj_set_style_text_color(s_weather_tabs[i],c(T->muted),0);}
    }
}
static void weather_tab_cb(lv_event_t *e){weather_section((unsigned)(uintptr_t)lv_event_get_user_data(e));}
static void close_weather_modal(lv_event_t *e){(void)e;if(s_weather_modal){lv_obj_delete(s_weather_modal);s_weather_modal=NULL;}}
static void show_day(lv_event_t *e)
{
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(e);if(index>=s_weather_view->day_count)return;
    close_weather_modal(NULL);weather_day_t *day=&s_weather_view->days[index];bool metric=settings_get()->weather_units;
    s_weather_modal=make_card(s_weather_page,86,118,628,334);
    lv_obj_set_style_bg_opa(s_weather_modal,LV_OPA_COVER,0);
    char date[80];struct tm tm;localtime_r(&day->date,&tm);strftime(date,sizeof(date),"%A, %B %d",&tm);
    weather_text(s_weather_modal,date,22,18,520,&lv_font_montserrat_22,T->text);
    lv_obj_t *close=lv_button_create(s_weather_modal);lv_obj_set_pos(close,564,10);lv_obj_set_size(close,48,40);
    lv_obj_set_style_bg_color(close,c(T->card_bg_2),0);lv_obj_t *label=lv_label_create(close);lv_label_set_text(label,LV_SYMBOL_CLOSE);lv_obj_center(label);lv_obj_add_event_cb(close,close_weather_modal,LV_EVENT_CLICKED,NULL);
    weather_icon(s_weather_modal,24,62,124,day->icon);
    weather_text(s_weather_modal,weather_icon_name(day->icon),169,61,420,&lv_font_montserrat_22,T->text);
    char hi[40],lo[40],line[160];weather_format(hi,sizeof(hi),day->high,"temp",metric);weather_format(lo,sizeof(lo),day->low,"temp",metric);
    snprintf(line,sizeof(line),"High %s   /   Low %s",hi,lo);weather_text(s_weather_modal,line,169,96,430,&lv_font_montserrat_18,T->warm);
    weather_format(hi,sizeof(hi),day->rain,"rain",metric);weather_format(lo,sizeof(lo),day->rain_chance,"percent",metric);
    snprintf(line,sizeof(line),"Rain %s  |  Chance %s",hi,lo);weather_text(s_weather_modal,line,169,128,430,&lv_font_montserrat_16,T->muted);
    weather_format(hi,sizeof(hi),day->wind,"wind",metric);weather_format(lo,sizeof(lo),day->uv,"",metric);
    snprintf(line,sizeof(line),"Wind %s  |  UV %s",hi,lo);weather_text(s_weather_modal,line,169,156,430,&lv_font_montserrat_16,T->muted);
    if(day->sunrise){localtime_r(&day->sunrise,&tm);strftime(hi,sizeof(hi),"%H:%M",&tm);}else snprintf(hi,sizeof(hi),"--");
    if(day->sunset){localtime_r(&day->sunset,&tm);strftime(lo,sizeof(lo),"%H:%M",&tm);}else snprintf(lo,sizeof(lo),"--");
    snprintf(line,sizeof(line),"Sunrise %s   /   Sunset %s",hi,lo);weather_text(s_weather_modal,line,22,207,580,&lv_font_montserrat_16,T->accent);
    const char *narrative=day->narrative[0]?day->narrative:"Forecast values are model guidance for the station location. Actual local conditions may differ.";
    label=weather_text(s_weather_modal,narrative,22,245,580,&lv_font_montserrat_16,T->muted);lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(label,70);
}
static void formatted(lv_obj_t *label,float value,const char *kind){char text[64];weather_format(text,sizeof(text),value,kind,settings_get()->weather_units);lv_label_set_text(label,text);}
static void update_weather_view(void)
{
    if(!s_weather_view||!weather_copy(s_weather_view))return;
    weather_snapshot_t *w=s_weather_view;int64_t now=esp_timer_get_time();time_t wall=time(NULL);
    char status[224];unsigned age=w->observed_at&&wall>w->observed_at?(unsigned)(wall-w->observed_at):0;
    bool stale=w->observed_at&&(age>900||now-w->station_received_us>(int64_t)(settings_get()->weather_poll_minutes+2)*60000000);
    const char *provider=settings_get()->weather_provider?"Ambient Weather":"Weather Underground";
    if(w->station_status[0])snprintf(status,sizeof(status),"%s | %s%s",provider,w->observed_at?"Cached - ":"",w->station_status);
    else if(w->observed_at)snprintf(status,sizeof(status),"%s | %sObserved %u min ago%s",provider,stale?"Stale - ":"",age/60,w->truncated?" | First 48 readings":"");
    else snprintf(status,sizeof(status),"%s | Waiting for station",provider);
    lv_label_set_text(s_weather_status,status);
    unsigned forecast_age=w->forecast_received_us?(unsigned)((now-w->forecast_received_us)/60000000):0;
    const char *forecast=settings_get()->weather_forecast==1?"Weather Underground":"Open-Meteo";
    if(w->forecast_status[0])snprintf(status,sizeof(status),"%s forecast | %s%s",forecast,w->day_count?"Cached - ":"",w->forecast_status);
    else if(w->forecast_received_us)snprintf(status,sizeof(status),"%s forecast | %s%u min ago",forecast,forecast_age>30?"Stale - ":"Updated ",forecast_age);
    else snprintf(status,sizeof(status),"Forecast waiting for station coordinates");
    lv_label_set_text(s_forecast_status,status);
    if(w->revision==s_weather_revision)return;
    s_weather_revision=w->revision;
    lv_label_set_text(s_weather_heading,w->name[0]?w->name:"Your weather station");
    formatted(s_weather_summary,w->current.temp,"temp");
    char value[64];weather_format(value,sizeof(value),w->current.feels,"temp",settings_get()->weather_units);snprintf(status,sizeof(status),"Feels like %s",value);lv_label_set_text(s_feels,status);
    weather_format(value,sizeof(value),w->current.dew,"temp",settings_get()->weather_units);snprintf(status,sizeof(status),"Dew point  %s",value);lv_label_set_text(s_dew,status);
    weather_format(value,sizeof(value),w->current.humidity,"percent",true);snprintf(status,sizeof(status),"Humidity   %s",value);lv_label_set_text(s_humidity,status);
    formatted(s_wind,w->current.wind,"wind");weather_format(value,sizeof(value),w->current.gust,"wind",settings_get()->weather_units);snprintf(status,sizeof(status),"Gust %s",value);lv_label_set_text(s_gust,status);
    static const char *dirs[]={"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
    if(isfinite(w->current.direction))snprintf(status,sizeof(status),"From %s  %.0f°",dirs[(int)lroundf(w->current.direction/22.5f)%16],w->current.direction);else snprintf(status,sizeof(status),"Direction unavailable");lv_label_set_text(s_wind_dir,status);lv_obj_invalidate(s_compass);
    formatted(s_pressure,w->current.pressure,"pressure");formatted(s_rain_day,w->current.rain_day,"rain");formatted(s_rain_rate,w->current.rain_rate,"rate");
    weather_format(value,sizeof(value),w->current.uv,"",true);snprintf(status,sizeof(status),"UV %s",value);lv_label_set_text(s_uv,status);
    formatted(s_solar,w->current.solar,"W/m2");formatted(s_risk,w->current.heat_index,"temp");
    weather_icon_t today=w->day_count?w->days[0].icon:WX_UNKNOWN;
    lv_obj_set_user_data(s_hero_icon,(void *)(uintptr_t)today);lv_obj_invalidate(s_hero_icon);
    char rise[20]="--",set[20]="--";struct tm tm;
    if(w->day_count&&w->days[0].sunrise){localtime_r(&w->days[0].sunrise,&tm);strftime(rise,sizeof(rise),"%H:%M",&tm);}
    if(w->day_count&&w->days[0].sunset){localtime_r(&w->days[0].sunset,&tm);strftime(set,sizeof(set),"%H:%M",&tm);}
    snprintf(status,sizeof(status),"%s / %s",rise,set);lv_label_set_text(s_sun,status);
    char lo[48],hi[48];weather_format(hi,sizeof(hi),w->day_count>1?w->days[1].high:NAN,"temp",settings_get()->weather_units);weather_format(lo,sizeof(lo),w->day_count>1?w->days[1].low:NAN,"temp",settings_get()->weather_units);
    snprintf(status,sizeof(status),"%s\nLow %s",hi,lo);lv_label_set_text(s_tomorrow,status);
    for(unsigned i=0;i<WEATHER_MAX_FIELDS;i++){
        lv_obj_t *cell=s_weather_fields[i];if(i>=w->field_count){lv_obj_add_flag(cell,LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(cell,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(lv_obj_get_child(cell,0),w->fields[i].label);
        snprintf(status,sizeof(status),"%s %s",w->fields[i].value,w->fields[i].unit);lv_label_set_text(lv_obj_get_child(cell,1),status);
    }
    for(unsigned i=0;i<WEATHER_DAYS;i++){
        lv_obj_t *card=s_forecast_cards[i];lv_obj_clean(card);if(T->magic)add_magic_card_ornaments(card,144,180);
        if(i>=w->day_count){weather_text(card,"Waiting",18,74,114,&lv_font_montserrat_16,T->muted);continue;}
        weather_day_t *day=&w->days[i];localtime_r(&day->date,&tm);strftime(value,sizeof(value),"%a %m/%d",&tm);
        weather_text(card,value,14,10,120,&lv_font_montserrat_14,T->text);weather_icon(card,40,27,68,day->icon);
        weather_format(hi,sizeof(hi),day->high,"temp",settings_get()->weather_units);weather_format(lo,sizeof(lo),day->low,"temp",settings_get()->weather_units);
        weather_text(card,hi,14,95,120,&lv_font_montserrat_22,T->text);snprintf(status,sizeof(status),"Low %s",lo);weather_text(card,status,14,124,120,&lv_font_montserrat_14,T->muted);
        weather_format(value,sizeof(value),day->rain_chance,"percent",true);snprintf(status,sizeof(status),"%s rain",value);weather_text(card,status,18,147,114,&lv_font_montserrat_14,T->accent);
        lv_obj_t *bar=weather_container(card,18,169,110,4);lv_obj_set_style_bg_color(bar,c(T->card_border),0);lv_obj_set_style_bg_opa(bar,100,0);
        if(isfinite(day->rain_chance)){lv_obj_t *fill=weather_container(bar,0,0,(int)(day->rain_chance*1.1f),4);lv_obj_set_style_bg_color(fill,c(T->accent),0);lv_obj_set_style_bg_opa(fill,255,0);}
    }
    if(w->hour_count){
        float min=INFINITY,max=-INFINITY;
        for(unsigned i=0;i<w->hour_count;i++)if(isfinite(w->hours[i].temp)){min=fminf(min,w->hours[i].temp);max=fmaxf(max,w->hours[i].temp);}
        weather_format(lo,sizeof(lo),min,"temp",settings_get()->weather_units);weather_format(hi,sizeof(hi),max,"temp",settings_get()->weather_units);
        snprintf(status,sizeof(status),"NEXT %u HOURS  |  %s to %s  |  Line: temperature / bars: rain chance",w->hour_count,lo,hi);
        lv_label_set_text(s_chart_caption,status);
        for(unsigned i=0;i<4;i++){unsigned index=i*(w->hour_count-1)/3;localtime_r(&w->hours[index].time,&tm);strftime(value,sizeof(value),"%H:%M",&tm);lv_label_set_text(s_chart_times[i],value);}
    }else{
        lv_label_set_text(s_chart_caption,settings_get()->weather_forecast==1?"DAILY OUTLOOK BELOW  |  Hourly chart requires Open-Meteo":"Hourly forecast is not available yet");
        for(unsigned i=0;i<4;i++)lv_label_set_text(s_chart_times[i],"--");
    }
    lv_obj_invalidate(s_hour_chart);
}
static void display_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const panel_settings_t *cfg = settings_get();
    int64_t now = esp_timer_get_time();
    if (!s_display_policy.asleep && lv_display_get_inactive_time(s_disp) < 300)
        display_policy_touch(&s_display_policy, cfg, now);
    presence_reading_t presence = c4001_reading();
    ambient_light_reading_t light = sensors_ambient_light();
    bool presence_valid = presence.available && now - presence.sampled_us < 3000000;
    bool light_valid = light.available && now - light.sampled_us < ((int64_t)cfg->sensor_interval_seconds * 3 + 5) * 1000000;
    uint8_t brightness = display_policy_step(&s_display_policy, cfg, now, presence_valid, presence.occupied,
        light_valid, light.lux, board_7c_has_pwm_backlight());
    if (brightness == 0) lv_obj_remove_flag(s_wake_shield, LV_OBJ_FLAG_HIDDEN);
    else if (s_last_backlight == 0) {
        lv_indev_t *input = lv_indev_get_next(NULL);
        if (!input || lv_indev_get_state(input) == LV_INDEV_STATE_RELEASED)
            lv_obj_add_flag(s_wake_shield, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_last_backlight != brightness && board_7c_set_backlight(brightness) == ESP_OK)
        s_last_backlight = brightness;
    if (cfg->weather_enabled) update_weather_view();
}

static void create_weather_page(lv_obj_t *root)
{
    if(!settings_get()->weather_enabled)return;
    s_weather_page=weather_container(root,0,0,800,480);
    s_weather_heading=weather_text(s_weather_page,"Your weather station",18,91,406,&lv_font_montserrat_22,T->text);
    lv_label_set_long_mode(s_weather_heading,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    const char *tabs[]={"Now","Outlook","Station"};
    for(unsigned i=0;i<3;i++){
        s_weather_sections[i]=weather_container(s_weather_page,0,0,800,480);
        lv_obj_t *button=lv_button_create(s_weather_page);s_weather_tabs[i]=button;
        lv_obj_set_pos(button,448+i*114,84);lv_obj_set_size(button,106,36);
        lv_obj_set_style_radius(button,18,0);lv_obj_set_style_shadow_width(button,0,0);
        lv_obj_t *label=lv_label_create(button);lv_label_set_text(label,tabs[i]);lv_obj_center(label);
        lv_obj_add_event_cb(button,weather_tab_cb,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    lv_obj_t *now=s_weather_sections[0];
    lv_obj_t *hero=make_card(now,14,130,310,207);
    lv_obj_set_style_border_color(hero,c(T->accent),0);
    weather_text(hero,"OUTDOOR TEMPERATURE",16,14,270,&lv_font_montserrat_14,T->muted);
    s_weather_summary=weather_text(hero,"--",16,42,194,&font_outfit_semibold_48,T->text);
    s_hero_icon=weather_icon(hero,207,35,86,WX_UNKNOWN);
    weather_text(hero,"Today's outlook",203,124,99,&lv_font_montserrat_14,T->muted);
    s_feels=weather_text(hero,"Feels like --",16,107,190,&lv_font_montserrat_18,T->warm);
    s_humidity=weather_text(hero,"Humidity --",16,153,270,&lv_font_montserrat_16,T->text);
    s_dew=weather_text(hero,"Dew point --",16,177,270,&lv_font_montserrat_16,T->muted);
    lv_obj_t *wind=make_card(now,334,130,216,207);
    weather_text(wind,"WIND",16,14,180,&lv_font_montserrat_14,T->muted);
    s_wind=weather_text(wind,"--",16,47,180,&lv_font_montserrat_22,T->text);
    s_gust=weather_text(wind,"Gust --",16,80,184,&lv_font_montserrat_14,T->muted);
    s_compass=weather_container(wind,114,99,88,88);lv_obj_add_event_cb(s_compass,compass_draw,LV_EVENT_DRAW_MAIN,NULL);
    weather_text(wind,"N",152,96,18,&lv_font_montserrat_14,T->warm);
    s_wind_dir=weather_text(wind,"From --",16,120,95,&lv_font_montserrat_16,T->accent);lv_label_set_long_mode(s_wind_dir,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *rain=make_card(now,560,130,226,99);
    weather_text(rain,"RAIN TODAY",16,12,190,&lv_font_montserrat_14,T->muted);
    s_rain_day=weather_text(rain,"--",16,36,192,&lv_font_montserrat_28,T->text);
    s_rain_rate=weather_text(rain,"--",16,73,192,&lv_font_montserrat_14,T->accent);
    lv_obj_t *solar=make_card(now,560,239,226,98);
    weather_text(solar,"SUN & SOLAR",16,12,190,&lv_font_montserrat_14,T->muted);
    s_uv=weather_text(solar,"UV --",16,36,190,&lv_font_montserrat_22,T->warm);
    s_solar=weather_text(solar,"--",16,69,190,&lv_font_montserrat_14,T->muted);
    const char *titles[]={"PRESSURE","HEAT INDEX","SUNRISE / SUNSET","TOMORROW"};
    lv_obj_t **values[]={&s_pressure,&s_risk,&s_sun,&s_tomorrow};
    for(unsigned i=0;i<4;i++){
        lv_obj_t *tile=make_card(now,14+i*195,349,187,98);
        weather_text(tile,titles[i],14,13,159,&lv_font_montserrat_14,T->muted);
        *values[i]=weather_text(tile,"--",14,42,160,i==3?&lv_font_montserrat_16:&lv_font_montserrat_20,i==1?T->warm:T->text);
        if(i==3){lv_label_set_long_mode(*values[i],LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(*values[i],50);}
    }
    lv_obj_t *outlook=s_weather_sections[1];
    lv_obj_t *chart_card=make_card(outlook,14,130,772,124);
    s_chart_caption=weather_text(chart_card,"HOURLY OUTLOOK",14,9,742,&lv_font_montserrat_14,T->muted);
    s_hour_chart=weather_container(chart_card,18,37,732,54);lv_obj_add_event_cb(s_hour_chart,chart_draw,LV_EVENT_DRAW_MAIN,NULL);
    for(unsigned i=0;i<4;i++)s_chart_times[i]=weather_text(chart_card,"--",20+i*229,98,62,&lv_font_montserrat_14,T->muted);
    for(unsigned i=0;i<WEATHER_DAYS;i++){
        s_forecast_cards[i]=make_card(outlook,14+i*156,264,148,184);
        lv_obj_add_flag(s_forecast_cards[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_forecast_cards[i],show_day,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    lv_obj_t *detail=make_card(s_weather_sections[2],14,130,772,318);
    lv_obj_add_flag(detail,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(detail,LV_DIR_VER);
    for(unsigned i=0;i<WEATHER_MAX_FIELDS;i++){
        lv_obj_t *cell=weather_container(detail,16+(i%3)*249,16+(i/3)*77,232,65);s_weather_fields[i]=cell;
        lv_obj_t *name=weather_text(cell,"",0,0,226,&lv_font_montserrat_14,T->muted);lv_label_set_long_mode(name,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_t *value=weather_text(cell,"",0,25,226,&lv_font_montserrat_22,T->text);lv_label_set_long_mode(value,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_add_flag(cell,LV_OBJ_FLAG_HIDDEN);
    }
    s_weather_status=weather_text(s_weather_page,"Waiting for station",18,449,766,&lv_font_montserrat_14,T->muted);
    lv_label_set_long_mode(s_weather_status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    /* Source attribution persists on every weather view. */
    s_forecast_status=weather_text(s_weather_page,"Forecast waiting",18,464,766,&lv_font_montserrat_14,T->muted);
    lv_label_set_long_mode(s_forecast_status,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s_page_indicator=weather_text(root,"<  Controls  1/2  >",82,62,260,&lv_font_montserrat_14,T->muted);
    s_header_hit=weather_container(root,0,0,800,77);lv_obj_add_flag(s_header_hit,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_header_hit,header_navigation_cb,LV_EVENT_GESTURE,NULL);
    lv_obj_add_event_cb(s_header_hit,header_navigation_cb,LV_EVENT_SHORT_CLICKED,NULL);
    weather_section(0);set_page(false);
}
static void create_display_behavior(void)
{
    s_wake_shield = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake_shield);
    lv_obj_set_size(s_wake_shield, 800, 480);
    lv_obj_remove_flag(s_wake_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wake_shield, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_wake_shield, wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_wake_shield, wake_cb, LV_EVENT_RELEASED, NULL);
    s_display_policy.brightness = settings_get()->backlight_percent;
    lv_timer_create(display_timer_cb, 250, NULL);
}
