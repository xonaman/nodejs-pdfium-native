import { existsSync, readFileSync, unlinkSync } from 'node:fs';
import { resolve } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { loadDocument, PDFiumDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('PDFiumPage.render', () => {
  let doc: PDFiumDocument;

  beforeAll(async () => {
    doc = await loadDocument(fixture('minimal.pdf'));
  });

  afterAll(() => {
    doc.destroy();
  });

  it('renders as JPEG by default', async () => {
    const page = await doc.getPage(0);
    const buf = await page.render();
    expect(buf).toBeInstanceOf(Buffer);
    expect(buf.length).toBeGreaterThan(0);
    // JPEG magic bytes: FF D8 FF
    expect(buf[0]).toBe(0xff);
    expect(buf[1]).toBe(0xd8);
    expect(buf[2]).toBe(0xff);
    page.close();
  });

  it('renders as PNG when format is png', async () => {
    const page = await doc.getPage(0);
    const buf = await page.render({ format: 'png' });
    expect(buf).toBeInstanceOf(Buffer);
    // PNG magic bytes: 89 50 4E 47
    expect(buf[0]).toBe(0x89);
    expect(buf[1]).toBe(0x50);
    expect(buf[2]).toBe(0x4e);
    expect(buf[3]).toBe(0x47);
    page.close();
  });

  it('respects quality option for JPEG', async () => {
    const page = await doc.getPage(0);
    const high = await page.render({ quality: 100 });
    const low = await page.render({ quality: 10 });
    expect(low.length).toBeLessThan(high.length);
    page.close();
  });

  it('renders with scale=2', async () => {
    const page = await doc.getPage(0);
    const buf = await page.render({ scale: 2 });
    expect(buf).toBeInstanceOf(Buffer);
    expect(buf.length).toBeGreaterThan(0);
    page.close();
  });

  it('renders with explicit width/height', async () => {
    const page = await doc.getPage(0);
    const buf = await page.render({ width: 100, height: 200 });
    expect(buf).toBeInstanceOf(Buffer);
    expect(buf.length).toBeGreaterThan(0);
    page.close();
  });

  it('writes JPEG to file when output is specified', async () => {
    const page = await doc.getPage(0);
    const outPath = resolve(import.meta.dirname!, '__test_output.jpg');
    try {
      const result = await page.render({ output: outPath });
      expect(result).toBeUndefined();
      expect(existsSync(outPath)).toBe(true);
      const bytes = readFileSync(outPath);
      expect(bytes[0]).toBe(0xff);
      expect(bytes[1]).toBe(0xd8);
      expect(bytes[2]).toBe(0xff);
    } finally {
      page.close();
      try {
        unlinkSync(outPath);
      } catch {}
    }
  });

  it('writes PNG to file when output and format=png', async () => {
    const page = await doc.getPage(0);
    const outPath = resolve(import.meta.dirname!, '__test_output.png');
    try {
      const result = await page.render({ output: outPath, format: 'png' });
      expect(result).toBeUndefined();
      const bytes = readFileSync(outPath);
      expect(bytes[0]).toBe(0x89);
      expect(bytes[1]).toBe(0x50);
      expect(bytes[2]).toBe(0x4e);
      expect(bytes[3]).toBe(0x47);
    } finally {
      page.close();
      try {
        unlinkSync(outPath);
      } catch {}
    }
  });
});
