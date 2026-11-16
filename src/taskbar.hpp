#pragma once
#include "lb.h"

namespace lb::taskbar {
struct LayoutStatus {
    LONG version,sequence,state,repairs,rebuilds,error,changes,reserved;
    double host_width,frame_width,tray_width,group_left,group_right,scale;
    LONG appearance_revision,appearance_color,appearance_opacity,appearance_flags,appearance_radius,appearance_reserved;
};
static_assert(sizeof(LayoutStatus)==104);
struct Control { LONG sequence,enabled,color,opacity,radius;ULONGLONG parent_created; };
void configure();
bool has_session();
bool transition_tests();
bool shutdown_tests();
bool visibility_tests();
bool shutdown();
void tick();
int helper_main(int argc,wchar_t **argv);
}
