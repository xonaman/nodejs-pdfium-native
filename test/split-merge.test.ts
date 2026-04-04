import { readFileSync, unlinkSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument, mergeDocuments, splitDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('splitDocument', () => {
  it('splits a two-page PDF into two single-page documents', async () => {
    const [part1, part2] = (await splitDocument(fixture('two-page.pdf'), [1])) as Buffer[];
    const doc1 = await loadDocument(part1);
    const doc2 = await loadDocument(part2);
    expect(doc1.pageCount).toBe(1);
    expect(doc2.pageCount).toBe(1);
    doc1.destroy();
    doc2.destroy();
  });

  it('returns a single document when splitAt is empty', async () => {
    const result = (await splitDocument(fixture('two-page.pdf'), [])) as Buffer[];
    expect(result).toHaveLength(1);
    const doc = await loadDocument(result[0]);
    expect(doc.pageCount).toBe(2);
    doc.destroy();
  });

  it('works with a Buffer input', async () => {
    const buf = readFileSync(fixture('two-page.pdf'));
    const [part1, part2] = (await splitDocument(buf, [1])) as Buffer[];
    expect(part1).toBeInstanceOf(Buffer);
    expect(part2).toBeInstanceOf(Buffer);
    const doc1 = await loadDocument(part1);
    expect(doc1.pageCount).toBe(1);
    doc1.destroy();
  });

  it('writes to files when outputs are specified', async () => {
    const out1 = resolve(import.meta.dirname!, 'fixtures', '_split-1.pdf');
    const out2 = resolve(import.meta.dirname!, 'fixtures', '_split-2.pdf');
    const result = await splitDocument(fixture('two-page.pdf'), [1], { outputs: [out1, out2] });
    expect(result).toBeUndefined();
    const doc1 = await loadDocument(out1);
    const doc2 = await loadDocument(out2);
    expect(doc1.pageCount).toBe(1);
    expect(doc2.pageCount).toBe(1);
    doc1.destroy();
    doc2.destroy();
    unlinkSync(out1);
    unlinkSync(out2);
  });

  it('rejects for out-of-range split index', async () => {
    await expect(splitDocument(fixture('two-page.pdf'), [5])).rejects.toThrow();
  });

  it('rejects for split index at 0', async () => {
    await expect(splitDocument(fixture('two-page.pdf'), [0])).rejects.toThrow();
  });

  it('rejects for unsorted split indices', async () => {
    await expect(splitDocument(fixture('minimal.pdf'), [2, 1])).rejects.toThrow();
  });

  it('rejects for invalid input', async () => {
    await expect(splitDocument('nonexistent.pdf', [1])).rejects.toThrow();
  });
});

describe('mergeDocuments', () => {
  it('merges two PDFs', async () => {
    const result = await mergeDocuments([fixture('minimal.pdf'), fixture('two-page.pdf')]);
    expect(result).toBeInstanceOf(Buffer);
    const doc = await loadDocument(result as Buffer);
    expect(doc.pageCount).toBe(3); // 1 + 2
    doc.destroy();
  });

  it('merges Buffer and file path inputs', async () => {
    const buf = readFileSync(fixture('minimal.pdf'));
    const result = await mergeDocuments([buf, fixture('two-page.pdf')]);
    expect(result).toBeInstanceOf(Buffer);
    const doc = await loadDocument(result as Buffer);
    expect(doc.pageCount).toBe(3);
    doc.destroy();
  });

  it('merges with object inputs containing passwords', async () => {
    const result = await mergeDocuments([
      { input: fixture('minimal.pdf') },
      { input: fixture('two-page.pdf') },
    ]);
    expect(result).toBeInstanceOf(Buffer);
    const doc = await loadDocument(result as Buffer);
    expect(doc.pageCount).toBe(3);
    doc.destroy();
  });

  it('writes to a file when output is specified', async () => {
    const outPath = resolve(import.meta.dirname!, 'fixtures', '_merge-output.pdf');
    const result = await mergeDocuments([fixture('minimal.pdf'), fixture('two-page.pdf')], {
      output: outPath,
    });
    expect(result).toBeUndefined();
    const doc = await loadDocument(outPath);
    expect(doc.pageCount).toBe(3);
    doc.destroy();
    const { unlinkSync } = await import('node:fs');
    unlinkSync(outPath);
  });

  it('rejects for empty inputs array', async () => {
    await expect(mergeDocuments([])).rejects.toThrow();
  });

  it('rejects for invalid document in inputs', async () => {
    await expect(mergeDocuments(['nonexistent.pdf'])).rejects.toThrow();
  });
});
