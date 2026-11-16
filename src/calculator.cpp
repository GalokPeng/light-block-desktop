#include "calculator.hpp"
#include <sstream>
#include <iomanip>
#include <locale>
namespace lb {
namespace {
class Parser {
    std::wstring text_;size_t position_{};int nodes_{},depth_{};
    struct Invalid {};
    void space() { while(position_<text_.size()&&iswspace(text_[position_])) ++position_; }
    bool take(wchar_t value) { space();if(position_<text_.size()&&text_[position_]==value) { ++position_;return true; }return false; }
    bool starts(std::wstring_view value) { space();return std::wstring_view(text_).substr(position_).starts_with(value); }
    long double checked(long double value) { if(++nodes_>50||!std::isfinite(value)||std::abs(value)>1e100L) throw Invalid{};return value; }
    long double number() {
        space();size_t start=position_;bool digit=false;
        if(position_+2<=text_.size()&&text_[position_]==L'0') {
            wchar_t code=wchar_t(towlower(text_[position_+1]));int base=code==L'x'?16:code==L'o'?8:code==L'b'?2:0;
            if(base) { position_+=2;long double value=0;while(position_<text_.size()) { wchar_t c=wchar_t(towlower(text_[position_]));int n=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;if(n<0||n>=base) break;digit=true;value=value*base+n;++position_;if(value>1e100L) throw Invalid{}; }if(!digit) throw Invalid{};return checked(value); }
        }
        while(position_<text_.size()&&iswdigit(text_[position_])) { digit=true;++position_; }
        if(position_<text_.size()&&text_[position_]==L'.') { ++position_;while(position_<text_.size()&&iswdigit(text_[position_])) { digit=true;++position_; } }
        if(!digit) throw Invalid{};
        if(position_<text_.size()&&(text_[position_]==L'e'||text_[position_]==L'E')) { ++position_;if(position_<text_.size()&&(text_[position_]==L'+'||text_[position_]==L'-')) ++position_;size_t exponent=position_;while(position_<text_.size()&&iswdigit(text_[position_])) ++position_;if(position_==exponent) throw Invalid{}; }
        auto token=text_.substr(start,position_-start);wchar_t *end=nullptr;long double value=wcstold(token.c_str(),&end);if(!end||*end) throw Invalid{};return checked(value);
    }
    long double atom() {
        if(++depth_>32) throw Invalid{};scope_exit leave([&]{--depth_;});
        if(take(L'(')) { auto value=sum();if(!take(L')')) throw Invalid{};return value; }return number();
    }
    long double power() { auto left=atom();if(starts(L"**")) { position_+=2;auto right=unary();if(std::abs(right)>16) throw Invalid{};left=checked(std::pow(left,right)); }return left; }
    long double unary() {
        if(++depth_>32) throw Invalid{};scope_exit leave([&]{--depth_;});
        if(take(L'+')) return checked(unary());if(take(L'-')) return checked(-unary());return power();
    }
    long double product() {
        auto left=unary();for(;;) {
            space();bool floor=starts(L"//");wchar_t operation=position_<text_.size()?text_[position_]:0;
            if(operation!=L'*'&&operation!=L'/'&&operation!=L'%') return left;
            position_+=floor?2:1;auto right=unary();if((operation==L'/'||operation==L'%')&&right==0) throw Invalid{};
            if(operation==L'*') left=checked(left*right);else if(operation==L'/') left=checked(floor?std::floor(left/right):left/right);
            else { auto value=std::fmod(left,right);if(value!=0&&std::signbit(value)!=std::signbit(right)) value+=right;left=checked(value); }
        }
    }
    long double sum() { auto left=product();for(;;) { space();wchar_t operation=position_<text_.size()?text_[position_]:0;if(operation!=L'+'&&operation!=L'-') return left;++position_;auto right=product();left=checked(operation==L'+'?left+right:left-right); } }
public:
    explicit Parser(std::wstring value):text_(std::move(value)) {}
    long double run() { if(text_.empty()||text_.size()>120) throw Invalid{};auto value=sum();space();if(position_!=text_.size()) throw Invalid{};return value; }
};
}
std::optional<std::wstring> calculate(std::wstring expression) {
    if(expression.size()>120) return {};
    for(auto &ch:expression) { if(ch==L'×') ch=L'*';else if(ch==L'÷') ch=L'/';else if(ch==L'−') ch=L'-'; }
    try { Parser parser(std::move(expression));auto value=parser.run();if(value==0) value=0;std::wostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(16)<<value;return stream.str(); }catch(...) { return {}; }
}
bool calculator_tests() {
    std::pair<wchar_t const*,wchar_t const*> valid[]={{L"24 × 60",L"1440"},{L"2 + 3 * 4",L"14"},{L"-2**2",L"-4"},{L"(-2)**2",L"4"},{L"2**-2",L"0.25"},{L"-7//3",L"-3"},{L"-7%3",L"2"},{L"7%-3",L"-2"},{L"1e2+0x10",L"116"},{L".5+.25",L"0.75"}};
    for(auto const& [expression,result]:valid) if(calculate(expression)!=result) return false;
    for(auto expression:{L"__import__('os')",L"(1).__class__",L"1/0",L"2**10000000",L"1e101",L"nan",L"abs(-1)",L"1;2",L"2+(",L"9**9**9"}) if(calculate(expression)) return false;return true;
}
}
