#include "TapeEngine7.h"
#include "../third_party/airwindows/ToTape7/ToTape7.h"
#include <array>
#include <vector>
#include <iostream>
void seedReference(ToTape7&);
template<class T> bool scenario(double rate,int setting,double& error) {
    TapeEngine7 actual; actual.reset(rate);
    ToTape7 reference(nullptr); reference.setSampleRate(rate); seedReference(reference);
    std::array<float,10> params{};
    for(int i=0;i<10;++i) { params[i]=setting<3?static_cast<float>(setting)*.5f:static_cast<float>((i*7+setting*3)%11)/10.f; actual.setParameter(i,params[i]); reference.setParameter(i,params[i]); }
    std::array<std::vector<T>,2> a{std::vector<T>(257),std::vector<T>(257)},b=a;
    T* ap[]{a[0].data(),a[1].data()}; T* bp[]{b[0].data(),b[1].data()};
    for(int block=0;block<48;++block) {
        if(block==24) for(int i=0;i<10;++i) { auto v=1.f-params[i]; actual.setParameter(i,v); reference.setParameter(i,v); }
        for(int c=0;c<2;++c) for(int n=0;n<257;++n) a[c][n]=b[c][n]=static_cast<T>(block<2?0.:.8*std::sin((block*257+n)*.031*(c+1)));
        if constexpr(std::is_same_v<T,float>) { actual.processReplacing(ap,ap,257); reference.processReplacing(bp,bp,257); }
        else { actual.processDoubleReplacing(ap,ap,257); reference.processDoubleReplacing(bp,bp,257); }
        for(int c=0;c<2;++c) for(int n=0;n<257;++n) {
            if(!std::isfinite(a[c][n]) || !std::isfinite(b[c][n])) return false;
            error=std::max(error,std::abs(static_cast<double>(a[c][n])-static_cast<double>(b[c][n])));
        }
    }
    return error<1.e-12;
}
int main() {
    int count=0; double error=0;
    for(auto rate:{44100.,48000.,96000.,192000.}) for(int setting=0;setting<6;++setting) {
        if(!scenario<float>(rate,setting,error) || !scenario<double>(rate,setting,error)) { std::cerr<<"Reference mismatch: "<<error<<'\n'; return 1; }
        count+=2;
    }
    std::cout<<count<<" upstream comparisons passed; maximum error "<<error<<'\n';
}
