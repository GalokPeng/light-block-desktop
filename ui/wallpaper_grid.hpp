#pragma once
#include "ui.hpp"

namespace ui {
inline int wallpaper_grid_cell_width(QListView const* view){
    // QListView reserves the style's scrollbar extent when wrapping, even when
    // QSS makes the visible scrollbar narrower. Match that internal bound;
    // viewport width alone leaves the third item on the next line.
    auto bar=view->verticalScrollBar();int available=view->maximumViewportSize().width();
    if(view->verticalScrollBarPolicy()==Qt::ScrollBarAsNeeded&&!view->style()->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap,nullptr,bar)){
        int frame=0;if(view->style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents,nullptr,view)){QStyleOption option;option.initFrom(view);frame=2*view->style()->pixelMetric(QStyle::PM_DefaultFrameWidth,&option,view);}
        available-=view->style()->pixelMetric(QStyle::PM_ScrollBarExtent,nullptr,bar)+frame;
    }
    return std::max(1,(std::min(available,view->viewport()->contentsRect().width())-2)/3);
}
}
