#include "scroll_capture.hpp"
#include <array>
#include <limits>
namespace lb::scroll {
namespace {
int gray(uint32_t pixel) { return int(((pixel>>16)&255)*77+((pixel>>8)&255)*150+(pixel&255)*29)>>8; }
struct Gray { int height{};std::vector<int> pixels; };
Gray simplify(Frame const& frame) {
    Gray result;result.height=frame.height;result.pixels.resize(size_t(frame.height)*64);
    for(int y=0;y<frame.height;++y) for(int x=0;x<64;++x) result.pixels[size_t(y)*64+size_t(x)]=gray(frame.pixels[size_t(y)*size_t(frame.width)+size_t((x+1)*(frame.width-16)/66+4)]);return result;
}
double row_difference(Gray const& a,int ay,Gray const& b,int by) { int sum=0;for(int x=0;x<64;++x) sum+=abs(a.pixels[size_t(ay)*64+size_t(x)]-b.pixels[size_t(by)*64+size_t(x)]);return double(sum)/64; }
double row_texture(Gray const& a,int y) { int sum=0;for(int x=1;x<64;++x) sum+=abs(a.pixels[size_t(y)*64+size_t(x)]-a.pixels[size_t(y)*64+size_t(x-1)]);return double(sum)/63; }
double score(Gray const& a,Gray const& b,int shift,int header,int footer) {
    int top=header+std::max(0,-shift),bottom=a.height-footer-std::max(0,shift);if(bottom-top<40) return 1e9;
    std::array<double,48> errors{};int count=0;
    for(int n=0;n<48;++n) { int y=top+(bottom-top-1)*n/47;if(row_texture(a,y+shift)<2&&row_texture(b,y)<2) continue;errors[size_t(count++)]=row_difference(a,y+shift,b,y); }
    if(count<8) return 1e9;std::sort(errors.begin(),errors.begin()+count);int kept=std::max(8,count*4/5);double total=0;for(int n=0;n<kept;++n) total+=errors[size_t(n)];return total/kept;
}
}
std::optional<int> match(Frame const& previous,Frame const& current) {
    if(previous.width!=current.width||previous.height!=current.height||previous.width<32||previous.height<96||previous.pixels.size()!=size_t(previous.width)*previous.height||current.pixels.size()!=previous.pixels.size()) return {};
    auto a=simplify(previous),b=simplify(current);double same=0;for(int y=0;y<a.height;y+=3) same+=row_difference(a,y,b,y);same/=((a.height+2)/3);if(same<.45) return 0;
    int header=0,footer=0;while(header<a.height/4&&row_difference(a,header,b,header)<.6) ++header;while(footer<a.height/8&&row_difference(a,a.height-1-footer,b,a.height-1-footer)<.6) ++footer;header=std::min(header+3,a.height/4);footer=std::min(footer+3,a.height/8);
    int range=a.height-header-footer-48,best_shift=0;double best=1e9;std::vector<double> scores(size_t(range*2+1));
    for(int shift=-range;shift<=range;++shift) { double value=score(a,b,shift,header,footer);scores[size_t(shift+range)]=value;if(value<best) { best=value;best_shift=shift; } }
    if(best>10) return {};double second=1e9;for(int shift=-range;shift<=range;++shift) if(abs(shift-best_shift)>8) second=std::min(second,scores[size_t(shift+range)]);
    if(second-best<.35) return {};return best_shift;
}
Stitch::Change Stitch::append(Frame frame) { return append_impl(std::move(frame),{}); }
Stitch::Change Stitch::manual(Frame frame,int overlap) {if(overlap<0||overlap>=frame.height)return Change::unmatched;int shift=frame.height-overlap;return append_impl(std::move(frame),shift);}
Stitch::Change Stitch::append_impl(Frame frame,std::optional<int> forced) {
    if(frame.width<32||frame.height<96||frame.pixels.size()!=size_t(frame.width)*frame.height||frame.pixels.size()>maximum_pixels) return Change::limit;
    if(pixels.empty()) { width=frame.width;height=frame.height;pixels=frame.pixels;previous_=std::move(frame);return Change::added; }
    if(frame.width!=width||frame.height!=previous_.height)return Change::unmatched;
    auto shift=forced?forced:match(previous_,frame);if(!shift) return Change::unmatched;
    int next=position_+*shift,prepend=std::max(0,-next),append=std::max(0,next+frame.height-height);
    int header=0,footer=0;
    if(!forced&&*shift&&(prepend||append)) { auto a=simplify(previous_),b=simplify(frame);while(header<frame.height/4&&row_difference(a,header,b,header)<.6) ++header;while(footer<frame.height/8&&row_difference(a,frame.height-1-footer,b,frame.height-1-footer)<.6) ++footer; }
    size_t size=size_t(height+prepend+append)*size_t(width);if(size>maximum_pixels) return Change::limit;
    if(size>pixels.capacity()) pixels.reserve(std::min(maximum_pixels,std::max(size,pixels.capacity()+pixels.capacity()/2)));
    if(prepend||append){Undo undo;undo.position=position_;undo.height=height;undo.prepend=prepend;undo.append=append;undo.header=header;undo.footer=footer;
        if(prepend&&header)undo.edge.assign(pixels.begin(),pixels.begin()+ptrdiff_t(size_t(header)*width));
        else if(append&&footer)undo.edge.assign(pixels.end()-ptrdiff_t(size_t(footer)*width),pixels.end());undo.previous=std::move(previous_);undo_=std::move(undo);}
    if(prepend) { if(header) pixels.erase(pixels.begin(),pixels.begin()+ptrdiff_t(size_t(header)*width));pixels.insert(pixels.begin(),frame.pixels.begin(),frame.pixels.begin()+ptrdiff_t(size_t(prepend+header)*width));height+=prepend;next=0; }
    if(append) { if(footer) pixels.resize(pixels.size()-size_t(footer)*width);pixels.insert(pixels.end(),frame.pixels.end()-ptrdiff_t(size_t(append+footer)*width),frame.pixels.end());height+=append; }
    previous_=std::move(frame);position_=next;return prepend||append?Change::added:Change::unchanged;
}
bool Stitch::undo() {
    if(!undo_)return false;auto undo=std::move(*undo_);undo_.reset();
    if(undo.prepend){pixels.erase(pixels.begin(),pixels.begin()+ptrdiff_t(size_t(undo.prepend+undo.header)*width));pixels.insert(pixels.begin(),undo.edge.begin(),undo.edge.end());}
    if(undo.append){pixels.resize(size_t(undo.height-undo.footer)*width);pixels.insert(pixels.end(),undo.edge.begin(),undo.edge.end());}
    previous_=std::move(undo.previous);position_=undo.position;height=undo.height;return true;
}
bool tests() {
    if(!fits_pixel_budget(4000,10000)||fits_pixel_budget(4000,10001)||fits_pixel_budget(-1,100)||fits_pixel_budget(100,0)||!fits_pixel_budget(3840,10416))return false;
    Frame canvas;canvas.width=160;canvas.height=1600;canvas.pixels.resize(size_t(canvas.width)*canvas.height);uint32_t random=37;
    for(auto &pixel:canvas.pixels) { random=random*1664525+1013904223;uint32_t value=random>>24;pixel=0xff000000|value*0x010101; }
    auto viewport=[&](int offset) { Frame frame;frame.width=160;frame.height=240;frame.pixels.assign(canvas.pixels.begin()+offset*160,canvas.pixels.begin()+(offset+240)*160);for(int y=0;y<20;++y) for(int x=0;x<160;++x) frame.pixels[size_t(y)*160+size_t(x)]=0xff101010|uint32_t(x)*0x00010101;return frame; };
    auto a=viewport(400),b=viewport(512);if(match(a,b)!=112||match(b,a)!=-112||match(a,a)!=0) return false;
    Stitch stitch;if(stitch.append(a)!=Stitch::Change::added||stitch.append(b)!=Stitch::Change::added||stitch.height!=352) return false;
    if(stitch.append(viewport(450))!=Stitch::Change::unchanged||stitch.height!=352) return false;
    if(stitch.append(viewport(320))!=Stitch::Change::added||stitch.height!=432) return false;
    Frame invalid=viewport(400);invalid.width=10;if(stitch.append(std::move(invalid))!=Stitch::Change::limit)return false;
    Stitch reversible;if(reversible.append(a)!=Stitch::Change::added)return false;auto original=reversible.pixels;
    if(reversible.append(b)!=Stitch::Change::added||!reversible.can_undo()||!reversible.undo()||reversible.pixels!=original||reversible.height!=240||reversible.can_undo())return false;
    if(reversible.append(viewport(280))!=Stitch::Change::added||!reversible.undo()||reversible.pixels!=original)return false;
    Frame blank=a;std::fill(blank.pixels.begin(),blank.pixels.end(),0xffffffff);if(reversible.append(blank)!=Stitch::Change::unmatched)return false;
    if(reversible.manual(blank,120)!=Stitch::Change::added||reversible.height!=360||!reversible.undo()||reversible.pixels!=original)return false;
    // High velocity still has a valid seam: use a full-screen-sized fixture and
    // keep only 140 pixels of overlap, as the periodic sampler must support.
    Frame wide;wide.width=320;wide.height=2160;wide.pixels.resize(size_t(wide.width)*wide.height);for(auto& p:wide.pixels){random=random*1664525+1013904223;p=0xff000000|(random>>8);}
    auto screen=[&](int offset){Frame f;f.width=wide.width;f.height=1080;f.pixels.assign(wide.pixels.begin()+offset*wide.width,wide.pixels.begin()+(offset+1080)*wide.width);return f;};
    return match(screen(0),screen(940))==940;
}
}
