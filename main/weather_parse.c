#include "weather.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "cJSON.h"

static cJSON *item(cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static float number(cJSON *v) { return cJSON_IsNumber(v) && isfinite(v->valuedouble) ? (float)v->valuedouble : NAN; }
static float num(cJSON *o, const char *key) { return number(item(o, key)); }
static float at(cJSON *o, const char *key, int n) { return number(cJSON_GetArrayItem(item(o,key),n)); }
static const char *str(cJSON *o, const char *key) { cJSON *v=item(o,key);return cJSON_IsString(v)?v->valuestring:""; }
static const char *str_at(cJSON *o, const char *key, int n) {cJSON *v=cJSON_GetArrayItem(item(o,key),n);return cJSON_IsString(v)?v->valuestring:"";}
static void text(char *dst, size_t size, const char *src)
{
    size_t n=strlen(src);
    if(n>=size){n=size-1;while(n && ((unsigned char)src[n]&0xc0)==0x80)--n;}
    memcpy(dst,src,n);dst[n]=0;
    for(size_t i=0;i<n;i++) if((unsigned char)dst[i]<32) dst[i]=' ';
}
static time_t epoch(cJSON *v, double divisor)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) return 0;
    double seconds=v->valuedouble/divisor;
    return seconds>=946684800 && seconds<=4102444800 ? (time_t)seconds : 0;
}
static time_t epoch_at(cJSON *o,const char *key,int i){return epoch(cJSON_GetArrayItem(item(o,key),i),1);}
static float range(float v,float lo,float hi){return isfinite(v)&&v>=lo&&v<=hi?v:NAN;}
static float from_f(float f){return (f-32)/1.8f;}

void weather_reset(weather_snapshot_t *w)
{
    memset(w,0,sizeof(*w));
    w->latitude=w->longitude=NAN;
    w->current=(weather_current_t){NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN};
    for(unsigned i=0;i<WEATHER_DAYS;i++) w->days[i]=(weather_day_t){.high=NAN,.low=NAN,.rain=NAN,.rain_chance=NAN,.wind=NAN,.uv=NAN,.icon=WX_UNKNOWN};
}
const char *weather_icon_name(weather_icon_t icon)
{
    static const char *names[]={"Clear","Partly cloudy","Cloudy","Fog","Rain","Snow","Thunderstorms","Windy","Unavailable"};
    return names[(unsigned)icon<=WX_UNKNOWN?icon:WX_UNKNOWN];
}
void weather_format(char *dst,size_t size,float v,const char *kind,bool metric)
{
    if(!isfinite(v)){snprintf(dst,size,"--");return;}
    if(!strcmp(kind,"temp"))snprintf(dst,size,"%.1f °%c",metric?v:v*1.8f+32,metric?'C':'F');
    else if(!strcmp(kind,"wind"))snprintf(dst,size,"%.1f %s",metric?v:v/1.609344f,metric?"km/h":"mph");
    else if(!strcmp(kind,"rain"))snprintf(dst,size,metric?"%.1f mm":"%.2f in",metric?v:v/25.4f);
    else if(!strcmp(kind,"rate"))snprintf(dst,size,metric?"%.1f mm/h":"%.2f in/h",metric?v:v/25.4f);
    else if(!strcmp(kind,"pressure"))snprintf(dst,size,metric?"%.1f hPa":"%.2f inHg",metric?v:v/33.8638867f);
    else if(!strcmp(kind,"percent"))snprintf(dst,size,"%.0f%%",v);
    else snprintf(dst,size,"%.1f%s%s",v,kind[0]?" ":"",kind);
}
static void field(weather_snapshot_t *w,const char *label,float value,const char *kind,bool metric)
{
    if(!isfinite(value)) return;
    if(w->field_count==WEATHER_MAX_FIELDS){w->truncated=true;return;}
    weather_field_t *f=&w->fields[w->field_count++];
    text(f->label,sizeof(f->label),label);weather_format(f->value,sizeof(f->value),value,kind,metric);f->unit[0]=0;
}
/* NOAA/NWS heat-index regression and standard wind-chill/dewpoint formulas.
 * Heat index is used only in warm conditions; wind chill only <=10 C and >4.8 km/h. */
static void derived(weather_current_t *c)
{
    if(isfinite(c->temp)&&isfinite(c->humidity)&&c->humidity>0&&!isfinite(c->dew)){
        float gamma=logf(c->humidity/100)+17.625f*c->temp/(243.04f+c->temp);
        c->dew=243.04f*gamma/(17.625f-gamma);
    }
    float t=c->temp*1.8f+32,r=c->humidity;
    if(!isfinite(c->heat_index)&&isfinite(t)&&isfinite(r)){
        float h=0.5f*(t+61+(t-68)*1.2f+r*0.094f);
        if((h+t)*0.5f>=80){
            h=-42.379f+2.04901523f*t+10.14333127f*r-0.22475541f*t*r-0.00683783f*t*t-0.05481717f*r*r+0.00122874f*t*t*r+0.00085282f*t*r*r-0.00000199f*t*t*r*r;
            if(r<13&&t>=80&&t<=112) h-=(13-r)/4*sqrtf((17-fabsf(t-95))/17);
            else if(r>85&&t>=80&&t<=87) h+=(r-85)/10*(87-t)/5;
        }
        c->heat_index=from_f(h);
    }
    if(!isfinite(c->wind_chill)&&isfinite(c->temp)&&isfinite(c->wind)&&c->temp<=10&&c->wind>4.8){
        float speed=powf(c->wind,0.16f);
        c->wind_chill=13.12f+0.6215f*c->temp-11.37f*speed+0.3965f*c->temp*speed;
    }
    c->feels=c->temp;
    if(c->temp>=26.7f&&isfinite(c->heat_index))c->feels=fmaxf(c->temp,c->heat_index);
    else if(c->temp<=10&&isfinite(c->wind_chill))c->feels=fminf(c->temp,c->wind_chill);
}
static bool same_mac(const char *a,const char *b)
{
    char x[13]={0},y[13]={0};unsigned i=0,j=0;
    for(;*a;a++){if(*a==':'||*a=='-')continue;if(!isxdigit((unsigned char)*a)||i==12)return false;x[i++]=(char)tolower((unsigned char)*a);}
    for(;*b;b++){if(*b==':'||*b=='-')continue;if(!isxdigit((unsigned char)*b)||j==12)return false;y[j++]=(char)tolower((unsigned char)*b);}
    return i==12&&j==12&&!strcmp(x,y);
}
bool weather_parse_station(weather_snapshot_t *w,const char *json,size_t length,bool ambient,const char *station,bool metric)
{
    if(!json||!length||length>WEATHER_MAX_PAYLOAD)return false;
    cJSON *root=cJSON_ParseWithLength(json,length),*obs=NULL,*measure=NULL,*info=NULL;
    if(ambient){
        if(!cJSON_IsArray(root))goto bad;
        cJSON *device;unsigned matches=0;
        cJSON_ArrayForEach(device,root){
            cJSON *di=item(device,"info");
            if(!strcasecmp(str(di,"name"),station)||same_mac(str(device,"macAddress"),station)){obs=item(device,"lastData");info=di;matches++;}
        }
        if(matches!=1||!cJSON_IsObject(obs))goto bad;
        measure=obs;
    }else{
        cJSON *rows=item(root,"observations");
        cJSON *row;
        cJSON_ArrayForEach(row,rows)if(!strcasecmp(str(row,"stationID"),station)){obs=row;break;}
        measure=item(obs,"metric");
        if(!cJSON_IsObject(obs)||!cJSON_IsObject(measure))goto bad;
    }
    weather_current_t c={NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN};
    c.temp=range(ambient?from_f(num(obs,"tempf")):num(measure,"temp"),-100,70);
    c.humidity=range(num(obs,"humidity"),0,100);
    c.wind=range(ambient?num(obs,"windspeedmph")*1.609344f:num(measure,"windSpeed"),0,500);
    c.gust=range(ambient?num(obs,"windgustmph")*1.609344f:num(measure,"windGust"),0,500);
    c.direction=range(num(obs,"winddir"),0,360);
    c.dew=ambient?from_f(num(obs,"dewPoint")):num(measure,"dewpt");
    if(ambient&&!isfinite(c.dew))c.dew=from_f(num(obs,"dewPointf"));
    c.pressure=range(ambient?num(obs,"baromrelin")*33.8638867f:num(measure,"pressure"),300,1200);
    c.rain_day=range(ambient?num(obs,"dailyrainin")*25.4f:num(measure,"precipTotal"),0,5000);
    c.rain_rate=range(ambient?num(obs,"rainratein")*25.4f:num(measure,"precipRate"),0,5000);
    if(ambient&&!isfinite(c.rain_rate))c.rain_rate=range(num(obs,"rainin")*25.4f,0,5000);
    c.uv=range(num(obs,ambient?"uv":"uv"),0,30);
    c.solar=range(num(obs,ambient?"solarradiation":"solarRadiation"),0,2500);
    if(!ambient){c.heat_index=range(num(measure,"heatIndex"),-100,100);c.wind_chill=range(num(measure,"windChill"),-150,70);}
    time_t observed=epoch(item(obs,ambient?"dateutc":"epoch"),ambient?1000:1);
    if(!observed||!isfinite(c.temp))goto bad;
    derived(&c);
    double lat=NAN,lon=NAN;
    if(ambient){
        cJSON *coords=item(info,"coords"),*values=item(coords,"coords");
        lat=num(values,"lat");lon=num(values,"lon");
        if(!isfinite(lat)||!isfinite(lon)){
            cJSON *geo=item(item(coords,"geo"),"coordinates");
            lon=number(cJSON_GetArrayItem(geo,0));lat=number(cJSON_GetArrayItem(geo,1));
        }
    }else{lat=num(obs,"lat");lon=num(obs,"lon");}
    w->latitude=isfinite(lat)&&lat>=-90&&lat<=90?lat:NAN;
    w->longitude=isfinite(lon)&&lon>=-180&&lon<=180?lon:NAN;
    text(w->name,sizeof(w->name),ambient?str(info,"name"):str(obs,"neighborhood"));
    if(!w->name[0])text(w->name,sizeof(w->name),station);
    w->observed_at=observed;w->current=c;w->field_count=0;w->truncated=false;
    field(w,"Outdoor temperature",c.temp,"temp",metric);field(w,"Feels like (calculated)",c.feels,"temp",metric);
    field(w,"Humidity",c.humidity,"percent",metric);field(w,"Dew point",c.dew,"temp",metric);
    field(w,"Heat index",c.heat_index,"temp",metric);field(w,"Wind chill",c.wind_chill,"temp",metric);
    field(w,"Wind speed",c.wind,"wind",metric);field(w,"Wind gust",c.gust,"wind",metric);
    field(w,"Wind direction",c.direction,"degrees",metric);field(w,"Pressure",c.pressure,"pressure",metric);
    field(w,"Rain today",c.rain_day,"rain",metric);field(w,"Rain rate",c.rain_rate,"rate",metric);
    field(w,"UV index",c.uv,"",metric);field(w,"Solar radiation",c.solar,"W/m2",metric);
    if(ambient){
        const char *rainkeys[]={"hourlyrainin","weeklyrainin","monthlyrainin","yearlyrainin","totalrainin"};
        const char *rainlabels[]={"Rain last hour","Rain this week","Rain this month","Rain this year","Rain total"};
        for(int i=0;i<5;i++)field(w,rainlabels[i],num(obs,rainkeys[i])*25.4f,"rain",metric);
        field(w,"Indoor temperature",from_f(num(obs,"tempinf")),"temp",metric);field(w,"Indoor humidity",num(obs,"humidityin"),"percent",metric);
        field(w,"Maximum daily gust",num(obs,"maxdailygust")*1.609344f,"wind",metric);
        /* Include optional station channels instead of silently losing sensors. */
        cJSON *v;
        cJSON_ArrayForEach(v,obs){
            if(!v->string||!cJSON_IsNumber(v))continue;
            const char *key=v->string;bool extra=false;
            const char *prefixes[]={"temp1","temp2","temp3","temp4","temp5","temp6","temp7","temp8","humidity1","humidity2","humidity3","humidity4","humidity5","humidity6","humidity7","humidity8","soil","pm","co2","batt","lightning","leak"};
            for(unsigned i=0;i<sizeof(prefixes)/sizeof(*prefixes);i++)if(!strncmp(key,prefixes[i],strlen(prefixes[i])))extra=true;
            if(extra){float val=number(v);const char *kind="";size_t n=strlen(key);if(!strncmp(key,"temp",4)&&n&&key[n-1]=='f'){val=from_f(val);kind="temp";}else if(!strncmp(key,"humidity",8))kind="percent";field(w,key,val,kind,metric);}
        }
    }
    w->station_available=true;w->station_status[0]=0;cJSON_Delete(root);return true;
bad:
    cJSON_Delete(root);return false;
}
static weather_icon_t wmo(float code)
{
    if(!isfinite(code)||code<0||code>99)return WX_UNKNOWN;
    int c=(int)code;if(c==0)return WX_CLEAR;if(c<=2)return WX_PARTLY;
    if(c==3)return WX_CLOUD;
    if(c<=48)return WX_FOG;
    if(c>=95)return WX_STORM;
    if((c>=71&&c<=77)||c==85||c==86)return WX_SNOW;
    return WX_RAIN;
}
static weather_icon_t twc(float code)
{
    if(!isfinite(code)||code<0||code>99)return WX_UNKNOWN;
    int c=(int)code;if(c<=4||c==37||c==38||c==39||c==45||c==47)return WX_STORM;
    if((c>=5&&c<=8)||(c>=13&&c<=18)||c==41||c==42||c==43||c==46)return WX_SNOW;
    if((c>=9&&c<=12)||c==35||c==40)return WX_RAIN;
    if(c>=19&&c<=22)return WX_FOG;
    if(c==23||c==24)return WX_WIND;
    if((c>=27&&c<=30)||c==44)return WX_PARTLY;
    if(c>=31&&c<=36)return WX_CLEAR;
    return c==26?WX_CLOUD:WX_UNKNOWN;
}
bool weather_parse_forecast(weather_snapshot_t *w,const char *json,size_t length,bool wu,time_t now)
{
    if(!json||!length||length>WEATHER_MAX_PAYLOAD)return false;
    cJSON *root=cJSON_ParseWithLength(json,length);
    if(!cJSON_IsObject(root)){cJSON_Delete(root);return false;}
    cJSON *daily=wu?root:item(root,"daily"),*part=cJSON_GetArrayItem(item(root,"daypart"),0);
    int count=cJSON_GetArraySize(item(daily,wu?"validTimeUtc":"time"));
    if(!count){cJSON_Delete(root);return false;}
    w->day_count=w->hour_count=0;
    for(int i=0;i<count&&w->day_count<WEATHER_DAYS;i++){
        time_t date=epoch_at(daily,wu?"validTimeUtc":"time",i);
        if(!date||date+86400<now)continue;
        weather_day_t d={.date=date,.high=NAN,.low=NAN,.rain=NAN,.rain_chance=NAN,.wind=NAN,.uv=NAN,.icon=WX_UNKNOWN};
        d.high=at(daily,wu?"temperatureMax":"temperature_2m_max",i);d.low=at(daily,wu?"temperatureMin":"temperature_2m_min",i);
        d.rain=at(daily,wu?"qpf":"precipitation_sum",i);
        d.sunrise=epoch_at(daily,wu?"sunriseTimeUtc":"sunrise",i);d.sunset=epoch_at(daily,wu?"sunsetTimeUtc":"sunset",i);
        if(wu){
            int j=i*2;float icon=at(part,"iconCode",j);if(!isfinite(icon))icon=at(part,"iconCode",++j);
            d.icon=twc(icon);d.rain_chance=fmaxf(at(part,"precipChance",i*2),at(part,"precipChance",i*2+1));
            d.wind=fmaxf(at(part,"windSpeed",i*2),at(part,"windSpeed",i*2+1));d.uv=fmaxf(at(part,"uvIndex",i*2),at(part,"uvIndex",i*2+1));
            text(d.narrative,sizeof(d.narrative),str_at(daily,"narrative",i));
        }else{
            d.icon=wmo(at(daily,"weather_code",i));d.rain_chance=at(daily,"precipitation_probability_max",i);
            d.wind=at(daily,"wind_speed_10m_max",i);d.uv=at(daily,"uv_index_max",i);
        }
        d.high=range(d.high,-100,70);d.low=range(d.low,-100,70);d.rain_chance=range(d.rain_chance,0,100);
        if(!isfinite(d.high)&&!isfinite(d.low))continue;
        w->days[w->day_count++]=d;
    }
    if(!wu){
        cJSON *hourly=item(root,"hourly");int n=cJSON_GetArraySize(item(hourly,"time"));
        for(int i=0;i<n&&w->hour_count<WEATHER_HOURS;i++){
            time_t date=epoch_at(hourly,"time",i);if(!date||date<now-1800)continue;
            weather_hour_t h={.time=date,.temp=range(at(hourly,"temperature_2m",i),-100,70),.rain_chance=range(at(hourly,"precipitation_probability",i),0,100),.rain=at(hourly,"precipitation",i),.icon=wmo(at(hourly,"weather_code",i))};
            w->hours[w->hour_count++]=h;
        }
    }
    cJSON_Delete(root);
    if(!w->day_count)return false;
    w->forecast_available=true;w->forecast_status[0]=0;return true;
}
bool weather_encode(char *dst,size_t size,const char *value)
{
    static const char hex[]="0123456789ABCDEF";size_t n=0;
    for(const unsigned char *p=(const unsigned char *)value;*p;p++){
        bool safe=(isalnum(*p)&&*p<128)||*p=='-'||*p=='_'||*p=='.'||*p=='~';
        size_t need=safe?1:3;if(n+need>=size)return false;
        if(safe)dst[n++]=(char)*p;else{dst[n++]='%';dst[n++]=hex[*p>>4];dst[n++]=hex[*p&15];}
    }
    if(n>=size)return false;
    dst[n]=0;return true;
}
unsigned weather_retry_seconds(unsigned failures,int status,unsigned retry_after)
{
    if(status==401||status==403)return 900;
    if(status==429)return retry_after>=60?(retry_after>3600?3600:retry_after):900;
    unsigned seconds=60U << (failures>4?4:failures);
    return seconds>900?900:seconds;
}
