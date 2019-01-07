/*******************************************************************************
Proof of concept chirp detection by Brad Eckert.
Based on the paper "Quantum Time Signals in Living Beings" by Brad Eckert.
Equations and sections are cross-referenced to the paper whenever possible.
This version of the software is intended as a reference, so speed-up hacks are
avoided in favor of clarity.

Copyright 2019, Brad Eckert

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

1. This permission notice does not grant patent rights.

2. The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Revision History
0: Work in progress, not officially released.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fft.h"

#define N          1024         // FFT points
#define MAXPOINTS 16384         // X size
#define sigAmpl   10000         // amplitude of test chirp
#define H_V0         16         // output step size
//#define VERBOSE               // you probably want PASSES=1 for this
#define PASSES       20

// For some reason, the software hangs if PASSES>6.

double X[MAXPOINTS];            // X input buffer
double R = -0.5;
double frequency = 0.95;        // initial frequency for test chirp, near Fs/2(1.0)
int pink = 0;                   // the chirp spectrum is white or pink

double now();

float maxscale(void) {          // maximum amplitude scale
    if (R<0.0) {
        return exp(-R*N/MAXPOINTS);
    } else {
        return 1.0;
    }
}
float signal(int idx) {
	float fscale = (exp((float)idx * R / N) - 1.0) * PI * N / R;
	float ascale = exp((float)idx * 0.5 * R / N) * maxscale();
	if (pink) {
        ascale = 1.0;
	}
	float angle = frequency * fscale;
    float sig = sigAmpl * ascale * sin (angle);
    return sig;
}

/** Compress
Interpolate LENGTH output samples from a sequence of input samples.
Pitch is stepped after each output point is stored if `post`=1.
Otherwise, it's stepped after each input point is read.
*/

void compress(
	double* in,			        // input stream
	double* out,		        // output stream
	int length,			        // points in output stream
	double pitch,		        // exponential input sample pitch
	double Prate,		        // growth/decay rate of pitch
	double Arate,		        // growth/decay rate of amplitude
	int post)                   // step pitch upon output
{
	int oc = length;	        // output counter
	double idx0 = 0;	        // input index
	double idx1 = 0;
	int pending = 1;	        // input read is pending
	double X0 = 0;
	double X1 = *in++;
	double Y;
	double Ascale = 1.0;        // amplitude compensation
	while (oc) {
        if (pending) {
            pending = 0;
            X0 = X1;
            X1 = *in++;
            if (post==0) {      // sweep with the input
                pitch += pitch * Prate;
            }
        }
        double frac0, int0;     // integer and fractional parts of X indices
        double frac1, int1;
		idx0 = idx1;            // next input span
		idx1 += pitch;
        frac0 = modf(idx0, &int0);
        frac1 = modf(idx1, &int1);
		int k = int1 - int0;    // input span crosses this many boundaries
		switch (k) {
        case 0:
            Y = X0 * (frac1 - frac0);
            break;
        case 1:
            Y = X0 * (1 - frac0) + X1 * frac1;
            pending = 1;
            break;
        default: // k>1
            Y = X0 * (1 - frac0) + X1;
            while (k>1) {
                X0 = X1;
                X1 = *in++;
                if (post==0) {  // sweep with the input
                    pitch += pitch * Prate;
                }
                k--;
                if (k>1) {
                    Y += X1;
                }
            }
            Y += X1 * frac1;
            pending = 1;
            break;
		}
        *out++ = Y * Ascale;  oc--;
        if (post) {             // sweep with the output
            pitch += pitch * Prate;
        }
        Ascale += Ascale * Arate;
	}
}

void dumpComplex (struct complex *data, int length, char* filename)
{
    FILE *fp;  int i;
    fp = fopen(filename, "w+");
    for (i=0; i<length; i++) {
        fprintf(fp, "%g,%g\n", data[i].r, data[i].i);
    }
    fclose(fp);
}

void dumpReal (double *data, int length, char* filename)
{
    FILE *fp;  int i;
    fp = fopen(filename, "w+");
    for (i=0; i<length; i++) {
        fprintf(fp, "%g\n", data[i]);
    }
    fclose(fp);
}

double XW[N];                   // warped version of X input
double mag2[N/2];
double W[PASSES][N/2];          // warped version of FFT output
double V[N];                    // Correlated passes

///////////////////////////////////////////////////////////////////////////////
int main()
{
    struct complex *Y;          // FFT working buffer of N points
    int i, p;

    for (i=0; i<MAXPOINTS; i++) {
        X[i] = signal(i);       // set up a test chirp
    }
    dumpReal(X, 2*N, "X.txt");  // dump the test chirp
    Y = InitFFT(N);             // set up Y for in-place FFT

    // M is the number of X input samples to warp to Y. Usually N to 4N.
    float M = -N * log(1 - N*(1 - exp(-fabs(R)/N))) / fabs(R);  // eq. 7
    // rate constant for downsampling exponential sweep
    double lambda = exp(-R/N) - 1;                              // eq. 9
    // k is an upsampling constant, about 2
    float k = N * log(((float)N-2)/((float)N-4));               // eq. 15
	// real H_X, ideal offset in X samples
	float gamma = H_V0 * k / fabs(R);                           // eq. 17
	int H_X = round(gamma);		// use the closest value
	int H_V = H_V0;
	float upsam_correct = gamma / H_X;
	double zeta = exp(k/N) - 1; // upsampling rate              // eq. 16

    float pitch = 1.0;                                          // eq. 10
    if (R>0) {
        pitch = R*M/N;          // upchirp starts sample pitch at e^RM/N
    }

    printf("N=%d, M=%g, R=%g, k=%g, lambda=%g\n",N,M,R,k,lambda);
    printf("H_X=%d, H_V=%d, gamma=%g, zeta=%g, corr=%g\n",
			H_X, H_V, gamma, zeta, upsam_correct);
    double timezero = now();
    int offset = 0;
    memset(V, 0, sizeof(V));    // clear V

    for (p=0; p<PASSES; p++) {
		#ifdef VERBOSE
		double mark = now();
		#endif

		compress(&X[offset], XW, N, pitch, lambda, -lambda/2, 0);
		#ifdef VERBOSE
		printf("Downsampled X to Y in %.3f usec\n", now() - mark);
		dumpReal(XW,N,"Y.txt"); // dump the time-warped input
		mark = now();
		#endif

		for (i=0; i<N; i++) {   // copy
			Y[i].r = XW[i];  Y[i].i = 0.0;
		}
		FFTwindow();            // Hann window
		FFT();
		for (i=0; i<(N/2); i++) {
			mag2[(N/2-1)-i] = Y[i].r * Y[i].r + Y[i].i * Y[i].i;
		}
		#ifdef VERBOSE
		printf("Executed FFT in %.3f usec\n", now() - mark);
		mark = now();
		#endif

		memset(W[p],0,sizeof(W[0]));  // clear V
		compress(mag2, W[p], N/2 - H_V, 1, -zeta, 0, 1);
		#ifdef VERBOSE
		printf("Upsampled U to W in %.3f usec\n", now() - mark);
		dumpReal(W[p],N/2,"W.txt");// dump the time-warped output
		#endif

		// Correlate Ws in V using an offset
		// This offset works with negative R. Not tested with positive R.
		for (i=0; i<(N/2-H_V); i++) {
			V[i + ((PASSES-1)-p)*H_V] += W[p][i];
		}

		offset += H_X;
    }

// dump the final output, which is square of magnitude. Take sqrt to get RMS.
    dumpReal(V,N,"V.txt");

/*******************************************************************************
Dump the RMS W from all of the passes to a CSV file for plotting.
*******************************************************************************/

    printf("Finished %d passes in %g msec.\n", p, 1e-3*(now() - timezero));

    FILE *fp;  int j;
    fp = fopen("AllW.csv", "w+");
    for (i=0; i<N/2; i++) {
        for (j=0; j<p; j++) {
            fprintf(fp, "%g", sqrt(W[j][i]));
            if (j<(p-1)) {
                fprintf(fp, ",");
            }
        }
        fprintf(fp, "\n");
    }
    fclose(fp);

    ByeFFT();
    return 0;
}
