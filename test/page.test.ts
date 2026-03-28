import { resolve } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { loadDocument, PDFiumDocument, PDFiumPage } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('PDFiumPage', () => {
  let doc: PDFiumDocument;
  let page: PDFiumPage;

  beforeAll(async () => {
    doc = await loadDocument(fixture('minimal.pdf'));
    page = await doc.getPage(0);
  });

  afterAll(() => {
    page.close();
    doc.destroy();
  });

  it('has correct page number', () => {
    expect(page.number).toBe(0);
  });

  it('returns width in points', () => {
    expect(page.width).toBeCloseTo(612, 0);
  });

  it('returns height in points', () => {
    expect(page.height).toBeCloseTo(792, 0);
  });

  it('getText returns a string', async () => {
    const text = await page.getText();
    expect(typeof text).toBe('string');
  });

  it('objectCount returns a number', () => {
    expect(typeof page.objectCount).toBe('number');
    expect(page.objectCount).toBeGreaterThanOrEqual(0);
  });

  it('getObject returns type and bounds', async () => {
    const doc2 = await loadDocument(fixture('text.pdf'));
    const p = await doc2.getPage(0);
    const count = p.objectCount;
    expect(count).toBeGreaterThan(0);
    const obj = await p.getObject(0);
    expect(obj).toHaveProperty('type');
    expect(obj).toHaveProperty('bounds');
    expect(['text', 'path', 'image', 'shading', 'form', 'unknown']).toContain(obj.type);
    expect(typeof obj.bounds.left).toBe('number');
    expect(typeof obj.bounds.bottom).toBe('number');
    expect(typeof obj.bounds.right).toBe('number');
    expect(typeof obj.bounds.top).toBe('number');

    // colors are present for all objects
    expect(obj).toHaveProperty('fillColor');
    expect(obj).toHaveProperty('strokeColor');

    // find the text object and verify enriched properties
    let foundText = false;
    for (let i = 0; i < count; i++) {
      const o = await p.getObject(i);
      if (o.type === 'text') {
        foundText = true;
        expect(o.text).toBe('Hello');
        expect(o.fontSize).toBeCloseTo(12, 0);
        expect(o.fontName).toBe('Helvetica');
        expect(typeof o.fontWeight).toBe('number');
        expect(typeof o.italicAngle).toBe('number');
        break;
      }
    }
    expect(foundText).toBe(true);

    p.close();
    doc2.destroy();
  });
});

describe('multi-page dimensions', () => {
  it('each page has its own dimensions', async () => {
    const doc = await loadDocument(fixture('two-page.pdf'));

    const page0 = await doc.getPage(0);
    expect(page0.width).toBeCloseTo(612, 0);
    expect(page0.height).toBeCloseTo(792, 0);
    page0.close();

    const page1 = await doc.getPage(1);
    expect(page1.width).toBeCloseTo(400, 0);
    expect(page1.height).toBeCloseTo(600, 0);
    page1.close();

    doc.destroy();
  });
});

describe('page close', () => {
  it('close prevents further method calls', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    page.close();
    // width/height are cached — still accessible after close
    expect(page.width).toBeCloseTo(612, 0);
    expect(page.height).toBeCloseTo(792, 0);
    // native methods throw synchronously after close (before async worker is created)
    expect(() => page.getText()).toThrow('Page is closed');
    doc.destroy();
  });

  it('double close does not throw', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    page.close();
    expect(() => page.close()).not.toThrow();
    doc.destroy();
  });
});
