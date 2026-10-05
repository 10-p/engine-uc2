/* xcorr: the largest normalised cross-correlation of two mono float32 signals over a range of lags.
 *   xcorr <a.f32> <b.f32> <lag-from> <lag-to>      (lags in samples; b is shifted by the lag)
 * Prints "<best r> <best lag>". Used by tools/dev/movie-lang-evidence.py (compiled on the fly). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float *load(const char *p, long *n)
{
	FILE *f = fopen(p, "rb");
	if (!f) { perror(p); exit(1); }
	fseek(f, 0, SEEK_END); *n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
	float *x = malloc(*n * 4);
	if (fread(x, 4, *n, f) != (size_t)*n) { perror(p); exit(1); }
	fclose(f);
	return x;
}

int main(int argc, char **argv)
{
	long na, nb;
	if (argc != 5) return 2;
	float *a = load(argv[1], &na), *b = load(argv[2], &nb);
	long l0 = atol(argv[3]), l1 = atol(argv[4]);
	double best = -2; long bestlag = 0;
	for (long lag = l0; lag <= l1; lag++) {
		double sab = 0, saa = 0, sbb = 0;
		for (long i = 0; i < na; i++) {
			long j = i + lag;
			if (j < 0 || j >= nb) continue;
			sab += (double)a[i] * b[j]; saa += (double)a[i] * a[i]; sbb += (double)b[j] * b[j];
		}
		double r = (saa > 0 && sbb > 0) ? sab / sqrt(saa * sbb) : 0;
		if (r > best) { best = r; bestlag = lag; }
	}
	printf("%.4f %ld\n", best, bestlag);
	return 0;
}
