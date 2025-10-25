#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define N 4
#define MAXLINE 100



void sinx_taylor(int num_elements, int terms, double* x, double* result) {
	int child_id, pid;
	int length;
	char message[MAXLINE], line[MAXLINE];
	int fd[2*N];
	for (int i=0; i<num_element; i++) {
		child_id = i;
		pipe(fd + 2 * i);
		pid = fork();
		if (pid == 0) break;
	}


	if (pid == 0) {
		close(fd[0]);
       		double value = x[i];
       		double numer = x[i] * x[i] * x[i];
       		double denom = 6.;
       		int sign = -1;

       		for (int j=1; j<=terms; j++) {
	       		value += (double)sign * numer /denom;
	       		numer *= x[i] * x[i];
	       		denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
	       		sign *= -1;
		}

		result[i] = value;
		sprintf(message, "%lf", result[i]);
		length = strlen(message)+1;
		write(fd[1], message, length);

		exit(0);
	}

	// parent
	for (int i=0; i<N; i++) {
		close(fd[2*i+1]);

		int status;
		wait(&status);

		int child_id = status >> 0;	
		int n = read(fd[2*child_id], line, MAXLINE);
		result[child_id] = atof(line);
	}	
}


void main() {
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);

	for (int i=0; i<N; i++) {
		printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}
	return 0;

}
