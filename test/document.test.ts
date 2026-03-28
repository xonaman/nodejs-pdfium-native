import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument, PDFiumPage } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('PDFiumDocument', () => {
  it('returns correct page count', async () => {
    const doc = await loadDocument(fixture('two-page.pdf'));
    expect(doc.pageCount).toBe(2);
    doc.destroy();
  });

  it('getPage returns a PDFiumPage', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    expect(page).toBeInstanceOf(PDFiumPage);
    page.close();
    doc.destroy();
  });

  it('getPage rejects for out-of-range index', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    await expect(doc.getPage(5)).rejects.toThrow();
    await expect(doc.getPage(-1)).rejects.toThrow();
    doc.destroy();
  });

  it('destroy prevents further use', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    doc.destroy();
    await expect(doc.getPage(0)).rejects.toThrow('Document is destroyed');
  });

  it('pages() iterates over all pages', async () => {
    const doc = await loadDocument(fixture('two-page.pdf'));
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

describe('PDFiumDocument.getMetadata', () => {
  it('returns metadata object with expected keys', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
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
    const doc = await loadDocument(fixture('metadata.pdf'));
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
    const doc = await loadDocument(fixture('minimal.pdf'));
    const bookmarks = doc.getBookmarks();
    expect(Array.isArray(bookmarks)).toBe(true);
    expect(bookmarks.length).toBe(0);
    doc.destroy();
  });

  it('returns bookmarks with titles and page indices', async () => {
    const doc = await loadDocument(fixture('bookmarks.pdf'));
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
