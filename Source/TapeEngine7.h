#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
class TapeEngine7 {
public:
    TapeEngine7() { reset(44100.0); }
    void reset(double rate);
    void setParameter(int index, float value);
    void processReplacing(float** inputs, float** outputs, int sampleFrames);
    void processDoubleReplacing(double** inputs, double** outputs, int sampleFrames);
    double getSampleRate() const { return sampleRate; }
private:
    double sampleRate = 44100.0;
    float A;
    float B;
    float C;
    float D;
    float E;
    float F;
    float G;
    float H;
    float I;
    float J;

	double iirEncL;
	double iirEncR;
	
	double dL[1002];
	double dR[1002];
	double sweepL;
	double sweepR;
	double nextmaxL;
	double nextmaxR;
	int gcount;		
	
	enum {
		prevSampL1,
		prevSampR1,
		threshold1,
		prevSampL2,
		prevSampR2,
		threshold2,
		prevSampL3,
		prevSampR3,
		threshold3,
		prevSampL4,
		prevSampR4,
		threshold4,
		prevSampL5,
		prevSampR5,
		threshold5,
		prevSampL6,
		prevSampR6,
		threshold6,
		prevSampL7,
		prevSampR7,
		threshold7,
		prevSampL8,
		prevSampR8,
		threshold8,
		prevSampL9,
		prevSampR9,
		threshold9,
		gslew_total
	}; //fixed frequency pear filter for ultrasonics, stereo
	double gslew[gslew_total]; //end bias	
	
	double iirMidRollerL;
	double iirLowCutoffL;
	double iirMidRollerR;
	double iirLowCutoffR;
	
	double headBumpL;
	double headBumpR;
	enum {
		hdb_freq,
		hdb_reso,
		hdb_a0,
		hdb_a1,
		hdb_a2,
		hdb_b1,
		hdb_b2,
		hdb_sL1,
		hdb_sL2,
		hdb_sR1,
		hdb_sR2,
		hdb_total
	}; //fixed frequency biquad filter for ultrasonics, stereo
	double hdbA[hdb_total];
	double hdbB[hdb_total];
	
	double iirDecL;
	double iirDecR;
	
	double lastSampleL;
	double intermediateL[16];
	bool wasPosClipL;
	bool wasNegClipL;
	double lastSampleR;
	double intermediateR[16];
	bool wasPosClipR;
	bool wasNegClipR; //Stereo ClipOnly2	
	
	uint32_t fpdL;
	uint32_t fpdR;
	//default stuff
};
