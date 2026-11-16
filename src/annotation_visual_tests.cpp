#include "annotation.hpp"

namespace lb::capture {
bool visual_parity_tests(){
    auto pixel=[](HBITMAP bitmap,int x,int y)->uint32_t{BITMAP data{};if(!bitmap||!GetObjectW(bitmap,sizeof data,&data)||!data.bmBits||x<0||y<0||x>=data.bmWidth||y>=data.bmHeight)return 0;return static_cast<uint32_t const*>(data.bmBits)[size_t(y)*data.bmWidth+x];};
    auto alpha=[&](HBITMAP bitmap,int x,int y){return pixel(bitmap,x,y)>>24;};
    Annotation shape;shape.tool=Tool::rectangle;shape.points={{20,20},{100,100}};shape.style.width=4;shape.style.radius=12;shape.style.filled=true;shape.style.color=RGB(255,0,0);
    auto box=overlay(220,160,{shape});if(!box||alpha(box.get(),50,50)!=80||alpha(box.get(),50,20)<250||alpha(box.get(),20,20)||alpha(box.get(),8,8))return false;
    shape.tool=Tool::ellipse;auto ellipse=overlay(220,160,{shape});if(!ellipse||alpha(ellipse.get(),60,60)!=80||alpha(ellipse.get(),60,20)<250||alpha(ellipse.get(),20,20))return false;
    Annotation marker;marker.tool=Tool::highlight;marker.style.width=4;marker.style.color=RGB(255,104,104);marker.points={{20,60},{180,60}};auto highlighter=overlay(220,160,{marker});if(!highlighter||alpha(highlighter.get(),100,60)!=100||alpha(highlighter.get(),100,53))return false;
    Annotation arrow;arrow.tool=Tool::arrow;arrow.style.width=4;arrow.points={{20,60},{100,60}};auto open=overlay(220,120,{arrow});if(!open||alpha(open.get(),86,64)||alpha(open.get(),100,60)<200||alpha(open.get(),81,48)<100)return false;
    arrow.style.width=1;arrow.points={{20,60},{22,60}};auto short_arrow=overlay(80,100,{arrow});if(!short_arrow||alpha(short_arrow.get(),15,57)||alpha(short_arrow.get(),22,60)==0)return false;
    Annotation number;number.tool=Tool::number;number.points={{90,70}};number.style.font_size=40;number.style.number=1234;number.style.color=RGB(255,255,255);auto bounds=number.bounds();if(bounds.right-bounds.left<67||bounds.bottom-bounds.top!=40)return false;
    auto badge=overlay(180,140,{number});if(!badge||alpha(badge.get(),58,70)<200||alpha(badge.get(),90,48))return false;int dark=0;for(int y=50;y<90;++y)for(int x=60;x<120;++x){auto c=pixel(badge.get(),x,y);if((c>>24)>200&&((c>>16)&255)<90&&((c>>8)&255)<90&&(c&255)<90)++dark;}if(dark<20)return false;
    number.style.color=RGB(21,32,40);auto inverse=overlay(180,140,{number});int white=0;for(int y=50;y<90;++y)for(int x=60;x<120;++x){auto c=pixel(inverse.get(),x,y);if((c>>24)>200&&((c>>16)&255)>200&&((c>>8)&255)>200&&(c&255)>200)++white;}return inverse&&white>20;
}
}
