int test4 (int a) {
    while (a < 20) {
	a = a + 1;
    }
    return a;
}

int main () {
    printf ("%d\n", test4 (3));
    printf ("%d\n", test4 (-1));
    printf ("%d\n", test4 (20));
    printf ("%d\n", test4 (21));
    return 0;
}
