# Remaining image resizer test work

Add these only when the corresponding behavior is defined or implemented:

- Straight-alpha and premultiplied-alpha filtering, including transparent-edge colors and the `RGB <= alpha` premultiplied invariant.
- Malformed views: zero dimensions/channels, null data, insufficient row or pixel stride, and incompatible source/destination layouts.
- AddressSanitizer and UndefinedBehaviorSanitizer execution for the suite where supported; this is test execution configuration, not benchmark work.

Benchmarks are deliberately outside this specification.
