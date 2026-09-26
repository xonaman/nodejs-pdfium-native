import { expect } from 'vitest';

// ---------------------------------------------------------------------------
// Shared narrowing helpers for the test suite
// ---------------------------------------------------------------------------
// The counted listings (getAttachments, getAnnotations, getFormFields,
// getSignatures, getJavaScriptActions) report an entry PDFium counted but could
// not load as `null` at its own index rather than dropping it, so their element
// type is `T | null` and `noUncheckedIndexedAccess` adds `undefined` on top of
// that for any indexed read. Reaching for `?.` to silence either one would let a
// fixture rot into damage with every assertion still passing -- which is the
// bug class issue #34 was about -- so these helpers assert instead, and say
// which index went wrong when they fire.

/** Asserts that no entry in a counted listing is `null`, and narrows it. */
export function allLoaded<T>(list: readonly (T | null)[]): T[] {
  const unloadable = list.flatMap((entry, index) => (entry === null ? [index] : []));
  expect(unloadable, 'listing has unloadable entries at these indices').toEqual([]);
  return list as T[];
}

/** Asserts that a counted listing has a loaded entry at `index`, and narrows it. */
export function loadedAt<T>(list: readonly (T | null)[], index: number): T {
  const entry = list[index];
  expect(entry, `no loaded entry at index ${index} of ${list.length}`).toBeTruthy();
  return entry as T;
}

/** Asserts that a counted listing holds exactly one loaded entry, and narrows it. */
export function onlyLoaded<T>(list: readonly (T | null)[]): T {
  expect(list, 'expected a listing of exactly one entry').toHaveLength(1);
  return loadedAt(list, 0);
}
