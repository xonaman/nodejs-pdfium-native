import { describe, expect, it } from 'vitest';
import { loadDocument } from '../index.js';
import { fixturePath, pdfBuffer } from './fixtures.js';

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
    const doc = await loadDocument(fixturePath('links.pdf'));
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
    const doc = await loadDocument(fixturePath('annotations.pdf'));
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
