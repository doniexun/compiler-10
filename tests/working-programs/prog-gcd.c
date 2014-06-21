
#define SWAP(X, Y) do {				  int _t = (X);					  (X) = (Y);					  (Y) = _t;					 } while (0)

int main ()
{
  int a = 1892;
  int b = 648;
  while (b != 0)
    {
      a %= b;
      SWAP (a, b);
    }
  printf ("%d\n", a);
  return 0;
}
