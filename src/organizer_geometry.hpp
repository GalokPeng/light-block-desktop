#pragma once
#include <algorithm>
namespace lb::organizer {
struct ShelfMetrics {
    int cell_width,row_height,icon,tile,label,margin;
    static ShelfMetrics make(int size,int dpi){size=std::clamp(size,0,2);auto px=[dpi](int v){return (v*std::clamp(dpi,72,768)+48)/96;};int icons[]={32,44,60},tiles[]={50,62,78},cells[]={72,88,104};return {px(cells[size]),px(tiles[size]+34),px(icons[size]),px(tiles[size]),px(24),px(12)};}
    int collapsed_height()const{return row_height+2*margin;}
    int columns(int width)const{return std::max(1,(width-2*margin)/cell_width);}
    int expanded_height(int count,int width,int available,int header)const{int rows=std::max(1,(count+columns(width)-1)/columns(width));return std::max(collapsed_height(),std::min(available,header+rows*row_height+margin));}
};
inline bool shelf_geometry_tests(){for(int dpi:{96,120,144,192,240}){int prior=0;for(int size=0;size<3;++size){auto m=ShelfMetrics::make(size,dpi);int hover=m.icon*117/100;if(hover>m.tile||m.tile+m.label>m.row_height||m.collapsed_height()<=prior||m.columns(11*m.cell_width+2*m.margin)!=11)return false;int height=m.expanded_height(1000,800,600,58);if(height>600||height<m.collapsed_height())return false;prior=m.collapsed_height();}}return true;}
}
