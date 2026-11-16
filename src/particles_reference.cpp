#include "particles.hpp"
#include "json.h"
namespace lb::particles {
bool reference_tests() {
    auto resource=FindResourceW(g_app.instance,MAKEINTRESOURCEW(101),RT_RCDATA);if(!resource) { log_line(L"particle reference resource missing");return false; }auto data=static_cast<char const*>(LockResource(LoadResource(g_app.instance,resource)));Json json;if(!data||!json_parse(&json,data,SizeofResource(g_app.instance,resource))) { log_line(L"particle reference JSON failed bytes=%lu offset=%d",SizeofResource(g_app.instance,resource),json.error);return false; }
    try {
        int styles=json_property(&json,0,"styles");if(styles<0||json.tokens[styles].count!=3) return false;int style=0,frames=0;
        for(int group=styles+1;group<json.tokens[styles].next;group=json.tokens[group].next,++style) {
            Model model;model.style=style;int reference=json_property(&json,group,"frames");if(reference<0||json.tokens[reference].count!=144) return false;
            for(int frame_token=reference+1;frame_token<json.tokens[reference].next;frame_token=json.tokens[frame_token].next) {
                wchar_t text[20000];if(!json_string(&json,frame_token,text,20000)) return false;std::string bytes;for(auto p=text;*p;++p) { if(*p>127) return false;bytes+=char(*p); }Json frame;if(!json_parse(&frame,bytes.data(),bytes.size())) return false;
                auto number=[&](int at) { if(at<0||at>=frame.count||frame.tokens[at].type!=JSON_NUMBER) throw std::runtime_error("Invalid particle reference");return strtod(frame.data+frame.tokens[at].start,nullptr); };
                double dt=number(json_property(&frame,0,"dt"));int point=json_property(&frame,0,"point");model.advance(dt);model.move({LONG(number(point+1)),LONG(number(point+2))},dt);
                int expected=json_property(&frame,0,"particles");if(expected<0||frame.tokens[expected].count!=int(model.items.size())) { log_line(L"particle reference count mismatch style=%d frame=%d",style,frames);return false; }
                size_t n=0;for(int row=expected+1;row<frame.tokens[expected].next;row=frame.tokens[row].next,++n) { if(frame.tokens[row].count!=6) return false;auto const& actual=model.items[n];double values[]={actual.x,actual.y,actual.vx,actual.vy,actual.age,actual.phase};for(int column=0;column<6;++column) if(std::abs(values[column]-number(row+column+1))>1e-6) { log_line(L"particle reference value mismatch style=%d frame=%d column=%d",style,frames,column);return false; } }++frames;
            }
        }return frames==432;
    }catch(std::exception const& error) { log_line(L"particle reference exception %hs",error.what());return false; }catch(...) { return false; }
}
}
