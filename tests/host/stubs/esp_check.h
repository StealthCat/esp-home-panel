#pragma once
#define ESP_RETURN_ON_ERROR(expr,tag,...) do { int err=(expr); if(err) return err; } while(0)
#define ESP_RETURN_ON_FALSE(expr,err,tag,...) do { if(!(expr)) return err; } while(0)
