#include <cassert>
#include <fstream>
#include <random>

/* zipf random number generator.
   https://cse.usf.edu/~kchriste/tools/genzipf.c */
int zipf(double alpha, int n, double z) {
  static bool first = true;  // Static first time flag
  static double c = 0;       // Normalization constant
  double sum_prob;           // Sum of probabilities
  double zipf_value = 0;     // Computed exponential value to be returned

  // Compute normalization constant on first call only
  if (first) {
    for (auto i = 1; i <= n; i++) c = c + (1.0 / pow((double)i, alpha));
    c = 1.0 / c;
    first = false;
  }

  // Map z to the value
  sum_prob = 0;
  for (auto i = 1; i <= n; i++) {
    sum_prob = sum_prob + c / pow((double)i, alpha);
    if (sum_prob >= z) {
      zipf_value = i;
      break;
    }
  }

  // Assert that zipf_value is between 1 and N
  assert((zipf_value >= 1) && (zipf_value <= n));

  return zipf_value;
}

int main() {
  constexpr int MAX_RECORDS = 1000000;
  constexpr int NUM = 100000;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0, 1.0);
  std::ofstream ofile;
  ofile.open("zipf_distrib");

  for (auto i = 0; i < NUM; i++) {
    ofile << zipf(0.99, MAX_RECORDS, dis(gen)) << "\n";
  }
}
