#include "json.h"
#include <ctype.h>
#include <errno.h>
static void space(Json *j,int *p) { while(*p<j->length && strchr(" \t\r\n",j->data[*p])) ++*p; }
static int hex(char c) { if(c>='0'&&c<='9') return c-'0';if(c>='a'&&c<='f') return c-'a'+10;if(c>='A'&&c<='F') return c-'A'+10;return -1; }
static int token(Json *j,JsonType type,int start) {
    if(j->count>=16384) return -1;
    if(j->count==j->capacity) { int capacity=j->capacity?j->capacity*2:128;auto *tokens=static_cast<JsonToken*>(realloc(j->tokens,(size_t)capacity*sizeof(JsonToken)));if(!tokens) return -1;j->tokens=tokens;j->capacity=capacity; }
    int result=j->count++;j->tokens[result]=JsonToken{type,start,0,0,0};return result;
}
static int value(Json *j,int *p,int depth) {
    if(depth>32 || *p>=j->length) return -1;space(j,p);if(*p>=j->length) return -1;
    char c=j->data[*p];JsonType type=c=='{'?JSON_OBJECT:c=='['?JSON_ARRAY:c=='"'?JSON_STRING:JSON_NUMBER;
    int index=token(j,type,*p);if(index<0) return -1;
    if(c=='{' || c=='[') {
        ++*p;space(j,p);char close=c=='{'?'}':']';
        if(*p<j->length && j->data[*p]==close) ++*p;
        else for(;;) {
            if(c=='{') { space(j,p);if(*p>=j->length || j->data[*p]!='"' || value(j,p,depth+1)<0) return -1;space(j,p);if(*p>=j->length || j->data[(*p)++]!=':') return -1; }
            if(value(j,p,depth+1)<0) return -1;++j->tokens[index].count;space(j,p);
            if(*p>=j->length) return -1;char next=j->data[(*p)++];if(next==close) break;if(next!=',') return -1;
        }
    } else if(c=='"') {
        ++*p;
        while(*p<j->length && j->data[*p]!='"') {
            unsigned char ch=(unsigned char)j->data[(*p)++];if(ch<32) return -1;
            if(ch=='\\') { if(*p>=j->length) return -1;char escape=j->data[(*p)++];if(escape=='u') { for(int n=0;n<4;++n) if(*p>=j->length || hex(j->data[(*p)++])<0) return -1; }else if(!strchr("\"\\/bfnrt",escape)) return -1; }
        }
        if(*p>=j->length) return -1;++*p;
    } else if(c=='t' || c=='f' || c=='n') {
        const char *literal=c=='t'?"true":c=='f'?"false":"null";size_t length=strlen(literal);if((size_t)(j->length-*p)<length || memcmp(j->data+*p,literal,length)) return -1;*p+=(int)length;j->tokens[index].type=c=='t'?JSON_TRUE:c=='f'?JSON_FALSE:JSON_NULL;
    } else {
        if(c=='-') ++*p;if(*p>=j->length) return -1;
        if(j->data[*p]=='0') ++*p;else { if(j->data[*p]<'1'||j->data[*p]>'9') return -1;while(*p<j->length && isdigit((unsigned char)j->data[*p])) ++*p; }
        if(*p<j->length && j->data[*p]=='.') { ++*p;int begin=*p;while(*p<j->length && isdigit((unsigned char)j->data[*p])) ++*p;if(begin==*p) return -1; }
        if(*p<j->length && (j->data[*p]=='e'||j->data[*p]=='E')) { ++*p;if(*p<j->length && (j->data[*p]=='+'||j->data[*p]=='-')) ++*p;int begin=*p;while(*p<j->length && isdigit((unsigned char)j->data[*p])) ++*p;if(begin==*p) return -1; }
    }
    j->tokens[index].end=*p;j->tokens[index].next=j->count;return index;
}
BOOL json_parse(Json *j,const char *data,size_t bytes) {
    Json result;Json *output=j;j=&result;if(!data || !bytes || bytes>4*1024*1024 || memchr(data,0,bytes)) return FALSE;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,(int)bytes,NULL,0)) return FALSE;
    j->data=static_cast<char*>(calloc(bytes+1,1));if(!j->data) return FALSE;memcpy(j->data,data,bytes);j->length=(int)bytes;
    int p=bytes>=3 && (BYTE)data[0]==0xef && (BYTE)data[1]==0xbb && (BYTE)data[2]==0xbf?3:0;
    if(value(j,&p,0)<0) { output->error=p;return FALSE; }space(j,&p);if(p!=j->length) { output->error=p;return FALSE; }*output=std::move(result);return TRUE;
}
BOOL json_file(Json *j,const wchar_t *path,DWORD limit) {
    lb::unique_handle file(CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file) return FALSE;
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.get(),&size)||size.QuadPart<=0||size.QuadPart>LB_MIN(limit,4*1024*1024)) return FALSE;
    auto data=std::unique_ptr<char[]>(new(std::nothrow) char[size_t(size.QuadPart)]);if(!data) return FALSE;DWORD read=0;
    return ReadFile(file.get(),data.get(),DWORD(size.QuadPart),&read,nullptr) && read==DWORD(size.QuadPart) && json_parse(j,data.get(),read);
}
void json_free(Json *j) { j->clear(); }
int json_property(const Json *j,int object,const char *name) {
    if(object<0 || object>=j->count || j->tokens[object].type!=JSON_OBJECT) return -1;
    for(int n=object+1;n<j->tokens[object].next;) { const JsonToken *key=&j->tokens[n];int next=n+1;if(key->type!=JSON_STRING || next>=j->count) return -1;if(key->end-key->start-2==(int)strlen(name) && !memcmp(j->data+key->start+1,name,strlen(name))) return next;n=j->tokens[next].next; }return -1;
}
BOOL json_string(const Json *j,int index,wchar_t *out,size_t capacity) {
    if(!out || !capacity) return FALSE;out[0]=0;
    bool complete=false;lb::scope_exit clear_failed([&]{if(!complete) out[0]=0;});
    if(index<0 || index>=j->count || j->tokens[index].type!=JSON_STRING) return FALSE;
    int p=j->tokens[index].start+1,end=j->tokens[index].end-1;size_t at=0;
    while(p<end) {
        if(j->data[p]!='\\') { int begin=p;while(p<end && j->data[p]!='\\') ++p;int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,j->data+begin,p-begin,NULL,0);if(!count || at+(size_t)count>=capacity) return FALSE;MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,j->data+begin,p-begin,out+at,count);at+=(size_t)count; }
        else { ++p;char c=j->data[p++];wchar_t ch;
            if(c=='u') { unsigned value=0;for(int n=0;n<4;++n) value=(value<<4)|(unsigned)hex(j->data[p++]);ch=(wchar_t)value;if(!ch || (ch>=0xdc00&&ch<=0xdfff)) return FALSE;
                if(ch>=0xd800&&ch<=0xdbff) { if(p+6>end || j->data[p]!='\\'||j->data[p+1]!='u') return FALSE;p+=2;unsigned low=0;for(int n=0;n<4;++n) low=(low<<4)|(unsigned)hex(j->data[p++]);if(low<0xdc00||low>0xdfff||at+2>=capacity) return FALSE;out[at++]=ch;out[at++]=(wchar_t)low;continue; }
            } else { switch(c) { case 'b':ch=L'\b';break;case 'f':ch=L'\f';break;case 'n':ch=L'\n';break;case 'r':ch=L'\r';break;case 't':ch=L'\t';break;default:ch=(wchar_t)c; } }
            if(at+1>=capacity) return FALSE;out[at++]=ch;
        }
    }out[at]=0;complete=true;return TRUE;
}
int64_t json_integer(const Json *j,int index,int64_t fallback) {
    if(index<0 || index>=j->count || j->tokens[index].type!=JSON_NUMBER) return fallback;const JsonToken *t=&j->tokens[index];if(t->end-t->start>24) return fallback;char number[30];memcpy(number,j->data+t->start,(size_t)(t->end-t->start));number[t->end-t->start]=0;char *end=NULL;errno=0;int64_t value=_strtoi64(number,&end,10);return errno || !end || *end?fallback:value;
}
