#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#define N 4
#define MAXLINE 100



void sinx_taylor(int num_elements, int terms, double* x, double* result) {
	int fd[N][2];
	pid_t pid;

	for (int i=0; i<num_elements; i++) {
		pipe(fd[i]);
		pid = fork();

		if (pid == 0) {
			close(fd[i][0]);

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
		
			char message[MAXLINE];	
			sprintf(message, "%lf", value);
			write(fd[i][1], message, strlen(message) + 1);
			close(fd[i][1]);
			exit(0);
		} else {
			close(fd[i][1]);
		}
	}
	for (int i=0; i<num_elements; i++) {
		char line[MAXLINE];
		read(fd[i][0], line, MAXLINE);
		result[i] = atof(line);
		close(fd[i][0]);
		wait(NULL);
	}	
}


int main() {
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);

	for (int i=0; i<N; i++) {
		printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}

	return 0;

}
