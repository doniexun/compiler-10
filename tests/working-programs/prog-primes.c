int main () {
  int primes[100];
  int i = 0;
  primes[i++] = 2;
  int j;
  for (j = 3; j < 100; j++) {
    int k;
    for (k = 0; primes[k] <= j / primes[k]; k++) {
      if (j % primes[k] == 0)
        goto end;
    }
    primes[i++] = j;
    printf ("%d\n", j);
   end:
    ;
  }
  return 0;
}
