#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define N 4

int main() {
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];
	int n_terms= 3;

	int pipe[N][2]
	pid_t pids[N];



	sinx_taylor(N, 3, x, res);
	for(int i=0; i<N; i++) {
		if(pipe(pipes[i]) == -1) {
			perror("pipe");
			exit(1);
		}

		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			exit(1);
		} else if (pid == 0) {
			close(pipes[i][0];
			double val = sinx_taylor(x[i], n_terms);
			write(pipes[i][1], &val, sizeof(double));
		}

		for (int i=0; i<N; i++) {
			double val;
			read(pipes[i][0], &val, sizeof(double));

		}

		for (int i=0; i<N; i++) {
			printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
			printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
		}
	}
	
	return 0;
}
