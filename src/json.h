#ifndef LB_JSON_H
#define LB_JSON_H
#include "lb.h"
typedef enum { JSON_OBJECT,JSON_ARRAY,JSON_STRING,JSON_NUMBER,JSON_TRUE,JSON_FALSE,JSON_NULL } JsonType;
struct JsonToken { JsonType type;int start,end,next,count; };
struct Json {
    char *data{};JsonToken *tokens{};int length{},count{},capacity{},error{};
    Json() noexcept=default;
    ~Json() { clear(); }
    Json(Json const&)=delete;
    Json& operator=(Json const&)=delete;
    Json(Json&& other) noexcept { *this=std::move(other); }
    Json& operator=(Json&& other) noexcept {
        if(this!=&other) { clear();data=std::exchange(other.data,nullptr);tokens=std::exchange(other.tokens,nullptr);length=std::exchange(other.length,0);count=std::exchange(other.count,0);capacity=std::exchange(other.capacity,0);error=std::exchange(other.error,0); }return *this;
    }
    void clear() noexcept { std::free(data);std::free(tokens);data=nullptr;tokens=nullptr;length=count=capacity=error=0; }
};
BOOL json_parse(Json *json,const char *data,size_t bytes);
BOOL json_file(Json *json,const wchar_t *path,DWORD limit);
void json_free(Json *json);
int json_property(const Json *json,int object,const char *name);
BOOL json_string(const Json *json,int token,wchar_t *out,size_t capacity);
int64_t json_integer(const Json *json,int token,int64_t fallback);
#endif
