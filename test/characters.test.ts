import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';
import type { PDFiumDocument, PDFiumPage } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// positioned-text.pdf draws three runs at known baselines (see
// scripts/generate-fixtures.mjs):
//   'AB' at (50, 250) size 24 Helvetica
//   'CD' at (50, 200) size 12 Helvetica-Bold
//   'EF' at (200, 100) size 18 Helvetica, rotated 45 degrees
// PDFium synthesizes a CRLF between runs, so the page reads "AB\r\nCD\r\nEF".
const withPositionedText = async (fn: (page: PDFiumPage, doc: PDFiumDocument) => Promise<void>) => {
  const doc = await loadDocument(fixture('positioned-text.pdf'));
  const page = await doc.getPage(0);
  try {
    await fn(page, doc);
  } finally {
    page.close();
    doc.destroy();
  }
};

describe('PDFiumPage.getCharacters', () => {
  it('returns one entry per character, aligned with getText()', async () => {
    await withPositionedText(async (page) => {
      const text = await page.getText();
      const chars = await page.getCharacters();

      expect(chars).toHaveLength(text.length);
      expect(chars.map((c) => c.char).join('')).toBe(text);
      expect(chars.map((c) => c.index)).toEqual([...text].map((_, i) => i));
    });
  });

  it('reports the baseline origin each run was drawn at', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();
      const byChar = (c: string) => chars.find((ch) => ch.char === c)!;

      // exact, because these are the coordinates passed to drawText
      expect(byChar('A').x).toBeCloseTo(50, 5);
      expect(byChar('A').y).toBeCloseTo(250, 5);
      expect(byChar('C').x).toBeCloseTo(50, 5);
      expect(byChar('C').y).toBeCloseTo(200, 5);
      expect(byChar('E').x).toBeCloseTo(200, 5);
      expect(byChar('E').y).toBeCloseTo(100, 5);
    });
  });

  it('reports a tight bounding box that sits on the baseline', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();
      const a = chars.find((c) => c.char === 'A')!;

      expect(a.bounds).toBeDefined();
      // 'A' has no descender, so its box bottom is the baseline
      expect(a.bounds!.bottom).toBeCloseTo(250, 5);
      expect(a.bounds!.top).toBeGreaterThan(a.bounds!.bottom);
      expect(a.bounds!.right).toBeGreaterThan(a.bounds!.left);

      // The box hugs the glyph's ink, so its left edge sits at or right of the
      // pen origin by the font's left side bearing. Deliberately not asserted
      // as an exact value: positioned-text.pdf uses non-embedded Helvetica, so
      // PDFium substitutes a different font per platform and the bearing
      // differs (0 on macOS, ~0.34pt on Linux at 24pt). The origin itself is
      // font-independent and is checked exactly in the test above.
      expect(a.bounds!.left).toBeGreaterThanOrEqual(a.x!);
      expect(a.bounds!.left - a.x!).toBeLessThan(2);

      // a 24pt cap-height glyph is taller than the 12pt one
      const c = chars.find((ch) => ch.char === 'C')!;
      const height = (b: typeof a.bounds) => b!.top - b!.bottom;
      expect(height(a.bounds)).toBeGreaterThan(height(c.bounds));
    });
  });

  it('reports per-character font name and size', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();
      const byChar = (c: string) => chars.find((ch) => ch.char === c)!;

      expect(byChar('A').fontName).toBe('Helvetica');
      expect(byChar('A').fontSize).toBe(24);
      expect(byChar('C').fontName).toBe('Helvetica-Bold');
      expect(byChar('C').fontSize).toBe(12);
      expect(byChar('E').fontName).toBe('Helvetica');
      expect(byChar('E').fontSize).toBe(18);
    });
  });

  it('reports rotation clockwise, so 45 degrees CCW reads as 2pi - pi/4', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();

      expect(chars.find((c) => c.char === 'A')!.angle).toBe(0);
      // PDFium computes atan2(c, a) and normalizes into [0, 2pi)
      expect(chars.find((c) => c.char === 'E')!.angle).toBeCloseTo(2 * Math.PI - Math.PI / 4, 4);
      expect(chars.find((c) => c.char === 'F')!.angle).toBeCloseTo(2 * Math.PI - Math.PI / 4, 4);
    });
  });

  it('flags the line breaks PDFium synthesizes between runs', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();

      const generated = chars.filter((c) => c.isGenerated);
      expect(generated.map((c) => c.char)).toEqual(['\r', '\n', '\r', '\n']);

      // every character actually present in the content stream is not generated
      for (const c of chars.filter((ch) => 'ABCDEF'.includes(ch.char))) {
        expect(c.isGenerated).toBe(false);
      }

      // synthesized characters carry no real font
      expect(generated[0].fontName).toBe('');
      expect(generated[0].fontWeight).toBeUndefined();
    });
  });

  it('exposes the unicode code point alongside the character', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();
      const a = chars.find((c) => c.char === 'A')!;

      expect(a.unicode).toBe(65);
      expect(String.fromCodePoint(a.unicode)).toBe(a.char);
      expect(a.hasUnicodeMapError).toBe(false);
      expect(a.isHyphen).toBe(false);
    });
  });

  it('returns an empty array for a page with no text', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    expect(await page.getCharacters()).toEqual([]);
    page.close();
    doc.destroy();
  });
});

describe('PDFiumPage.getCharacters ranges', () => {
  it('returns only the requested slice', async () => {
    await withPositionedText(async (page) => {
      const all = await page.getCharacters();
      const slice = await page.getCharacters({ start: 4, count: 2 });

      expect(slice.map((c) => c.char)).toEqual(['C', 'D']);
      expect(slice.map((c) => c.index)).toEqual([4, 5]);
      // indices stay absolute, so a slice entry equals its full-list counterpart
      expect(slice[0]).toEqual(all[4]);
    });
  });

  it('runs to the end of the page when count is omitted', async () => {
    await withPositionedText(async (page) => {
      const all = await page.getCharacters();
      const tail = await page.getCharacters({ start: 8 });

      expect(tail.map((c) => c.char)).toEqual(['E', 'F']);
      expect(tail).toEqual(all.slice(8));
    });
  });

  it('clamps a count that runs past the end of the page', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters({ start: 8, count: 1000 });
      expect(chars.map((c) => c.char)).toEqual(['E', 'F']);
    });
  });

  it('returns an empty array for a start past the end, and for count 0', async () => {
    await withPositionedText(async (page) => {
      expect(await page.getCharacters({ start: 10 })).toEqual([]);
      expect(await page.getCharacters({ start: 999 })).toEqual([]);
      expect(await page.getCharacters({ start: 0, count: 0 })).toEqual([]);
    });
  });

  it('rejects a negative or non-integer range', async () => {
    await withPositionedText(async (page) => {
      await expect(page.getCharacters({ start: -1 })).rejects.toThrow(RangeError);
      await expect(page.getCharacters({ start: 0, count: -5 })).rejects.toThrow(RangeError);
      await expect(page.getCharacters({ start: 1.5 })).rejects.toThrow(RangeError);
      await expect(page.getCharacters({ start: Number.NaN })).rejects.toThrow(RangeError);
      // passes Number.isInteger but ToInt32-wraps to 1
      await expect(page.getCharacters({ start: 2 ** 32 + 1 })).rejects.toThrow(RangeError);
    });
  });

  it('rejects after the page is closed', async () => {
    const doc = await loadDocument(fixture('positioned-text.pdf'));
    const page = await doc.getPage(0);
    page.close();
    await expect(page.getCharacters()).rejects.toThrow('Page is closed');
    doc.destroy();
  });
});

describe('PDFiumPage.getCharacters with non-BMP text', () => {
  // astral-text.pdf maps the glyph for 'A' to U+1F600 and 'B' to U+00E9 via a
  // ToUnicode CMap, so the page reads "😀é" — three UTF-16 code units.
  const withAstral = async (fn: (page: PDFiumPage) => Promise<void>) => {
    const doc = await loadDocument(fixture('astral-text.pdf'));
    const page = await doc.getPage(0);
    try {
      await fn(page);
    } finally {
      page.close();
      doc.destroy();
    }
  };

  it('keeps one entry per UTF-16 code unit, matching getText()', async () => {
    await withAstral(async (page) => {
      const text = await page.getText();
      const chars = await page.getCharacters();

      expect(text).toBe('\u{1F600}é');
      // the emoji occupies two indices, so 3 units and 3 entries
      expect(text.length).toBe(3);
      expect(chars).toHaveLength(3);
    });
  });

  it('emits the whole astral character on its first entry, not a replacement char', async () => {
    await withAstral(async (page) => {
      const chars = await page.getCharacters();

      // Regression: each lone surrogate used to be converted separately and
      // V8 replaced it with U+FFFD, corrupting every non-BMP character.
      expect(chars[0].char).toBe('\u{1F600}');
      expect(chars[0].char).not.toContain('�');
      // the second half is an empty continuation entry
      expect(chars[1].char).toBe('');
      expect(chars[2].char).toBe('é');
    });
  });

  it('still reproduces getText() exactly when the chars are concatenated', async () => {
    await withAstral(async (page) => {
      const text = await page.getText();
      const chars = await page.getCharacters();
      expect(chars.map((c) => c.char).join('')).toBe(text);
    });
  });

  it('exposes the raw surrogate code units in unicode', async () => {
    await withAstral(async (page) => {
      const chars = await page.getCharacters();
      // high surrogate, low surrogate, then a plain BMP code point
      expect(chars.map((c) => c.unicode)).toEqual([0xd83d, 0xde00, 0x00e9]);
    });
  });

  it('omits fontWeight when the font declares none', async () => {
    await withPositionedText(async (page) => {
      const chars = await page.getCharacters();
      // Regression: PDFium returns 0 (not -1) for the non-embedded standard
      // fonts, so a meaningless fontWeight: 0 used to be present on every entry.
      for (const c of chars.filter((ch) => 'ABCD'.includes(ch.char))) {
        expect(c.fontWeight).toBeUndefined();
      }
    });
  });
});
