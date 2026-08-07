import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { assemblePages, loadDocument, nUpPages } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

// four-page.pdf carries "Page 1".."Page 4" as text, one per page, so the page
// order of an assembled document can be read back rather than assumed.
const readPageLabels = async (pdf: Buffer): Promise<string[]> => {
  const doc = await loadDocument(pdf);
  const labels: string[] = [];
  for (let i = 0; i < doc.pageCount; i++) {
    const page = await doc.getPage(i);
    labels.push((await page.getText()).trim());
    page.close();
  }
  doc.destroy();
  return labels;
};

describe('assemblePages', () => {
  it('selects and reorders pages', async () => {
    const result = await assemblePages(fixture('four-page.pdf'), [3, 1, 0]);
    expect(await readPageLabels(result)).toEqual(['Page 4', 'Page 2', 'Page 1']);
  });

  it('repeats a page when its index is given more than once', async () => {
    const result = await assemblePages(fixture('four-page.pdf'), [2, 2, 2]);
    expect(await readPageLabels(result)).toEqual(['Page 3', 'Page 3', 'Page 3']);
  });

  it('extracts a single page', async () => {
    const result = await assemblePages(fixture('four-page.pdf'), [1]);
    expect(await readPageLabels(result)).toEqual(['Page 2']);
  });

  it('accepts a Buffer as input', async () => {
    const buf = readFileSync(fixture('four-page.pdf'));
    const result = await assemblePages(buf, [0, 3]);
    expect(await readPageLabels(result)).toEqual(['Page 1', 'Page 4']);
  });

  it('writes to a file when output is given', async () => {
    const out = join(tmpdir(), `pdfium-assemble-${process.pid}.pdf`);
    tempFiles.push(out);

    const result = await assemblePages(fixture('four-page.pdf'), [1, 0], { output: out });
    expect(result).toBeUndefined();
    expect(await readPageLabels(readFileSync(out))).toEqual(['Page 2', 'Page 1']);
  });

  it('rejects an out-of-range page index, naming it', async () => {
    await expect(assemblePages(fixture('four-page.pdf'), [4])).rejects.toThrow(
      'Page index out of range: 4',
    );
    await expect(assemblePages(fixture('four-page.pdf'), [-1])).rejects.toThrow(
      'Page index out of range: -1',
    );
  });

  it('rejects an empty page list', async () => {
    await expect(assemblePages(fixture('four-page.pdf'), [])).rejects.toThrow();
  });

  it('rejects a non-integer index instead of silently truncating', async () => {
    await expect(assemblePages(fixture('four-page.pdf'), [1.5])).rejects.toThrow(RangeError);
    await expect(assemblePages(fixture('four-page.pdf'), [Number.NaN])).rejects.toThrow(RangeError);
    // passes Number.isInteger but ToInt32-wraps to 1, silently assembling page 2
    await expect(assemblePages(fixture('four-page.pdf'), [2 ** 32 + 1])).rejects.toThrow(
      RangeError,
    );
  });
});

describe('nUpPages', () => {
  it('places columns x rows source pages on each output page', async () => {
    // 4 source pages, 2x2 per sheet -> 1 sheet
    const result = await nUpPages(fixture('four-page.pdf'), { columns: 2, rows: 2 });
    const doc = await loadDocument(result);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('rounds partial sheets up', async () => {
    // 4 source pages, 3 per sheet -> 2 sheets, the second only partly filled
    const result = await nUpPages(fixture('four-page.pdf'), { columns: 3, rows: 1 });
    const doc = await loadDocument(result);
    expect(doc.pageCount).toBe(2);
    doc.destroy();
  });

  it('defaults the sheet size to the first source page', async () => {
    // four-page.pdf pages are 300x400
    const result = await nUpPages(fixture('four-page.pdf'), { columns: 2, rows: 2 });
    const doc = await loadDocument(result);
    const page = await doc.getPage(0);
    expect(page.width).toBeCloseTo(300, 1);
    expect(page.height).toBeCloseTo(400, 1);
    page.close();
    doc.destroy();
  });

  it('honours an explicit sheet size', async () => {
    const result = await nUpPages(fixture('four-page.pdf'), {
      columns: 2,
      rows: 2,
      width: 842,
      height: 595,
    });
    const doc = await loadDocument(result);
    const page = await doc.getPage(0);
    expect(page.width).toBeCloseTo(842, 1);
    expect(page.height).toBeCloseTo(595, 1);
    page.close();
    doc.destroy();
  });

  it('keeps the source content — a 1x1 layout is a straight copy', async () => {
    const result = await nUpPages(fixture('four-page.pdf'), { columns: 1, rows: 1 });
    expect(await readPageLabels(result)).toEqual(['Page 1', 'Page 2', 'Page 3', 'Page 4']);
  });

  it('accepts a Buffer as input', async () => {
    const buf = readFileSync(fixture('four-page.pdf'));
    const result = await nUpPages(buf, { columns: 2, rows: 2 });
    const doc = await loadDocument(result);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('writes to a file when output is given', async () => {
    const out = join(tmpdir(), `pdfium-nup-${process.pid}.pdf`);
    tempFiles.push(out);

    const result = await nUpPages(fixture('four-page.pdf'), {
      columns: 2,
      rows: 2,
      output: out,
    });
    expect(result).toBeUndefined();

    const doc = await loadDocument(readFileSync(out));
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('rejects a non-positive layout', async () => {
    await expect(nUpPages(fixture('four-page.pdf'), { columns: 0, rows: 2 })).rejects.toThrow(
      RangeError,
    );
    await expect(nUpPages(fixture('four-page.pdf'), { columns: 2, rows: -1 })).rejects.toThrow(
      RangeError,
    );
  });
});

describe('nUpPages layout validation', () => {
  it('rejects a layout outside the 32-bit range instead of wrapping it', async () => {
    // Regression: Number.isInteger alone let 2**32 + 2 through, and the native
    // ToInt32 coercion wrapped it to 2 — silently producing a valid but
    // completely different layout.
    await expect(
      nUpPages(fixture('four-page.pdf'), { columns: 2 ** 32 + 2, rows: 1 }),
    ).rejects.toThrow(RangeError);
    await expect(
      nUpPages(fixture('four-page.pdf'), { columns: 1, rows: 2 ** 32 + 3 }),
    ).rejects.toThrow(RangeError);
    await expect(nUpPages(fixture('four-page.pdf'), { columns: 1.5, rows: 1 })).rejects.toThrow(
      RangeError,
    );
  });
});
