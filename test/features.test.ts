import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDocument } from '../lib/index.js';

const fixture = (name: string) => resolve(import.meta.dirname!, 'fixtures', name);

describe('PDFiumPage.getLinks', () => {
  it('returns an empty array for a page with no links', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    const links = await page.getLinks();
    expect(Array.isArray(links)).toBe(true);
    expect(links.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('returns links with URLs and page indices', async () => {
    const doc = await loadDocument(fixture('links.pdf'));
    const page = await doc.getPage(0);
    const links = await page.getLinks();
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
    expect(externalLink!.actionType).toBe('uri');

    page.close();
    doc.destroy();
  });
});

describe('PDFiumPage.getAnnotations', () => {
  it('returns an empty array for a page with no annotations', async () => {
    const doc = await loadDocument(fixture('minimal.pdf'));
    const page = await doc.getPage(0);
    const annotations = await page.getAnnotations();
    expect(Array.isArray(annotations)).toBe(true);
    expect(annotations.length).toBe(0);
    page.close();
    doc.destroy();
  });

  it('returns annotations with types, bounds, and contents', async () => {
    const doc = await loadDocument(fixture('annotations.pdf'));
    const page = await doc.getPage(0);
    const annotations = await page.getAnnotations();
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
    // quad points should be present for highlight annotations
    if (highlightAnnot!.quadPoints) {
      expect(Array.isArray(highlightAnnot!.quadPoints)).toBe(true);
      if (highlightAnnot!.quadPoints.length > 0) {
        const qp = highlightAnnot!.quadPoints[0]!;
        expect(typeof qp.x1).toBe('number');
        expect(typeof qp.y1).toBe('number');
        expect(typeof qp.x4).toBe('number');
        expect(typeof qp.y4).toBe('number');
      }
    }

    page.close();
    doc.destroy();
  });

  it('returns annotation metadata (author, subject, dates, flags)', async () => {
    const doc = await loadDocument(fixture('rich-annotations.pdf'));
    const page = await doc.getPage(0);
    const annotations = await page.getAnnotations();
    expect(annotations.length).toBe(1);

    const annot = annotations[0]!;
    expect(annot.type).toBe('text');
    expect(annot.contents).toBe('Test note');
    expect(annot.author).toBe('John Doe');
    expect(annot.subject).toBe('Review Comment');
    expect(annot.creationDate).toBe('D:20250101120000Z');
    expect(annot.modDate).toBe('D:20250102120000Z');
    expect(annot.flags).toBe(4); // print flag

    page.close();
    doc.destroy();
  });

  it('returns border and interior color for square/circle annotations', async () => {
    const doc = await loadDocument(fixture('border-annotations.pdf'));
    const page = await doc.getPage(0);
    const annotations = await page.getAnnotations();
    expect(annotations.length).toBe(2);

    // square annotation with border and interior color
    const square = annotations.find((a) => a.type === 'square');
    expect(square).toBeDefined();
    expect(square!.contents).toBe('Filled square');
    expect(square!.color).toBeDefined(); // blue stroke
    if (square!.interiorColor) {
      expect(square!.interiorColor.r).toBeGreaterThan(200); // yellow = high R
      expect(square!.interiorColor.g).toBeGreaterThan(200); // yellow = high G
    }
    if (square!.border) {
      expect(typeof square!.border.width).toBe('number');
      expect(square!.border.width).toBeGreaterThan(0);
    }

    // circle annotation with interior color
    const circle = annotations.find((a) => a.type === 'circle');
    expect(circle).toBeDefined();
    expect(circle!.contents).toBe('Filled circle');
    if (circle!.interiorColor) {
      expect(circle!.interiorColor.g).toBeGreaterThan(200); // green fill
    }

    page.close();
    doc.destroy();
  });
});
