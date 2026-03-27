import { existsSync, readFileSync, unlinkSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  loadDocument,
  PDFiumDocument,
  PDFiumPage,
  PDFiumError,
  PDFiumFileError,
  PDFiumFormatError,
} from './index.js';

// minimal valid PDF: single page (612x792 pts), no content
const MINIMAL_PDF = `%PDF-1.0
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj
3 0 obj<</Type/Page/MediaBox[0 0 612 792]/Parent 2 0 R/Resources<<>>>>endobj
xref
0 4
0000000000 65535 f 
0000000009 00000 n 
0000000058 00000 n 
0000000115 00000 n 
trailer<</Size 4/Root 1 0 R>>
startxref
206
%%EOF`;

// 2-page PDF
const TWO_PAGE_PDF = `%PDF-1.0
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2>>endobj
3 0 obj<</Type/Page/MediaBox[0 0 612 792]/Parent 2 0 R/Resources<<>>>>endobj
4 0 obj<</Type/Page/MediaBox[0 0 400 600]/Parent 2 0 R/Resources<<>>>>endobj
xref
0 5
0000000000 65535 f 
0000000009 00000 n 
0000000058 00000 n 
0000000115 00000 n 
0000000206 00000 n 
trailer<</Size 5/Root 1 0 R>>
startxref
297
%%EOF`;

const pdfBuffer = Buffer.from(MINIMAL_PDF);
const twoPagesBuffer = Buffer.from(TWO_PAGE_PDF);
const tmpFilePath = resolve(import.meta.dirname!, 'test-fixture.pdf');

beforeAll(() => {
  writeFileSync(tmpFilePath, pdfBuffer);
});

afterAll(() => {
  try {
    unlinkSync(tmpFilePath);
  } catch {
    // ignore
  }
});

describe('loadDocument', () => {
  it('loads a PDF from a Buffer', async () => {
    const doc = await loadDocument(pdfBuffer);
    expect(doc).toBeInstanceOf(PDFiumDocument);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('loads a PDF from a file path', async () => {
    const doc = await loadDocument(tmpFilePath);
    expect(doc).toBeInstanceOf(PDFiumDocument);
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });

  it('rejects with PDFiumFormatError for invalid data', async () => {
    await expect(loadDocument(Buffer.from('not a pdf'))).rejects.toThrow(PDFiumFormatError);
    try {
      await loadDocument(Buffer.from('not a pdf'));
    } catch (err) {
      expect(err).toBeInstanceOf(PDFiumError);
      expect((err as PDFiumError).code).toBe('FORMAT');
    }
  });

  it('rejects with PDFiumFileError for non-existent file', async () => {
    await expect(loadDocument('/tmp/does-not-exist.pdf')).rejects.toThrow(PDFiumFileError);
    try {
      await loadDocument('/tmp/does-not-exist.pdf');
    } catch (err) {
      expect(err).toBeInstanceOf(PDFiumError);
      expect((err as PDFiumError).code).toBe('FILE');
    }
  });

  it('rejects with an error for wrong password', async () => {
    // since our test PDF is not encrypted, this just opens normally
    const doc = await loadDocument(pdfBuffer, 'unused-password');
    expect(doc.pageCount).toBe(1);
    doc.destroy();
  });
});

describe('PDFiumDocument', () => {
  it('returns correct page count', async () => {
    const doc = await loadDocument(twoPagesBuffer);
    expect(doc.pageCount).toBe(2);
    doc.destroy();
  });

  it('getPage returns a PDFiumPage', async () => {
    const doc = await loadDocument(pdfBuffer);
    const page = await doc.getPage(0);
    expect(page).toBeInstanceOf(PDFiumPage);
    page.close();
    doc.destroy();
  });

  it('getPage rejects for out-of-range index', async () => {
    const doc = await loadDocument(pdfBuffer);
    await expect(doc.getPage(5)).rejects.toThrow();
    await expect(doc.getPage(-1)).rejects.toThrow();
    doc.destroy();
  });

  it('destroy prevents further use', async () => {
    const doc = await loadDocument(pdfBuffer);
    doc.destroy();
    await expect(doc.getPage(0)).rejects.toThrow('Document is destroyed');
  });

  it('pages() iterates over all pages', async () => {
    const doc = await loadDocument(twoPagesBuffer);
    const pages: PDFiumPage[] = [];
    for await (const page of doc.pages()) {
      pages.push(page);
    }
    expect(pages).toHaveLength(2);
    expect(pages[0]!.number).toBe(0);
    expect(pages[1]!.number).toBe(1);
    for (const p of pages) p.close();
    doc.destroy();
  });
});

describe('PDFiumPage', () => {
  let doc: PDFiumDocument;
  let page: PDFiumPage;

  beforeAll(async () => {
    doc = await loadDocument(pdfBuffer);
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

  it('size returns { width, height }', () => {
    expect(page.size).toHaveProperty('width');
    expect(page.size).toHaveProperty('height');
    expect(page.size.width).toBeCloseTo(612, 0);
    expect(page.size.height).toBeCloseTo(792, 0);
  });

  it('getText returns a string', () => {
    const text = page.getText();
    expect(typeof text).toBe('string');
  });

  it('objectCount returns a number', () => {
    expect(typeof page.objectCount).toBe('number');
    expect(page.objectCount).toBeGreaterThanOrEqual(0);
  });

  it('getObject returns type and bounds', async () => {
    // use a PDF with at least one object by rendering text via a content stream
    const textPdf = `%PDF-1.4
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj
3 0 obj<</Type/Page/MediaBox[0 0 612 792]/Parent 2 0 R/Resources<</Font<</F1 4 0 R>>>>>/Contents 5 0 R>>endobj
4 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj
5 0 obj<</Length 44>>stream
BT /F1 12 Tf 100 700 Td (Hello) Tj ET
endstream
endobj
xref
0 6
0000000000 65535 f \r
0000000009 00000 n \r
0000000058 00000 n \r
0000000115 00000 n \r
0000000266 00000 n \r
0000000340 00000 n \r
trailer<</Size 6/Root 1 0 R>>
startxref
434
%%EOF`;
    const doc2 = await loadDocument(Buffer.from(textPdf));
    const p = await doc2.getPage(0);
    const count = p.objectCount;
    expect(count).toBeGreaterThan(0);
    const obj = p.getObject(0);
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
      const o = p.getObject(i);
      if (o.type === 'text') {
        foundText = true;
        expect(o.text).toBe('Hello');
        expect(o.fontSize).toBeCloseTo(12, 0);
        expect(o.fontName).toBe('Helvetica');
        break;
      }
    }
    expect(foundText).toBe(true);

    p.close();
    doc2.destroy();
  });
});

describe('PDFiumPage.render', () => {
  let doc: PDFiumDocument;

  beforeAll(async () => {
    doc = await loadDocument(pdfBuffer);
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
    const outPath = resolve(import.meta.dirname, '__test_output.jpg');
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
    const outPath = resolve(import.meta.dirname, '__test_output.png');
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

describe('multi-page dimensions', () => {
  it('each page has its own dimensions', async () => {
    const doc = await loadDocument(twoPagesBuffer);

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
    const doc = await loadDocument(pdfBuffer);
    const page = await doc.getPage(0);
    page.close();
    // width/height are cached — still accessible after close
    expect(page.width).toBeCloseTo(612, 0);
    expect(page.height).toBeCloseTo(792, 0);
    // native methods throw after close
    expect(() => page.getText()).toThrow('Page is closed');
    doc.destroy();
  });

  it('double close does not throw', async () => {
    const doc = await loadDocument(pdfBuffer);
    const page = await doc.getPage(0);
    page.close();
    expect(() => page.close()).not.toThrow();
    doc.destroy();
  });
});

// PDF with text content (used by multiple test suites)
const TEXT_PDF = `%PDF-1.4
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj
2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj
3 0 obj<</Type/Page/MediaBox[0 0 612 792]/Parent 2 0 R/Resources<</Font<</F1 4 0 R>>>>>/Contents 5 0 R>>endobj
4 0 obj<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>endobj
5 0 obj<</Length 44>>stream
BT /F1 12 Tf 100 700 Td (Hello) Tj ET
endstream
endobj
xref
0 6
0000000000 65535 f \r
0000000009 00000 n \r
0000000058 00000 n \r
0000000115 00000 n \r
0000000266 00000 n \r
0000000340 00000 n \r
trailer<</Size 6/Root 1 0 R>>
startxref
434
%%EOF`;

describe('PDFiumDocument.getMetadata', () => {
  it('returns metadata object with expected keys', async () => {
    const doc = await loadDocument(pdfBuffer);
    const meta = doc.metadata;
    expect(meta).toHaveProperty('title');
    expect(meta).toHaveProperty('author');
    expect(meta).toHaveProperty('subject');
    expect(meta).toHaveProperty('keywords');
    expect(meta).toHaveProperty('creator');
    expect(meta).toHaveProperty('producer');
    expect(meta).toHaveProperty('creationDate');
    expect(meta).toHaveProperty('modDate');
    expect(meta).toHaveProperty('pdfVersion');
    expect(typeof meta.title).toBe('string');
    expect(typeof meta.pdfVersion).toBe('number');
    doc.destroy();
  });

  it('returns correct metadata values from a rich PDF', async () => {
    const doc = await loadDocument(
      resolve(import.meta.dirname!, '..', 'test', 'fixtures', 'metadata.pdf'),
    );
    const meta = doc.metadata;
    expect(meta.title).toBe('Test Document Title');
    expect(meta.author).toBe('Test Author');
    expect(meta.subject).toBe('Test Subject');
    expect(meta.creator).toBe('pdfium-native test');
    expect(meta.producer).toContain('pdf-lib');
    doc.destroy();
  });
});

describe('PDFiumDocument.getBookmarks', () => {
  it('returns an empty array for a PDF without bookmarks', async () => {
    const doc = await loadDocument(pdfBuffer);
    const bookmarks = doc.getBookmarks();
    expect(Array.isArray(bookmarks)).toBe(true);
    expect(bookmarks.length).toBe(0);
    doc.destroy();
  });

  it('returns bookmarks with titles and page indices', async () => {
    const doc = await loadDocument(
      resolve(import.meta.dirname!, '..', 'test', 'fixtures', 'bookmarks.pdf'),
    );
    expect(doc.pageCount).toBe(3);
    const bookmarks = doc.getBookmarks();
    expect(bookmarks.length).toBe(3);
    expect(bookmarks[0]!.title).toBe('Chapter 1');
    expect(bookmarks[0]!.pageIndex).toBe(0);
    expect(bookmarks[1]!.title).toBe('Chapter 2');
    expect(bookmarks[1]!.pageIndex).toBe(1);
    expect(bookmarks[2]!.title).toBe('Chapter 3');
    expect(bookmarks[2]!.pageIndex).toBe(2);
    doc.destroy();
  });
});

describe('PDFiumPage.getLinks', () => {
  it('returns an empty array for a page with no links', async () => {
    const doc = await loadDocument(pdfBuffer);
    const page = await doc.getPage(0);
    const links = page.getLinks();
    expect(Array.isArray(links)).toBe(true);
    expect(links.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('returns links with URLs and page indices', async () => {
    const doc = await loadDocument(
      resolve(import.meta.dirname!, '..', 'test', 'fixtures', 'links.pdf'),
    );
    const page = await doc.getPage(0);
    const links = page.getLinks();
    expect(links.length).toBe(2);

    // internal link (to page 2)
    const internalLink = links.find((l) => l.pageIndex !== undefined);
    expect(internalLink).toBeDefined();
    expect(internalLink!.pageIndex).toBe(1);
    expect(internalLink!.bounds).toBeDefined();

    // external link (URL)
    const externalLink = links.find((l) => l.url !== undefined);
    expect(externalLink).toBeDefined();
    expect(externalLink!.url).toBe('https://example.com');
    expect(externalLink!.bounds).toBeDefined();

    page.close();
    doc.destroy();
  });
});

describe('PDFiumPage.search', () => {
  it('finds text occurrences with positions', async () => {
    const doc = await loadDocument(Buffer.from(TEXT_PDF));
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
    const doc = await loadDocument(Buffer.from(TEXT_PDF));
    const page = await doc.getPage(0);
    const results = page.search('xyz-no-match');
    expect(results.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('respects caseSensitive option', async () => {
    const doc = await loadDocument(Buffer.from(TEXT_PDF));
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

describe('PDFiumPage.getAnnotations', () => {
  it('returns an empty array for a page with no annotations', async () => {
    const doc = await loadDocument(pdfBuffer);
    const page = await doc.getPage(0);
    const annotations = page.getAnnotations();
    expect(Array.isArray(annotations)).toBe(true);
    expect(annotations.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('returns annotations with types, bounds, and contents', async () => {
    const doc = await loadDocument(
      resolve(import.meta.dirname!, '..', 'test', 'fixtures', 'annotations.pdf'),
    );
    const page = await doc.getPage(0);
    const annotations = page.getAnnotations();
    expect(annotations.length).toBe(2);

    // text annotation (sticky note)
    const textAnnot = annotations.find((a) => a.type === 'text');
    expect(textAnnot).toBeDefined();
    expect(textAnnot!.contents).toBe('This is a sticky note');
    expect(textAnnot!.bounds).toBeDefined();
    expect(textAnnot!.color).toBeDefined();

    // highlight annotation
    const highlightAnnot = annotations.find((a) => a.type === 'highlight');
    expect(highlightAnnot).toBeDefined();
    expect(highlightAnnot!.contents).toBe('Highlighted text');
    expect(highlightAnnot!.bounds).toBeDefined();

    page.close();
    doc.destroy();
  });
});
