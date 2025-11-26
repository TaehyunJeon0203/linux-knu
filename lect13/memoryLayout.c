#include <stdio.h>
#include <stdlib.h>
int glob = 1;
int main() {
	static int stat = 2;
	int stack;
	char *heap = (char *)malloc(100);
	printf("Stack = %p\n", &stack);
	printf("Heap = %p\n", heap);
	printf("Global = %p\n", &glob);
	printf("Static = %p\n", &stat);
	printf("main = %p\n", main);
	return 0;
}
