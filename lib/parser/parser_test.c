#include "parser.h"

void my_test(const char *test) {
  char my_test_dat[20];
  parse_string(&test, my_test_dat, 20);
  double a1 = parse_double(&test);
  double a2 = parse_double(&test);
  char ch1 = parse_char(&test);
  double a3 = parse_double(&test);
  char ch2 = parse_char(&test);
  int32_t n1 = parse_int32(&test);
  int32_t n2 = parse_int32(&test);
  double a4 = parse_double(&test);
  double a5 = parse_double(&test);
  char ch3 = parse_char(&test);
  double a6 = parse_double(&test);
  char ch4 = parse_char(&test);
  double a7 = parse_double(&test);

  printf("%s %lf %lf %c %lf %c %d %d %lf %lf %c %lf %c %lf\r\n", my_test_dat,
         a1, a2, ch1, a3, ch2, n1, n2, a4, a5, ch3, a6, ch4, a7);
}

int main() {
  printf("test\r\n");

  const char *test1 = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,"
                      "499.6,M,48.0,M,,*5B\r\n";

  my_test(test1);

  return 0;
}
