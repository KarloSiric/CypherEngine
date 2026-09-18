# Kernel Tests

Kernel tests define the numerical contract before topology depends on it. They
cover:

- exact orientation signs for ordinary, collinear, nearly collinear, reversed,
  translated, and scale-extreme inputs;
- checked point-plane classification on both sides and on the policy boundary,
  including adjacent `nextafter` values;
- zero-length, subnormal, overflow-prone, NaN, and infinity inputs;
- deterministic quantization, tie handling, and ordering;
- repeatability under the documented coordinate limits.

Property cases use fixed, named seeds and print the seed and generated input on
failure. Random tests supplement explicit adversarial examples; they do not
replace them. No predicate test may assert only that two implementations agree
when both can share the same defect.
