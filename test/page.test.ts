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

  it('has rotation and hasTransparency properties', () => {
    expect(typeof page.rotation).toBe('number');
    expect([0, 1, 2, 3]).toContain(page.rotation);
    expect(typeof page.hasTransparency).toBe('boolean');
  });

  it('returns rotation=1 for a 90° rotated page', async () => {
    const doc2 = await loadDocument(fixture('rotated-page.pdf'));
    const p = await doc2.getPage(0);
    expect(p.rotation).toBe(1);
    p.close();
    doc2.destroy();
  });

  it('returns cropBox and trimBox when set', async () => {
    const doc2 = await loadDocument(fixture('page-boxes.pdf'));
    const p = await doc2.getPage(0);
    expect(p.cropBox).toBeDefined();
    expect(p.cropBox!.left).toBeCloseTo(50, 0);
    expect(p.cropBox!.bottom).toBeCloseTo(50, 0);
    expect(p.cropBox!.right).toBeCloseTo(562, 0);
    expect(p.cropBox!.top).toBeCloseTo(742, 0);
    expect(p.trimBox).toBeDefined();
    expect(p.trimBox!.left).toBeCloseTo(72, 0);
    expect(p.trimBox!.bottom).toBeCloseTo(72, 0);
    expect(p.trimBox!.right).toBeCloseTo(540, 0);
    expect(p.trimBox!.top).toBeCloseTo(720, 0);
    p.close();
    doc2.destroy();
  });

  it('returns undefined cropBox/trimBox when not set', () => {
    // minimal.pdf has no explicit CropBox or TrimBox
    expect(page.cropBox).toBeUndefined();
    expect(page.trimBox).toBeUndefined();
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
        // enriched text object properties
        expect(typeof o.renderMode).toBe('string');
        expect(typeof o.isEmbedded).toBe('boolean');
        if (o.fontFlags !== undefined) expect(typeof o.fontFlags).toBe('number');
        break;
      }
    }
    expect(foundText).toBe(true);

    p.close();
    doc2.destroy();
  });

  it('returns image object enrichments (DPI, colorspace, filters)', async () => {
    const doc2 = await loadDocument(fixture('image.pdf'));
    const p = await doc2.getPage(0);
    let foundImage = false;
    for (let i = 0; i < p.objectCount; i++) {
      const o = await p.getObject(i);
      if (o.type === 'image') {
        foundImage = true;
        expect(o.imageWidth).toBeGreaterThan(0);
        expect(o.imageHeight).toBeGreaterThan(0);
        if (o.horizontalDpi !== undefined) expect(o.horizontalDpi).toBeGreaterThan(0);
        if (o.verticalDpi !== undefined) expect(o.verticalDpi).toBeGreaterThan(0);
        if (o.bitsPerPixel !== undefined) expect(o.bitsPerPixel).toBeGreaterThan(0);
        if (o.colorspace !== undefined) expect(typeof o.colorspace).toBe('string');
        if (o.filters !== undefined) {
          expect(Array.isArray(o.filters)).toBe(true);
          for (const f of o.filters) expect(typeof f).toBe('string');
        }
        break;
      }
    }
    expect(foundImage).toBe(true);
    p.close();
    doc2.destroy();
  });
});

describe('image render', () => {
  async function findImage(p: PDFiumPage) {
    for (let i = 0; i < p.objectCount; i++) {
      const o = await p.getObject(i);
      if (o.type === 'image') return o;
    }
    throw new Error('No image object found');
  }

  it('renders image as PNG buffer by default', async () => {
    const doc2 = await loadDocument(fixture('image.pdf'));
    const p = await doc2.getPage(0);
    const img = await findImage(p);
    const buf = await img.render();
    expect(Buffer.isBuffer(buf)).toBe(true);
    expect(buf.length).toBeGreaterThan(0);
    // PNG magic: 0x89 P N G
    expect(buf[0]).toBe(0x89);
    expect(buf[1]).toBe(0x50);
    p.close();
    doc2.destroy();
  });

  it('renders image as JPEG buffer', async () => {
    const doc2 = await loadDocument(fixture('image.pdf'));
    const p = await doc2.getPage(0);
    const img = await findImage(p);
    const buf = await img.render({ format: 'jpeg', quality: 80 });
    expect(Buffer.isBuffer(buf)).toBe(true);
    expect(buf.length).toBeGreaterThan(0);
    // JPEG magic: 0xFF 0xD8
    expect(buf[0]).toBe(0xff);
    expect(buf[1]).toBe(0xd8);
    p.close();
    doc2.destroy();
  });

  it('renders raw image data', async () => {
    const doc2 = await loadDocument(fixture('image.pdf'));
    const p = await doc2.getPage(0);
    const img = await findImage(p);
    const buf = await img.render({ format: 'raw' });
    expect(Buffer.isBuffer(buf)).toBe(true);
    expect(buf.length).toBeGreaterThan(0);
    p.close();
    doc2.destroy();
  });

  it('writes image to file', async () => {
    const { mkdtemp, rm, readFile } = await import('node:fs/promises');
    const { tmpdir } = await import('node:os');
    const { join } = await import('node:path');
    const dir = await mkdtemp(join(tmpdir(), 'pdfium-'));
    try {
      const doc2 = await loadDocument(fixture('image.pdf'));
      const p = await doc2.getPage(0);
      const img = await findImage(p);
      const outPath = join(dir, 'extracted.png');
      const result = await img.render({ output: outPath });
      expect(result).toBeUndefined();
      const fileData = await readFile(outPath);
      expect(fileData.length).toBeGreaterThan(0);
      expect(fileData[0]).toBe(0x89); // PNG magic
      p.close();
      doc2.destroy();
    } finally {
      await rm(dir, { recursive: true });
    }
  });

  it('renders with mask/matrix applied', async () => {
    const doc2 = await loadDocument(fixture('image.pdf'));
    const p = await doc2.getPage(0);
    const img = await findImage(p);
    const buf = await img.render({ rendered: true });
    expect(Buffer.isBuffer(buf)).toBe(true);
    expect(buf.length).toBeGreaterThan(0);
    p.close();
    doc2.destroy();
  });

  it('render() is not present on non-image objects', async () => {
    const doc2 = await loadDocument(fixture('text.pdf'));
    const p = await doc2.getPage(0);
    let foundText = false;
    for (let i = 0; i < p.objectCount; i++) {
      const o = await p.getObject(i);
      if (o.type === 'text') {
        foundText = true;
        expect('render' in o).toBe(false);
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
    // native methods reject after close
    await expect(page.getText()).rejects.toThrow('Page is closed');
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
