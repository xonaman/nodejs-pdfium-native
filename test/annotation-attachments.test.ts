import { readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

// Exact bytes embedded as report.txt (in a FileAttachment annotation) by
// scripts/generate-fixtures.mjs. FlateDecode is lossless, so extraction must
// round-trip these bytes exactly.
const REPORT_TXT = 'Quarterly report attachment.\nSecond line.\n';

const tempFiles: string[] = [];
afterAll(async () => {
  const { rm } = await import('node:fs/promises');
  await Promise.all(tempFiles.map((f) => rm(f, { force: true })));
});

describe('PDFiumPage.getAnnotations (file attachments)', () => {
  it('surfaces the file name and stable index of a FileAttachment annotation', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);

    const annotations = await page.getAnnotations();
    expect(annotations).toHaveLength(2);

    // fixture lays out [Text, FileAttachment], so indices are 0 and 1
    const text = annotations.find((a) => a.type === 'text');
    expect(text?.index).toBe(0);
    expect(text?.fileName).toBeUndefined();

    const file = annotations.find((a) => a.type === 'fileattachment');
    expect(file).toBeDefined();
    expect(file!.index).toBe(1);
    expect(file!.fileName).toBe('report.txt');

    page.close();
    doc.destroy();
  });
});

describe('PDFiumPage.getAnnotationAttachment', () => {
  const openFileAnnot = async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    const annotations = await page.getAnnotations();
    const file = annotations.find((a) => a.type === 'fileattachment');
    if (!file) throw new Error('file-attachment annotation not found');
    return { doc, page, index: file.index };
  };

  it('reads the embedded file bytes exactly', async () => {
    const { doc, page, index } = await openFileAnnot();

    const bytes = await page.getAnnotationAttachment(index);
    expect(Buffer.isBuffer(bytes)).toBe(true);
    // exact-byte recovery, not just a substring match
    expect(bytes.equals(Buffer.from(REPORT_TXT, 'utf8'))).toBe(true);

    page.close();
    doc.destroy();
  });

  it('writes the embedded file to a path when output is given', async () => {
    const { doc, page, index } = await openFileAnnot();

    const out = join(tmpdir(), `pdfium-annot-attachment-${process.pid}-${index}.txt`);
    tempFiles.push(out);

    const result = await page.getAnnotationAttachment(index, { output: out });
    expect(result).toBeUndefined();

    const onDisk = readFileSync(out);
    const inMemory = await page.getAnnotationAttachment(index);
    expect(onDisk.equals(inMemory)).toBe(true);

    page.close();
    doc.destroy();
  });

  it('rejects when the annotation at index is not a file attachment', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    // index 0 is the plain Text annotation
    await expect(page.getAnnotationAttachment(0)).rejects.toThrow('not a file attachment');
    page.close();
    doc.destroy();
  });

  it('rejects for an out-of-range index', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    await expect(page.getAnnotationAttachment(99)).rejects.toThrow('out of range');
    await expect(page.getAnnotationAttachment(-1)).rejects.toThrow('out of range');
    page.close();
    doc.destroy();
  });

  it('rejects a non-integer index instead of silently truncating', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    await expect(page.getAnnotationAttachment(Number.NaN)).rejects.toThrow(RangeError);
    await expect(page.getAnnotationAttachment(1.5)).rejects.toThrow(RangeError);
    page.close();
    doc.destroy();
  });

  it('rejects an out-of-32-bit index that ToInt32 would wrap onto a real annotation', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    // 2**32 + 1 passes Number.isInteger but ToInt32-wraps to 1 (the file
    // attachment); it must reject, not silently resolve the wrong file.
    await expect(page.getAnnotationAttachment(2 ** 32 + 1)).rejects.toThrow(RangeError);
    page.close();
    doc.destroy();
  });

  it('rejects after the page is closed', async () => {
    const doc = await loadDocument(fixture('file-attachment-annotation.pdf'));
    const page = await doc.getPage(0);
    page.close();
    await expect(page.getAnnotationAttachment(1)).rejects.toThrow('Page is closed');
    doc.destroy();
  });
});
