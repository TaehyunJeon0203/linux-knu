#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char *argv[]) {
	double num1 = atof(argv[1]);
	double num2 = atof(argv[3]);
	char *op = argv[2];
	double result;

	if (strcmp(op, "+") == 0) {
		result = num1 + num2;
	} else if (strcmp(op, "-") == 0) {
		result = num1 - num2;
	} else if (strcmp(op, "x") == 0) {
		result = num1 * num2;
	} else if (strcmp(op, "/") == 0) {
		if (num2 == 0) {
			printf("no 0");
			return 1;
		} else {
			result = num1 / num2;
		}
	}
	printf("%f\n", result);
}
