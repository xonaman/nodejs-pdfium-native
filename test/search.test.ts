import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';
import { textPdfBuffer } from './fixtures.js';

describe('PDFiumPage.search', () => {
  it('finds text occurrences with positions', async () => {
    const doc = await loadDocument(textPdfBuffer);
    const page = await doc.getPage(0);
    const results = page.search('Hello');
    expect(results.length).toBe(1);
    expect(results[0]!.charIndex).toBeGreaterThanOrEqual(0);
    expect(results[0]!.length).toBe(5);
    expect(Array.isArray(results[0]!.rects)).toBe(true);
    expect(results[0]!.rects.length).toBeGreaterThan(0);
    expect(results[0]!.rects[0]).toHaveProperty('left');
    expect(results[0]!.rects[0]).toHaveProperty('top');
    expect(results[0]!.rects[0]).toHaveProperty('right');
    expect(results[0]!.rects[0]).toHaveProperty('bottom');
    page.close();
    doc.destroy();
  });

  it('returns empty for non-matching text', async () => {
    const doc = await loadDocument(textPdfBuffer);
    const page = await doc.getPage(0);
    const results = page.search('xyz-no-match');
    expect(results.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('respects caseSensitive option', async () => {
    const doc = await loadDocument(textPdfBuffer);
    const page = await doc.getPage(0);
    // case-insensitive (default) should find it
    const insensitive = page.search('hello');
    expect(insensitive.length).toBe(1);
    // case-sensitive should not find lowercase when original is capitalized
    const sensitive = page.search('hello', { caseSensitive: true });
    expect(sensitive.length).toBe(0);
    page.close();
    doc.destroy();
  });
});
