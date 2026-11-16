#pragma once
#include <algorithm>
#include <windows.h>
namespace lb::organizer {
// Explorer's work area can deliberately include an auto-hidden/styled taskbar.
// The desktop shelf still reserves its physical edge, without changing rcWork
// for ordinary application windows. Repeated reservations are idempotent.
inline RECT reserve_taskbar(RECT work,RECT monitor,RECT bar){
    LONG mw=monitor.right-monitor.left,mh=monitor.bottom-monitor.top;
    LONG bw=bar.right-bar.left,bh=bar.bottom-bar.top;
    LONG overlap_x=std::min(bar.right,monitor.right)-std::max(bar.left,monitor.left);
    LONG overlap_y=std::min(bar.bottom,monitor.bottom)-std::max(bar.top,monitor.top);
    if(mw<=0||mh<=0||bw<=0||bh<=0)return work;
    if(overlap_x>=mw/2&&bh<=mh/4){
        if(bar.top<=monitor.top+2&&bar.bottom>=monitor.top-2)work.top=std::max(work.top,monitor.top+bh);
        else if(bar.bottom>=monitor.bottom-2&&bar.top<=monitor.bottom+2)work.bottom=std::min(work.bottom,monitor.bottom-bh);
    }else if(overlap_y>=mh/2&&bw<=mw/4){
        if(bar.left<=monitor.left+2&&bar.right>=monitor.left-2)work.left=std::max(work.left,monitor.left+bw);
        else if(bar.right>=monitor.right-2&&bar.left<=monitor.right+2)work.right=std::min(work.right,monitor.right-bw);
    }
    return work;
}
inline RECT shelf_bounds(RECT work,int width,int height,int alignment,int gap,bool custom=false,int x=0,int y=0){
    width=std::clamp(width,1,std::max(1,int(work.right-work.left)));
    height=std::clamp(height,1,std::max(1,int(work.bottom-work.top)));
    if(!custom){x=work.left+(work.right-work.left-width)/2;y=alignment==0?work.top+gap:alignment==1?work.top+(work.bottom-work.top-height)/2:work.bottom-height-gap;}
    x=std::clamp(x,int(work.left),std::max(int(work.left),int(work.right)-width));
    y=std::clamp(y,int(work.top),std::max(int(work.top),int(work.bottom)-height));
    return {x,y,x+width,y+height};
}
struct ShelfMetrics {
    int cell_width,row_height,icon,tile,label,margin;
    static ShelfMetrics make(int size,int dpi){size=std::clamp(size,0,2);auto px=[dpi](int v){return (v*std::clamp(dpi,72,768)+48)/96;};int icons[]={32,44,60},tiles[]={50,62,78},cells[]={72,88,104};return {px(cells[size]),px(tiles[size]+34),px(icons[size]),px(tiles[size]),px(24),px(12)};}
    int collapsed_height()const{return row_height+2*margin;}
    int columns(int width)const{return std::max(1,(width-2*margin)/cell_width);}
    int expanded_height(int count,int width,int available,int header)const{int rows=std::max(1,(count+columns(width)-1)/columns(width));return std::max(collapsed_height(),std::min(available,header+rows*row_height+margin));}
};
inline bool shelf_geometry_tests(){for(int dpi:{96,120,144,192,240}){int prior=0;for(int size=0;size<3;++size){auto m=ShelfMetrics::make(size,dpi);int hover=m.icon*117/100;if(hover>m.tile||m.tile+m.label>m.row_height||m.collapsed_height()<=prior||m.columns(11*m.cell_width+2*m.margin)!=11)return false;int height=m.expanded_height(1000,800,600,58);if(height>600||height<m.collapsed_height())return false;prior=m.collapsed_height();}}return true;}
inline bool shelf_taskbar_tests(){
    for(int dpi:{96,120,144,192,240}){for(int origin:{0,-3840}){
        RECT monitor{origin,0,origin+3840,2160},bar{origin,2088,origin+3840,2160};
        auto safe=reserve_taskbar(monitor,monitor,bar);if(safe.bottom!=bar.top)return false;
        auto reserved=reserve_taskbar(safe,monitor,bar);if(!EqualRect(&safe,&reserved))return false;
        // Hidden/clipped taskbars retain the same reserved height; no jumping.
        auto hidden=reserve_taskbar(monitor,monitor,RECT{origin,2158,origin+3840,2230});if(!EqualRect(&safe,&hidden))return false;
        for(int repeat=0;repeat<10;++repeat)for(int size:{0,2,1,2,0})for(int align=0;align<3;++align){auto m=ShelfMetrics::make(size,dpi);for(int height:{m.collapsed_height(),m.expanded_height(1000,1200,1700,58)}){
            auto r=shelf_bounds(safe,11*m.cell_width+2*m.margin,height,align,MulDiv(22,dpi,96));RECT overlap{};
            if(IntersectRect(&overlap,&r,&bar)||r.left<safe.left||r.right>safe.right||r.top<safe.top||r.bottom>safe.bottom)return false;
            auto custom=shelf_bounds(safe,1200,height,align,22,true,origin+3000,2150);if(IntersectRect(&overlap,&custom,&bar)||custom.right>safe.right)return false;
        }}
        auto other=reserve_taskbar(monitor,monitor,RECT{origin+3840,2088,origin+7680,2160});if(!EqualRect(&other,&monitor))return false;
        auto floating=reserve_taskbar(monitor,monitor,RECT{origin,1000,origin+3840,1072});if(!EqualRect(&floating,&monitor))return false;
        if(reserve_taskbar(monitor,monitor,RECT{origin,0,origin+3840,72}).top!=72)return false;
        if(reserve_taskbar(monitor,monitor,RECT{origin,0,origin+72,2160}).left!=origin+72)return false;
        if(reserve_taskbar(monitor,monitor,RECT{origin+3768,0,origin+3840,2160}).right!=origin+3768)return false;
    }}
    return true;
}
}
