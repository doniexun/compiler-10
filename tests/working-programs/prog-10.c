#ifdef GCC
#define ptr_t int *
#endif

int main ()
{
  int a = 45;
  ptr_t a_ptr = &a;
  printf ("%d\n", *a_ptr);
  return 0;
}
