import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { flattenDocument, loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

const annotationCount = async (pdf: Buffer, pageIndex = 0): Promise<number> => {
  const doc = await loadDocument(pdf);
  const page = await doc.getPage(pageIndex);
  const count = (await page.getAnnotations()).length;
  page.close();
  doc.destroy();
  return count;
};

describe('flattenDocument', () => {
  it('removes annotations by merging them into the page content', async () => {
    const before = readFileSync(fixture('annotations.pdf'));
    expect(await annotationCount(before)).toBeGreaterThan(0);

    const after = await flattenDocument(before);
    expect(await annotationCount(after)).toBe(0);
  });

  it('leaves a page without annotations intact', async () => {
    // FLATTEN_NOTHINGTODO is a normal outcome, not an error
    const flattened = await flattenDocument(fixture('minimal.pdf'));
    const doc = await loadDocument(flattened);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('preserves page count and text content', async () => {
    const flattened = await flattenDocument(fixture('annotations.pdf'));
    const doc = await loadDocument(flattened);
    expect(doc.pageCount).toBe(1);

    const page = await doc.getPage(0);
    expect(await page.getText()).toContain('Page with annotations');
    page.close();
    doc.destroy();
  });

  it('flattens form widgets, so the fields are gone afterwards', async () => {
    const doc = await loadDocument(fixture('form-fields.pdf'));
    const page = await doc.getPage(0);
    expect((await page.getFormFields()).length).toBeGreaterThan(0);
    page.close();
    doc.destroy();

    const flattened = await flattenDocument(fixture('form-fields.pdf'));
    const after = await loadDocument(flattened);
    const afterPage = await after.getPage(0);
    expect(await afterPage.getFormFields()).toHaveLength(0);
    afterPage.close();
    after.destroy();
  });

  it('flattens only the requested pages', async () => {
    // links.pdf has link annotations on page 0 and none on page 1
    const before = readFileSync(fixture('links.pdf'));
    expect(await annotationCount(before, 0)).toBeGreaterThan(0);

    // asking for page 1 only must leave page 0 untouched
    const flattened = await flattenDocument(before, { pages: [1] });
    expect(await annotationCount(flattened, 0)).toBeGreaterThan(0);
  });

  it('accepts the print usage flag', async () => {
    const flattened = await flattenDocument(fixture('annotations.pdf'), { usage: 'print' });
    expect(await annotationCount(flattened)).toBe(0);
  });

  it('writes to a file when output is given', async () => {
    const out = join(tmpdir(), `pdfium-flatten-${process.pid}.pdf`);
    tempFiles.push(out);

    const result = await flattenDocument(fixture('annotations.pdf'), { output: out });
    expect(result).toBeUndefined();
    expect(await annotationCount(readFileSync(out))).toBe(0);
  });

  it('rejects an unknown usage value', async () => {
    await expect(
      // @ts-expect-error deliberately invalid usage
      flattenDocument(fixture('annotations.pdf'), { usage: 'screen' }),
    ).rejects.toThrow();
  });

  it('rejects an out-of-range page index, naming it', async () => {
    await expect(flattenDocument(fixture('minimal.pdf'), { pages: [3] })).rejects.toThrow(
      'Page index out of range: 3',
    );
  });

  it('rejects a non-integer page index instead of silently truncating', async () => {
    await expect(flattenDocument(fixture('minimal.pdf'), { pages: [1.5] })).rejects.toThrow(
      RangeError,
    );
    await expect(flattenDocument(fixture('minimal.pdf'), { pages: [2 ** 32 + 1] })).rejects.toThrow(
      RangeError,
    );
  });
});
