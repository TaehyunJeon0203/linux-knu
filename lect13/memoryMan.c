#include <stdio.h>
#include <stdlib.h>

int main() {
	int* arr = (int*)malloc(10*sizeof(int));
	arr[100] = 1;
	return 0;
}
