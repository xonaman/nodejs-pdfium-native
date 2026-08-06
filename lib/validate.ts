/**
 * Returns whether `index` is safe to pass to the native layer, which coerces
 * arguments with ToInt32 (N-API `Int32Value`).
 *
 * `Number.isInteger` alone is insufficient: it is `true` for any integer up to
 * 2^53, but ToInt32 truncates to the low 32 bits, so a value such as
 * `2**32 + 1` would wrap to `1` and silently address a *different* — valid —
 * entry with no error. Restricting to the signed 32-bit range means any value
 * that passes is returned unchanged by ToInt32. In-range negatives are allowed
 * through on purpose, so the native bounds check reports them as "out of
 * range" (preserving that error contract).
 */
export function isNativeIndex(index: number): boolean {
  return Number.isInteger(index) && index >= -0x80000000 && index <= 0x7fffffff;
}
