#include "TapeEngine8.h"
#include <array>
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    int scenarios=0;
    try {
        for(double rate:{44100.,48000.,96000.,192000.}) for(int block:{1,17,512,1024}) {
            TapeEngine8 e; e.reset(rate);
            std::array<std::vector<double>,2> b{std::vector<double>(block),std::vector<double>(block)};
            double* data[]{b[0].data(),b[1].data()};
            for(int pass=0;pass<16;++pass) {
                for(int p=0;p<9;++p) e.setParameter(p,pass%3==0?0.f:pass%3==1?.5f:1.f);
                for(int c=0;c<2;++c) for(int n=0;n<block;++n) b[c][n]=1.5*std::sin((pass*block+n)*.073*(c+1));
                e.processDoubleReplacing(data,data,block);
                for(auto& channel:b) for(auto v:channel) require(std::isfinite(v) && std::abs(v)<1.001,"Nonfinite or unbounded output");
            }
            ++scenarios;
        }
        // Reset must clear every delay/filter and reproduce the same stream.
        TapeEngine8 e;
        std::array<std::array<double,512>,2> b{},first{};
        double* data[]{b[0].data(),b[1].data()};
        for(int pass=0;pass<2;++pass) {
            e.reset(48000);
            for(int c=0;c<2;++c) for(int n=0;n<512;++n) b[c][n]=.2*std::sin(n*.08*(c+1));
            e.processDoubleReplacing(data,data,512);
            if(pass==0) first=b; else require(first==b,"Reset not deterministic");
        }
        ++scenarios;
        // Original ClipOnly3 contributes one sample of small-signal delay.
        for(double rate:{44100.,48000.,96000.,192000.}) {
            e.reset(rate); e.setParameter(3,0); e.setParameter(6,0);
            b={}; b[0][0]=.01; b[1][0]=.01;
            e.processDoubleReplacing(data,data,512);
            require(std::abs(b[0][0])<1.e-6 && std::abs(b[0][1])>1.e-4,"Unexpected impulse latency");
            ++scenarios;
        }
        std::cout<<scenarios<<" DSP scenarios passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
